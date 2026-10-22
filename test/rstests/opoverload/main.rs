// Operator overloading and the formatting traits: Add/Sub/Mul/Div/Neg/Rem on a
// 2-D vector, a rational number and a small matrix; the *Assign variants;
// Index/IndexMut; Not and BitAnd/BitOr/Shl on a bitset; PartialEq/PartialOrd
// written by hand next to derived Eq/Ord; Display with precision and width
// forwarded through the Formatter; Debug written by hand with debug_struct;
// From/Into, and Sum/Product over custom types.
//
// Most of these return small structs by value, which wasm passes as a pointer
// to a stack slot the caller provides; what this mostly exercises is that
// sret plumbing at -O0 (everything a call) and the scalar-replaced form at -O2
// (everything inlined into registers) agreeing.

use std::cmp::Ordering;
use std::fmt;
use std::iter::{Product, Sum};
use std::ops::{Add, AddAssign, BitAnd, BitOr, Div, Index, IndexMut, Mul, MulAssign, Neg, Not, Rem, Shl, Sub, SubAssign};

#[derive(Debug, Clone, Copy, PartialEq, Default)]
struct V2 {
    x: f64,
    y: f64,
}

impl Add for V2 {
    type Output = V2;
    fn add(self, o: V2) -> V2 {
        V2 { x: self.x + o.x, y: self.y + o.y }
    }
}
impl Sub for V2 {
    type Output = V2;
    fn sub(self, o: V2) -> V2 {
        V2 { x: self.x - o.x, y: self.y - o.y }
    }
}
impl Mul<f64> for V2 {
    type Output = V2;
    fn mul(self, k: f64) -> V2 {
        V2 { x: self.x * k, y: self.y * k }
    }
}
impl Mul<V2> for f64 {
    type Output = V2;
    fn mul(self, v: V2) -> V2 {
        v * self
    }
}
// Dot product as V2 * V2.
impl Mul for V2 {
    type Output = f64;
    fn mul(self, o: V2) -> f64 {
        self.x * o.x + self.y * o.y
    }
}
impl Neg for V2 {
    type Output = V2;
    fn neg(self) -> V2 {
        V2 { x: -self.x, y: -self.y }
    }
}
impl AddAssign for V2 {
    fn add_assign(&mut self, o: V2) {
        self.x += o.x;
        self.y += o.y;
    }
}
impl SubAssign for V2 {
    fn sub_assign(&mut self, o: V2) {
        self.x -= o.x;
        self.y -= o.y;
    }
}
impl MulAssign<f64> for V2 {
    fn mul_assign(&mut self, k: f64) {
        self.x *= k;
        self.y *= k;
    }
}
impl fmt::Display for V2 {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        // Forward the caller's precision to both components.
        match f.precision() {
            Some(p) => write!(f, "({:.*}, {:.*})", p, self.x, p, self.y),
            None => write!(f, "({}, {})", self.x, self.y),
        }
    }
}
impl Sum for V2 {
    fn sum<I: Iterator<Item = V2>>(it: I) -> V2 {
        it.fold(V2::default(), |a, b| a + b)
    }
}
impl<'a> Sum<&'a V2> for V2 {
    fn sum<I: Iterator<Item = &'a V2>>(it: I) -> V2 {
        it.fold(V2::default(), |a, b| a + *b)
    }
}

fn gcd(a: i64, b: i64) -> i64 {
    if b == 0 {
        a.abs()
    } else {
        gcd(b, a % b)
    }
}

#[derive(Clone, Copy, Eq)]
struct Ratio {
    n: i64,
    d: i64,
}
impl Ratio {
    fn new(n: i64, d: i64) -> Ratio {
        let g = gcd(n, d).max(1);
        let s = if d < 0 { -1 } else { 1 };
        Ratio { n: s * n / g, d: s * d / g }
    }
}
impl PartialEq for Ratio {
    fn eq(&self, o: &Ratio) -> bool {
        self.n * o.d == o.n * self.d
    }
}
impl PartialOrd for Ratio {
    fn partial_cmp(&self, o: &Ratio) -> Option<Ordering> {
        Some(self.cmp(o))
    }
}
impl Ord for Ratio {
    fn cmp(&self, o: &Ratio) -> Ordering {
        (self.n * o.d).cmp(&(o.n * self.d))
    }
}
impl Add for Ratio {
    type Output = Ratio;
    fn add(self, o: Ratio) -> Ratio {
        Ratio::new(self.n * o.d + o.n * self.d, self.d * o.d)
    }
}
impl Sub for Ratio {
    type Output = Ratio;
    fn sub(self, o: Ratio) -> Ratio {
        Ratio::new(self.n * o.d - o.n * self.d, self.d * o.d)
    }
}
impl Mul for Ratio {
    type Output = Ratio;
    fn mul(self, o: Ratio) -> Ratio {
        Ratio::new(self.n * o.n, self.d * o.d)
    }
}
impl Div for Ratio {
    type Output = Ratio;
    fn div(self, o: Ratio) -> Ratio {
        Ratio::new(self.n * o.d, self.d * o.n)
    }
}
impl Rem for Ratio {
    type Output = Ratio;
    // a - b * floor(a / b)
    fn rem(self, o: Ratio) -> Ratio {
        let q = self / o;
        let fl = q.n.div_euclid(q.d);
        self - o * Ratio::new(fl, 1)
    }
}
impl Neg for Ratio {
    type Output = Ratio;
    fn neg(self) -> Ratio {
        Ratio::new(-self.n, self.d)
    }
}
impl From<i64> for Ratio {
    fn from(n: i64) -> Ratio {
        Ratio::new(n, 1)
    }
}
impl fmt::Display for Ratio {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let s = if self.d == 1 { format!("{}", self.n) } else { format!("{}/{}", self.n, self.d) };
        // Honour width and alignment by padding the whole string.
        f.pad(&s)
    }
}
impl fmt::Debug for Ratio {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Ratio").field("n", &self.n).field("d", &self.d).finish()
    }
}
impl Product for Ratio {
    fn product<I: Iterator<Item = Ratio>>(it: I) -> Ratio {
        it.fold(Ratio::from(1), |a, b| a * b)
    }
}
impl Sum for Ratio {
    fn sum<I: Iterator<Item = Ratio>>(it: I) -> Ratio {
        it.fold(Ratio::from(0), |a, b| a + b)
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
struct M2([[i64; 2]; 2]);
impl Mul for M2 {
    type Output = M2;
    fn mul(self, o: M2) -> M2 {
        let mut r = [[0i64; 2]; 2];
        for i in 0..2 {
            for j in 0..2 {
                for k in 0..2 {
                    r[i][j] = r[i][j].wrapping_add(self.0[i][k].wrapping_mul(o.0[k][j]));
                }
            }
        }
        M2(r)
    }
}
impl Index<(usize, usize)> for M2 {
    type Output = i64;
    fn index(&self, ij: (usize, usize)) -> &i64 {
        &self.0[ij.0][ij.1]
    }
}
impl IndexMut<(usize, usize)> for M2 {
    fn index_mut(&mut self, ij: (usize, usize)) -> &mut i64 {
        &mut self.0[ij.0][ij.1]
    }
}

fn mat_pow(m: M2, mut e: u32) -> M2 {
    let mut acc = M2([[1, 0], [0, 1]]);
    let mut b = m;
    while e > 0 {
        if e & 1 == 1 {
            acc = acc * b;
        }
        b = b * b;
        e >>= 1;
    }
    acc
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
struct Bits(u64);
impl BitAnd for Bits {
    type Output = Bits;
    fn bitand(self, o: Bits) -> Bits {
        Bits(self.0 & o.0)
    }
}
impl BitOr for Bits {
    type Output = Bits;
    fn bitor(self, o: Bits) -> Bits {
        Bits(self.0 | o.0)
    }
}
impl Not for Bits {
    type Output = Bits;
    fn not(self) -> Bits {
        Bits(!self.0)
    }
}
impl Shl<u32> for Bits {
    type Output = Bits;
    fn shl(self, k: u32) -> Bits {
        Bits(self.0.rotate_left(k))
    }
}
impl fmt::Debug for Bits {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let set: Vec<u32> = (0..64).filter(|i| self.0 >> i & 1 == 1).collect();
        f.debug_set().entries(set.iter()).finish()
    }
}
impl fmt::Binary for Bits {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        fmt::Binary::fmt(&(self.0 as u16), f)
    }
}

// A polynomial with Index into its coefficients, and Display of the terms.
#[derive(Clone, PartialEq, Debug)]
struct Poly(Vec<i64>);
impl Add for &Poly {
    type Output = Poly;
    fn add(self, o: &Poly) -> Poly {
        let n = self.0.len().max(o.0.len());
        Poly((0..n).map(|i| self.0.get(i).unwrap_or(&0) + o.0.get(i).unwrap_or(&0)).collect())
    }
}
impl Mul for &Poly {
    type Output = Poly;
    fn mul(self, o: &Poly) -> Poly {
        let mut r = vec![0i64; self.0.len() + o.0.len() - 1];
        for (i, a) in self.0.iter().enumerate() {
            for (j, b) in o.0.iter().enumerate() {
                r[i + j] += a * b;
            }
        }
        Poly(r)
    }
}
impl Index<usize> for Poly {
    type Output = i64;
    fn index(&self, i: usize) -> &i64 {
        &self.0[i]
    }
}
impl fmt::Display for Poly {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut first = true;
        for (i, &c) in self.0.iter().enumerate().rev() {
            if c == 0 {
                continue;
            }
            if !first {
                write!(f, " {} ", if c < 0 { '-' } else { '+' })?;
            } else if c < 0 {
                write!(f, "-")?;
            }
            first = false;
            let a = c.abs();
            match i {
                0 => write!(f, "{}", a)?,
                1 => write!(f, "{}x", a)?,
                _ => write!(f, "{}x^{}", a, i)?,
            }
        }
        if first {
            write!(f, "0")?;
        }
        Ok(())
    }
}

fn main() {
    let a = V2 { x: 1.5, y: -2.0 };
    let b = V2 { x: 0.25, y: 4.0 };
    println!("a+b {} a-b {} a*3 {} 2*b {} -a {} a.b {}", a + b, a - b, a * 3.0, 2.0 * b, -a, a * b);
    println!("prec {:.3} {:.0} debug {:?}", a + b, a - b, a);
    let mut c = a;
    for i in 0..6 {
        c += b;
        c *= 0.5;
        if i % 2 == 1 {
            c -= a;
        }
        println!("c step {} = {:.6}", i, c);
    }
    let pts: Vec<V2> = (0..10).map(|i| V2 { x: i as f64, y: (i * i) as f64 * 0.5 }).collect();
    let s1: V2 = pts.iter().sum();
    let s2: V2 = pts.iter().map(|p| -*p).sum();
    println!("sum {} neg sum {} eq {}", s1, s2, s1 == -s2);

    let h = Ratio::new(1, 2);
    let t = Ratio::new(1, 3);
    let q = Ratio::new(-3, 4);
    println!("h+t {} h-t {} h*t {} h/t {} q {} -q {} q%t {} h%q {}", h + t, h - t, h * t, h / t, q, -q, q % t, h % q);
    println!("cmp h>t {} q<t {} eq {} max {} min {}", h > t, q < t, Ratio::new(2, 4) == h, h.max(t), q.min(t));
    println!("debug {:?} pad [{:>8}] [{:<8}] [{:^9}]", q, h, t, Ratio::from(5));
    let harmonic: Ratio = (1..=12).map(|k| Ratio::new(1, k)).sum();
    let prod: Ratio = (2..=9).map(|k| Ratio::new(k - 1, k)).product();
    println!("H12 {} prod {}", harmonic, prod);
    let mut rs: Vec<Ratio> = vec![Ratio::new(3, 7), Ratio::new(-1, 2), Ratio::new(5, 11), Ratio::new(2, 5), Ratio::from(0), Ratio::new(9, 20)];
    rs.sort();
    println!("sorted {}", rs.iter().map(|r| r.to_string()).collect::<Vec<_>>().join(" "));
    let mut x = Ratio::from(1);
    for i in 0..8 {
        // Continued fraction for sqrt(2): x -> 1 + 1/(1+x)
        x = Ratio::from(1) + Ratio::from(1) / (Ratio::from(1) + x);
        println!("cf {} -> {} ~ {:.10}", i, x, x.n as f64 / x.d as f64);
    }

    let fib = M2([[1, 1], [1, 0]]);
    for n in [1u32, 2, 10, 50, 90, 92] {
        let m = mat_pow(fib, n);
        println!("fib matrix ^{} = {:?} F = {}", n, m, m[(0, 1)]);
    }
    let mut m = M2([[2, 3], [5, 7]]);
    m[(1, 0)] = -1;
    m[(0, 1)] *= 4;
    println!("indexed {:?} m*m {:?}", m, m * m);

    let s = Bits(0b1011_0110);
    let u = Bits(0b0110_0011);
    println!("and {:?} or {:?} not-and {:?} shl {:?}", s & u, s | u, !s & u, s << 62);
    println!("binary {:b} {:#010b} {:>12b}", s, u, s | u);
    println!("ord {:?} {:?}", s.cmp(&u), Bits(1).max(Bits(2)));

    let p = Poly(vec![1, -3, 0, 2]);
    let r = Poly(vec![-1, 1]);
    println!("p = {} r = {}", p, r);
    println!("p+r = {} p*r = {} p[3] {}", &p + &r, &p * &r, p[3]);
    let mut pw = Poly(vec![1]);
    for i in 0..6 {
        pw = &pw * &Poly(vec![1, 1]);
        println!("(1+x)^{} = {}", i + 1, pw);
    }
    println!("zero poly {}", Poly(vec![0, 0]));
}
