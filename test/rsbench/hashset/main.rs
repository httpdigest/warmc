// `HashSet` keyed by a struct with a hand-written `Hash` impl, hashed by a
// hand-written `Hasher` rather than the default SipHash: Conway's Life on an
// unbounded board, with the live cells kept as a set of points.
//
// The `Hash` impl packs both coordinates into one `write_u64`, and the hasher is
// FxHash's rotate-xor-multiply.  The state starts at zero, so the rotate folds
// away and hashing a point comes to an `i64.mul` by FxHash's constant, which
// LLVM splits across the two coordinates and partly hoists out of the
// neighbour loops.  Everything else is hashbrown, monomorphised for `Pt` and
// inlined into `step`: a group of eight control bytes is one `i64` load,
// matched SWAR-style against the tag broadcast by a multiply with
// 0x0101010101010101, with `i64.ctz` picking the slot.  The rehash on growth is
// the only call left in the function.
//
// That makes this the other half of mapint.  There the time is SipHash's long
// dependency chain; here the hash is one multiply, so what is left to measure is
// hashbrown's probe loop and the key compare -- nine lookups per candidate cell,
// with hits and misses mixed in a pattern the board decides.

use std::collections::HashSet;
use std::hash::{BuildHasherDefault, Hash, Hasher};

#[derive(Default)]
struct Fx {
    h: u64,
}

impl Fx {
    #[inline]
    fn add(&mut self, w: u64) {
        self.h = (self.h.rotate_left(5) ^ w).wrapping_mul(0x51_7c_c1_b7_27_22_0a_95);
    }
}

impl Hasher for Fx {
    fn finish(&self) -> u64 {
        self.h
    }
    fn write(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.add(b as u64);
        }
    }
    fn write_u64(&mut self, x: u64) {
        self.add(x);
    }
}

type Set = HashSet<Pt, BuildHasherDefault<Fx>>;

#[derive(Clone, Copy, PartialEq, Eq)]
struct Pt {
    x: i32,
    y: i32,
}

impl Hash for Pt {
    fn hash<H: Hasher>(&self, h: &mut H) {
        h.write_u64(((self.x as u32 as u64) << 32) | self.y as u32 as u64);
    }
}

#[inline(never)]
fn step(live: &Set, seen: &mut Set, next: &mut Set) {
    seen.clear();
    next.clear();
    for c in live {
        for dy in -1..=1 {
            for dx in -1..=1 {
                let d = Pt { x: c.x + dx, y: c.y + dy };
                if !seen.insert(d) {
                    continue;
                }
                let mut n = 0u32;
                for ey in -1..=1 {
                    for ex in -1..=1 {
                        if (ex != 0 || ey != 0) && live.contains(&Pt { x: d.x + ex, y: d.y + ey }) {
                            n += 1;
                        }
                    }
                }
                if n == 3 || (n == 2 && live.contains(&d)) {
                    next.insert(d);
                }
            }
        }
    }
}

#[inline(never)]
fn soup(seed: u64, side: i32) -> Set {
    let mut s = Set::default();
    let mut x = seed;
    for y in 0..side {
        for xx in 0..side {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            if x & 3 == 0 {
                s.insert(Pt { x: xx, y });
            }
        }
    }
    s
}

#[inline(never)]
fn digest(s: &Set) -> u64 {
    // A sum of per-cell mixes, so the result does not depend on the order the
    // set iterates in.
    let mut t = 0u64;
    for p in s {
        let k = ((p.x as u32 as u64) << 32) | p.y as u32 as u64;
        t = t.wrapping_add(k.wrapping_mul(0x9e3779b97f4a7c15).rotate_left(23));
    }
    t
}

fn main() {
    let side = std::hint::black_box(48);
    let mut t = 0u64;
    let mut cells = 0u64;
    let mut seen = Set::default();
    let mut next = Set::default();
    for r in 0..3u64 {
        let mut live = soup(0x9e3779b97f4a7c15 ^ (r + 1), side);
        for _ in 0..150 {
            step(&live, &mut seen, &mut next);
            std::mem::swap(&mut live, &mut next);
            cells += live.len() as u64;
        }
        t = t.wrapping_add(digest(&live));
    }
    println!("cells = {} t = {}", cells, t);
}
