// Unsafe Rust: raw pointers (*const / *mut) with add/offset/sub and
// offset_from, read/write and their _unaligned forms, ptr::copy (memmove,
// overlapping) and copy_nonoverlapping (memcpy), ptr::swap, write_bytes,
// slice::from_raw_parts(_mut), MaybeUninit arrays initialised element by
// element, transmute between same-size types (floats <-> ints, arrays <->
// integers, a #[repr(C)] struct <-> bytes), unions, NonNull, a hand-rolled
// Vec on std::alloc, alignment queries, and byte views of repr(C) structs.
//
// These are the operations where Rust hands the back end bare loads and
// stores at arbitrary offsets and widths -- an unaligned u64 read at an odd
// address, a memmove whose ranges overlap in either direction, a store of 3
// bytes through write_bytes.  wasm permits unaligned access everywhere, so
// the native code has to as well, at every width.

// The transmutes below are the point, even where a safe method exists.
#![allow(unnecessary_transmutes)]

use std::alloc::{alloc, dealloc, realloc, Layout};
use std::hint::black_box as bb;
use std::mem::{self, MaybeUninit};
use std::ptr::{self, NonNull};

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
struct Header {
    magic: u32,
    version: u16,
    flags: u8,
    kind: u8,
    length: u64,
}

#[repr(C)]
union Bits {
    f: f32,
    u: u32,
    b: [u8; 4],
}

struct RawVec<T> {
    ptr: NonNull<T>,
    cap: usize,
    len: usize,
}

impl<T: Copy + std::fmt::Debug> RawVec<T> {
    fn new() -> Self {
        RawVec { ptr: NonNull::dangling(), cap: 0, len: 0 }
    }
    fn push(&mut self, v: T) {
        if self.len == self.cap {
            let new_cap = if self.cap == 0 { 2 } else { self.cap * 2 };
            let new_layout = Layout::array::<T>(new_cap).unwrap_or_else(|_| Layout::new::<T>());
            let p = unsafe {
                if self.cap == 0 {
                    alloc(new_layout)
                } else {
                    let old = Layout::array::<T>(self.cap).unwrap_or_else(|_| Layout::new::<T>());
                    realloc(self.ptr.as_ptr() as *mut u8, old, new_layout.size())
                }
            };
            self.ptr = NonNull::new(p as *mut T).unwrap_or(NonNull::dangling());
            self.cap = new_cap;
        }
        unsafe { self.ptr.as_ptr().add(self.len).write(v) };
        self.len += 1;
    }
    fn insert(&mut self, i: usize, v: T) {
        self.push(v);
        unsafe {
            let p = self.ptr.as_ptr().add(i);
            ptr::copy(p, p.add(1), self.len - 1 - i);
            p.write(v);
        }
    }
    fn remove(&mut self, i: usize) -> T {
        unsafe {
            let p = self.ptr.as_ptr().add(i);
            let v = p.read();
            ptr::copy(p.add(1), p, self.len - 1 - i);
            self.len -= 1;
            v
        }
    }
    fn as_slice(&self) -> &[T] {
        unsafe { std::slice::from_raw_parts(self.ptr.as_ptr(), self.len) }
    }
}

impl<T> Drop for RawVec<T> {
    fn drop(&mut self) {
        if self.cap > 0 {
            if let Ok(l) = Layout::array::<T>(self.cap) {
                unsafe { dealloc(self.ptr.as_ptr() as *mut u8, l) };
            }
        }
    }
}

fn pointers() {
    let mut arr: [i32; 10] = std::array::from_fn(|i| (i * i) as i32);
    let p = arr.as_mut_ptr();
    unsafe {
        println!("read {} {} {}", *p, *p.add(3), p.add(9).read());
        *p.add(1) = -1;
        p.add(2).write(-2);
        let q = p.add(7);
        println!("offset_from {} {} sub-read {}", q.offset_from(p), p.offset_from(q), *q.sub(2));
        ptr::swap(p, p.add(9));
        let s = std::slice::from_raw_parts_mut(p.add(4), 4);
        s.reverse();
        let cp = p as *const i32;
        let mut sum = 0;
        let mut it = cp;
        let end = cp.add(10);
        while it < end {
            sum += *it;
            it = it.offset(1);
        }
        println!("after writes {:?} sum {}", arr, sum);
    }
    let mut buf: Vec<u8> = (0..32).collect();
    unsafe {
        let b = buf.as_mut_ptr();
        ptr::copy(b, b.add(4), 10);
        println!("copy forward overlap {:?}", &buf[..16]);
        let b = buf.as_mut_ptr();
        ptr::copy(b.add(10), b.add(6), 12);
        println!("copy backward overlap {:?}", &buf[..24]);
        let b = buf.as_mut_ptr();
        ptr::copy_nonoverlapping(b.add(24), b, 8);
        ptr::write_bytes(b.add(8), 0xee, 3);
        println!("copy_nonoverlapping + write_bytes {:?}", buf);
    }
    let src: Vec<u64> = (1..=5).map(|x| x * 0x0101_0101_0101_0101).collect();
    let mut dst = vec![0u64; 5];
    unsafe { ptr::copy_nonoverlapping(src.as_ptr(), dst.as_mut_ptr(), 5) };
    println!("u64 copy {:x?}", dst);
}

fn unaligned() {
    let bytes: Vec<u8> = (0..40u8).map(|x| x.wrapping_mul(37).wrapping_add(11)).collect();
    let p = bytes.as_ptr();
    for off in [0usize, 1, 2, 3, 5, 7, 13] {
        let off = bb(off);
        unsafe {
            let a = (p.add(off) as *const u16).read_unaligned();
            let b = (p.add(off) as *const u32).read_unaligned();
            let c = (p.add(off) as *const u64).read_unaligned();
            let d = (p.add(off) as *const u128).read_unaligned();
            let f = (p.add(off) as *const f64).read_unaligned();
            println!("off {:2}: u16 {:04x} u32 {:08x} u64 {:016x} u128 {:032x} f64 bits {:016x}", off, a, b, c, d, f.to_bits());
        }
    }
    let mut out = vec![0u8; 24];
    let q = out.as_mut_ptr();
    unsafe {
        (q.add(1) as *mut u32).write_unaligned(bb(0xdead_beef));
        (q.add(6) as *mut u64).write_unaligned(bb(0x0102_0304_0506_0708));
        (q.add(15) as *mut u16).write_unaligned(bb(0xabcd));
        (q.add(17) as *mut f32).write_unaligned(bb(-1.5));
    }
    println!("unaligned writes {:02x?}", out);
    let le = u32::from_le_bytes([out[1], out[2], out[3], out[4]]);
    println!("read back via from_le_bytes {:08x}", le);
}

fn maybe_uninit() {
    let mut arr: [MaybeUninit<String>; 5] = unsafe { MaybeUninit::uninit().assume_init() };
    for (i, slot) in arr.iter_mut().enumerate() {
        slot.write(format!("item-{}", i * 11));
    }
    let done: [String; 5] = unsafe { mem::transmute(arr) };
    println!("MaybeUninit array {:?}", done);
    let mut x = MaybeUninit::<u64>::uninit();
    unsafe { x.as_mut_ptr().write(bb(77)) };
    let x = unsafe { x.assume_init() };
    println!("MaybeUninit scalar {}", x);
    let z: [u32; 6] = unsafe { MaybeUninit::zeroed().assume_init() };
    println!("zeroed {:?}", z);
    let mut buf = [MaybeUninit::<u8>::uninit(); 16];
    let msg = b"partial";
    for (d, s) in buf.iter_mut().zip(msg.iter()) {
        d.write(*s);
    }
    let init: &[u8] = unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const u8, msg.len()) };
    println!("partial init {:?}", std::str::from_utf8(init));
}

fn transmutes() {
    for f in [1.0f32, -0.0, 0.1, f32::MAX, f32::INFINITY] {
        let u: u32 = unsafe { mem::transmute(bb(f)) };
        let back: f32 = unsafe { mem::transmute(u) };
        println!("f32 {:?} -> {:08x} -> {:?} same as to_bits {}", f, u, back, u == f.to_bits());
    }
    let d: u64 = unsafe { mem::transmute(bb(std::f64::consts::PI)) };
    println!("pi bits {:016x}", d);
    let arr: [u8; 8] = unsafe { mem::transmute(bb(0x0807_0605_0403_0201u64)) };
    println!("u64 -> [u8; 8] {:?}", arr);
    let pair: [u32; 2] = unsafe { mem::transmute(bb(0xaaaa_bbbb_cccc_ddddu64)) };
    println!("u64 -> [u32; 2] {:x?}", pair);
    let h = Header { magic: 0x5741_534d, version: 3, flags: 0b1010, kind: 7, length: 0x1122_3344_5566_7788 };
    let raw: [u8; 16] = unsafe { mem::transmute(h) };
    println!("header bytes {:02x?}", raw);
    let back: Header = unsafe { mem::transmute(raw) };
    println!("header back {:?} eq {}", back, back == h);
    let mut raw2 = raw;
    raw2[4] = 9;
    raw2[7] = 1;
    let h2: Header = unsafe { ptr::read(raw2.as_ptr() as *const Header) };
    println!("patched header {:?}", h2);
    let mut u = Bits { u: 0x3fc0_0000 };
    unsafe {
        println!("union f {} u {:08x} b {:?}", u.f, u.u, u.b);
        u.b[3] = 0xc0;
        println!("union after byte write f {} u {:08x}", u.f, u.u);
        u.f = 0.15625;
        println!("union after f write u {:08x} b {:?}", u.u, u.b);
    }
    let words: [u16; 4] = [0x1234, 0x5678, 0x9abc, 0xdef0];
    let as_bytes: &[u8] = unsafe { std::slice::from_raw_parts(words.as_ptr() as *const u8, 8) };
    println!("u16 slice as bytes {:02x?}", as_bytes);
    let i: i32 = -1;
    let as_u: u32 = unsafe { mem::transmute(bb(i)) };
    println!("i32 -1 as u32 {}", as_u);
    let c: char = unsafe { mem::transmute(bb(0x1f980u32)) };
    println!("u32 -> char {:?}", c);
}

fn layouts() {
    println!(
        "size/align Header {}/{} u128 {}/{} f64 {}/{} (u8,u32) {}/{} [u16;3] {}/{} Bits {}/{}",
        mem::size_of::<Header>(),
        mem::align_of::<Header>(),
        mem::size_of::<u128>(),
        mem::align_of::<u128>(),
        mem::size_of::<f64>(),
        mem::align_of::<f64>(),
        mem::size_of::<(u8, u32)>(),
        mem::align_of::<(u8, u32)>(),
        mem::size_of::<[u16; 3]>(),
        mem::align_of::<[u16; 3]>(),
        mem::size_of::<Bits>(),
        mem::align_of::<Bits>()
    );
    let h = Header { magic: 1, version: 2, flags: 3, kind: 4, length: 5 };
    let base = &h as *const Header as usize;
    println!(
        "field offsets magic {} version {} flags {} kind {} length {}",
        &h.magic as *const _ as usize - base,
        &h.version as *const _ as usize - base,
        &h.flags as *const _ as usize - base,
        &h.kind as *const _ as usize - base,
        &h.length as *const _ as usize - base
    );
    let v = vec![1u64, 2, 3];
    let p = v.as_ptr();
    println!("vec ptr aligned to 8 {} align_offset(16) < 16 {}", p as usize % 8 == 0, p.align_offset(16) < 16);
    let l = Layout::from_size_align(24, 8).map(|l| (l.size(), l.align(), l.pad_to_align().size()));
    let l2 = Layout::new::<Header>().extend(Layout::new::<u8>()).map(|(l, off)| (l.size(), l.align(), off));
    println!("layout {:?} extend {:?}", l, l2);
}

fn main() {
    pointers();
    unaligned();
    maybe_uninit();
    transmutes();
    layouts();
    let mut rv: RawVec<u32> = RawVec::new();
    for i in 0..20 {
        rv.push(bb(i * 3));
    }
    rv.insert(0, 999);
    rv.insert(10, 555);
    let r1 = rv.remove(5);
    let r2 = rv.remove(rv.len - 1);
    println!("rawvec {:?} removed {} {} cap {}", rv.as_slice(), r1, r2, rv.cap);
    let np = NonNull::from(&rv.len);
    println!("nonnull read {}", unsafe { *np.as_ptr() });
    let null: *const u8 = ptr::null();
    println!("null is_null {} non-null {}", null.is_null(), NonNull::new(null as *mut u8).is_none());
}
