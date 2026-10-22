// Const generics and const evaluation: a fixed-size matrix type generic over
// its dimensions with multiplication whose shape is checked at compile time,
// a ring buffer and a bitset parameterised by N, functions generic over array
// length, `const fn` used in array lengths, statics and consts, const blocks,
// associated consts depending on generic parameters, and arrays produced by
// `std::array::from_fn`.
//
// Each distinct N is a separate monomorphised copy with its loop bounds baked
// in as constants, so LLVM unrolls the small ones completely and keeps the
// large ones as loops: the same source line becomes straight-line code at
// N = 2 and a counted loop at N = 64.

use std::hint::black_box as bb;

const fn fib(n: u32) -> u64 {
    let mut a = 0u64;
    let mut b = 1u64;
    let mut i = 0;
    while i < n {
        let t = a + b;
        a = b;
        b = t;
        i += 1;
    }
    a
}

const fn gcd(a: u64, b: u64) -> u64 {
    if b == 0 {
        a
    } else {
        gcd(b, a % b)
    }
}

const fn crc_table() -> [u32; 256] {
    let mut t = [0u32; 256];
    let mut i = 0;
    while i < 256 {
        let mut c = i as u32;
        let mut k = 0;
        while k < 8 {
            c = if c & 1 != 0 { 0xedb8_8320 ^ (c >> 1) } else { c >> 1 };
            k += 1;
        }
        t[i] = c;
        i += 1;
    }
    t
}

const FIB_40: u64 = fib(40);
const GCD: u64 = gcd(1071, 462);
static CRC: [u32; 256] = crc_table();
const SQUARES: [u32; 10] = {
    let mut a = [0u32; 10];
    let mut i = 0;
    while i < 10 {
        a[i] = (i * i) as u32;
        i += 1;
    }
    a
};
static GREETING: &str = "const str";
const LEN: usize = fib(7) as usize; // 13

fn crc32(data: &[u8]) -> u32 {
    let mut c = !0u32;
    for &b in data {
        c = CRC[((c ^ b as u32) & 0xff) as usize] ^ (c >> 8);
    }
    !c
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Matrix<const R: usize, const C: usize> {
    m: [[i64; C]; R],
}

impl<const R: usize, const C: usize> Matrix<R, C> {
    const CELLS: usize = R * C;
    fn from_fn(f: impl Fn(usize, usize) -> i64) -> Self {
        Matrix { m: std::array::from_fn(|r| std::array::from_fn(|c| f(r, c))) }
    }
    fn transpose(&self) -> Matrix<C, R> {
        Matrix::from_fn(|r, c| self.m[c][r])
    }
    fn mul<const K: usize>(&self, o: &Matrix<C, K>) -> Matrix<R, K> {
        let mut out = Matrix { m: [[0i64; K]; R] };
        for r in 0..R {
            for k in 0..K {
                let mut s = 0i64;
                for c in 0..C {
                    s = s.wrapping_add(self.m[r][c].wrapping_mul(o.m[c][k]));
                }
                out.m[r][k] = s;
            }
        }
        out
    }
    fn trace(&self) -> i64 {
        (0..R.min(C)).map(|i| self.m[i][i]).sum()
    }
}

impl<const N: usize> Matrix<N, N> {
    fn identity() -> Self {
        Matrix::from_fn(|r, c| (r == c) as i64)
    }
    fn pow(&self, mut e: u32) -> Self {
        let mut acc = Self::identity();
        let mut b = *self;
        while e > 0 {
            if e & 1 == 1 {
                acc = acc.mul(&b);
            }
            b = b.mul(&b);
            e >>= 1;
        }
        acc
    }
}

struct Ring<T: Copy + Default, const N: usize> {
    buf: [T; N],
    head: usize,
    len: usize,
}

impl<T: Copy + Default + std::fmt::Debug, const N: usize> Ring<T, N> {
    fn new() -> Self {
        Ring { buf: [T::default(); N], head: 0, len: 0 }
    }
    fn push(&mut self, v: T) -> Option<T> {
        let idx = (self.head + self.len) % N;
        if self.len == N {
            let old = self.buf[self.head];
            self.buf[self.head] = v;
            self.head = (self.head + 1) % N;
            Some(old)
        } else {
            self.buf[idx] = v;
            self.len += 1;
            None
        }
    }
    fn items(&self) -> Vec<T> {
        (0..self.len).map(|i| self.buf[(self.head + i) % N]).collect()
    }
}

#[derive(Clone, Copy, PartialEq)]
struct BitSet<const WORDS: usize> {
    w: [u64; WORDS],
}

impl<const WORDS: usize> BitSet<WORDS> {
    const BITS: usize = WORDS * 64;
    fn new() -> Self {
        BitSet { w: [0; WORDS] }
    }
    fn set(&mut self, i: usize) {
        if i < Self::BITS {
            self.w[i / 64] |= 1 << (i % 64);
        }
    }
    fn get(&self, i: usize) -> bool {
        i < Self::BITS && self.w[i / 64] >> (i % 64) & 1 == 1
    }
    fn count(&self) -> u32 {
        self.w.iter().map(|w| w.count_ones()).sum()
    }
    fn union(&self, o: &Self) -> Self {
        let mut r = *self;
        for (a, b) in r.w.iter_mut().zip(o.w.iter()) {
            *a |= *b;
        }
        r
    }
    fn iter(&self) -> impl Iterator<Item = usize> + '_ {
        (0..Self::BITS).filter(move |&i| self.get(i))
    }
}

fn sum_array<const N: usize>(a: [i32; N]) -> i32 {
    a.iter().sum()
}

fn reverse_array<T: Copy, const N: usize>(a: [T; N]) -> [T; N] {
    let mut out = a;
    for i in 0..N {
        out[i] = a[N - 1 - i];
    }
    out
}

fn chunks_of<const K: usize>(v: &[u8]) -> Vec<[u8; K]> {
    v.chunks_exact(K).map(|c| {
        let mut a = [0u8; K];
        a.copy_from_slice(c);
        a
    }).collect()
}

struct Check<const N: usize>;
impl<const N: usize> Check<N> {
    // Evaluated per instantiation at compile time.
    const DOUBLE: usize = {
        assert!(N < 100);
        N * 2
    };
}
fn only_small<const N: usize>() -> usize {
    Check::<N>::DOUBLE
}

fn main() {
    println!("FIB_40 {} GCD {} LEN {} SQUARES {:?} greeting {}", FIB_40, GCD, LEN, SQUARES, GREETING);
    println!("fib at runtime {:?}", (0..15).map(|n| fib(bb(n))).collect::<Vec<_>>());
    println!("crc table head {:08x} {:08x} {:08x} tail {:08x}", CRC[1], CRC[2], CRC[128], CRC[255]);
    for s in ["", "a", "abc", "The quick brown fox jumps over the lazy dog", "123456789"] {
        println!("crc32 {:?} = {:08x}", s, crc32(bb(s.as_bytes())));
    }
    let arr = [0u8; LEN];
    println!("array of const-fn length {}", arr.len());
    let inline_const = const { fib(20) * 2 };
    println!("const block {}", inline_const);

    let a: Matrix<2, 3> = Matrix::from_fn(|r, c| (r * 3 + c) as i64 + 1);
    let b: Matrix<3, 4> = Matrix::from_fn(|r, c| (r as i64 - c as i64) * bb(2));
    let ab = a.mul(&b);
    println!("a {:?}", a.m);
    println!("b {:?}", b.m);
    println!("a*b {:?} trace {}", ab.m, ab.trace());
    println!("a^T {:?} cells {} {} {}", a.transpose().m, Matrix::<2, 3>::CELLS, Matrix::<3, 4>::CELLS, Matrix::<7, 7>::CELLS);
    let f: Matrix<2, 2> = Matrix { m: [[1, 1], [1, 0]] };
    for e in [0u32, 1, 5, 30, 60, 90] {
        println!("fib matrix^{} = {:?}", e, f.pow(bb(e)).m);
    }
    let m4: Matrix<4, 4> = Matrix::from_fn(|r, c| ((r * 7 + c * 3) % 5) as i64 - 2);
    let p = m4.pow(6);
    println!("m4 {:?}", m4.m);
    println!("m4^6 {:?} trace {}", p.m, p.trace());
    let m8: Matrix<8, 8> = Matrix::from_fn(|r, c| if (r + 1) % 8 == c { 1 } else { 0 });
    println!("perm8^8 is identity {} perm8^3 row0 {:?}", m8.pow(8) == Matrix::identity(), m8.pow(3).m[0]);

    let mut ring: Ring<u32, 5> = Ring::new();
    for i in 0..12u32 {
        let ev = ring.push(i * i);
        println!("ring push {} evicted {:?} items {:?}", i * i, ev, ring.items());
    }
    let mut r2: Ring<char, 3> = Ring::new();
    for c in "abcdefg".chars() {
        r2.push(c);
    }
    println!("char ring {:?}", r2.items());

    let mut s1: BitSet<2> = BitSet::new();
    let mut s2: BitSet<2> = BitSet::new();
    for i in (0..128).step_by(7) {
        s1.set(i);
    }
    for i in (0..200).step_by(11) {
        s2.set(i);
    }
    let u = s1.union(&s2);
    println!("bitset bits {} count {} {} union {} members {:?}", BitSet::<2>::BITS, s1.count(), s2.count(), u.count(), u.iter().take(20).collect::<Vec<_>>());
    let mut big: BitSet<16> = BitSet::new();
    for i in 0..1024 {
        if (i * i + 1) % 17 == 0 {
            big.set(i);
        }
    }
    println!("big bitset count {} first {:?}", big.count(), big.iter().take(10).collect::<Vec<_>>());

    println!("sum_array {} {} {}", sum_array([1, 2, 3]), sum_array([10; 50]), sum_array::<0>([]));
    println!("reverse {:?} {:?}", reverse_array([1, 2, 3, 4, 5]), reverse_array(['x', 'y']));
    println!("chunks_of<3> {:?}", chunks_of::<3>(b"abcdefghij"));
    println!("chunks_of<4> {:?}", chunks_of::<4>(&[1, 2, 3, 4, 5, 6, 7, 8, 9]));
    println!("only_small {} {}", only_small::<5>(), only_small::<99>());
    let squares: [u64; 12] = std::array::from_fn(|i| (i as u64).pow(3));
    println!("from_fn cubes {:?}", squares);
    let nested: [[char; 3]; 3] = std::array::from_fn(|r| std::array::from_fn(|c| (b'a' + (r * 3 + c) as u8) as char));
    println!("nested from_fn {:?}", nested);
    println!("array map/zip {:?} {:?}", [1, 2, 3].map(|x| x * 10), [1u8, 2, 3].iter().zip([4u8, 5, 6]).map(|(a, b)| a * b).collect::<Vec<_>>());
    println!("sizes {} {} {}", std::mem::size_of::<Matrix<3, 5>>(), std::mem::size_of::<Ring<u16, 7>>(), std::mem::size_of::<BitSet<3>>());
}
