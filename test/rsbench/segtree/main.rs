// A generic segment tree over a trait with an associated type, monomorphised
// three times: a range minimum over u32, a range sum over u64, and a
// composition of affine maps mod 998244353 over (u64, u64) pairs, which is not
// commutative, so a query keeps its left and right halves apart.
//
// `Monoid` has an associated `type T` and two associated functions with no
// receiver, and `SegTree<M>` and the driver are written once against it.  rustc
// emits a separate `drive` and `SegTree::set` for each instance, and LLVM inlines
// `query` and the monoid operations into them, so none of the abstraction
// reaches wasm: what differs between the three copies is the element width and
// whether the operator is a `select`, an add, or multiplies and remainders.  The
// affine copy's `% P` arrives as `i64.rem_u` by a constant, because wasm has no
// 64-bit high multiply to expand it with, so the magic-number division there is
// warmc's to do.
//
// A query walks two indices up the tree with branches on their low bits, and
// `set` walks one leaf to the root; with 16,384 leaves the trees are a few
// hundred kilobytes, so the row is about how well three specialisations of one
// generic function are compiled rather than about memory.

trait Monoid {
    type T: Copy;
    fn id() -> Self::T;
    fn op(a: Self::T, b: Self::T) -> Self::T;
}

struct MinU32;
impl Monoid for MinU32 {
    type T = u32;
    fn id() -> u32 {
        u32::MAX
    }
    fn op(a: u32, b: u32) -> u32 {
        a.min(b)
    }
}

struct SumU64;
impl Monoid for SumU64 {
    type T = u64;
    fn id() -> u64 {
        0
    }
    fn op(a: u64, b: u64) -> u64 {
        a.wrapping_add(b)
    }
}

// x -> a*x + b (mod P), composed left to right.  Not commutative, so the tree
// has to keep the left and right halves of a query apart.
const P: u64 = 998_244_353;
struct Affine;
impl Monoid for Affine {
    type T = (u64, u64);
    fn id() -> (u64, u64) {
        (1, 0)
    }
    fn op(f: (u64, u64), g: (u64, u64)) -> (u64, u64) {
        (f.0 * g.0 % P, (f.1 * g.0 + g.1) % P)
    }
}

struct SegTree<M: Monoid> {
    n: usize,
    t: Vec<M::T>,
}

impl<M: Monoid> SegTree<M> {
    fn new(n: usize) -> Self {
        SegTree { n, t: vec![M::id(); 2 * n] }
    }
    fn set(&mut self, mut i: usize, v: M::T) {
        i += self.n;
        self.t[i] = v;
        while i > 1 {
            i >>= 1;
            self.t[i] = M::op(self.t[2 * i], self.t[2 * i + 1]);
        }
    }
    fn query(&self, mut l: usize, mut r: usize) -> M::T {
        let mut left = M::id();
        let mut right = M::id();
        l += self.n;
        r += self.n;
        while l < r {
            if l & 1 == 1 {
                left = M::op(left, self.t[l]);
                l += 1;
            }
            if r & 1 == 1 {
                r -= 1;
                right = M::op(self.t[r], right);
            }
            l >>= 1;
            r >>= 1;
        }
        M::op(left, right)
    }
}

// One generic driver, monomorphised three times.  `gen` turns a random word
// into an element and `fold` turns a query result back into a checksum word.
#[inline(never)]
fn drive<M: Monoid>(n: usize, ops: u32, seed: u64, gen: impl Fn(u64) -> M::T, fold: impl Fn(M::T) -> u64) -> u64 {
    let mut st = SegTree::<M>::new(n);
    let mut x = seed;
    let mut s = 0u64;
    for i in 0..n {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        st.set(i, gen(x));
    }
    for _ in 0..ops {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        let a = (x % n as u64) as usize;
        let b = ((x >> 32) % n as u64) as usize;
        if x & (1 << 20) != 0 {
            st.set(a, gen(x.rotate_left(17)));
        } else {
            let (l, r) = if a <= b { (a, b + 1) } else { (b, a + 1) };
            s = s.wrapping_mul(1_000_003).wrapping_add(fold(st.query(l, r)));
        }
    }
    s
}

fn main() {
    let n = std::hint::black_box(1usize << 14);
    let ops = std::hint::black_box(200_000u32);
    let mut s = 0u64;
    for r in 0..3u64 {
        let seed = 0x9e3779b97f4a7c15 ^ r;
        s ^= drive::<MinU32>(n, ops, seed, |x| (x >> 33) as u32, |v| v as u64);
        s = s.rotate_left(11);
        s ^= drive::<SumU64>(n, ops, seed, |x| x >> 20, |v| v);
        s = s.rotate_left(11);
        s ^= drive::<Affine>(n, ops, seed, |x| ((x >> 34) % P, (x & 0xffff_ffff) % P), |(a, b)| a ^ (b << 30));
    }
    println!("s = {}", s);
}
