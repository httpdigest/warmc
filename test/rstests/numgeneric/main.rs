// Generic numeric code over a user-defined `Num` trait implemented by macro
// for i8, i16, i32, i64, u8, u32, u64, f32 and f64: generic sum/product/dot,
// Horner polynomial evaluation, fast exponentiation, gcd for the integer
// subset, mean/variance/min/max statistics, a generic 3x3 determinant and
// matrix product, a generic complex number type, a fixed-point Q16.16 type
// implementing the same trait, and wrapping arithmetic chosen by trait.
//
// Monomorphisation turns each generic function into one copy per type, so
// the same source runs as i8 arithmetic (i32 ops plus sign extension in
// wasm), as u64 arithmetic, and as f32 arithmetic -- the instruction mix
// changes entirely while the program text stays the same.

use std::fmt::{Debug, Display};
use std::hint::black_box as bb;
use std::ops::{Add, Div, Mul, Neg, Sub};

trait Num: Copy + PartialOrd + Debug + Display + Add<Output = Self> + Sub<Output = Self> + Mul<Output = Self> + Div<Output = Self> {
    const ZERO: Self;
    const ONE: Self;
    const NAME: &'static str;
    fn from_i32(v: i32) -> Self;
    fn to_f64(self) -> f64;
    fn wmul(self, o: Self) -> Self;
    fn wadd(self, o: Self) -> Self;
}

macro_rules! impl_int {
    ($($t:ty),*) => {$(
        impl Num for $t {
            const ZERO: Self = 0;
            const ONE: Self = 1;
            const NAME: &'static str = stringify!($t);
            fn from_i32(v: i32) -> Self { v as $t }
            fn to_f64(self) -> f64 { self as f64 }
            fn wmul(self, o: Self) -> Self { self.wrapping_mul(o) }
            fn wadd(self, o: Self) -> Self { self.wrapping_add(o) }
        }
    )*};
}
macro_rules! impl_float {
    ($($t:ty),*) => {$(
        impl Num for $t {
            const ZERO: Self = 0.0;
            const ONE: Self = 1.0;
            const NAME: &'static str = stringify!($t);
            fn from_i32(v: i32) -> Self { v as $t }
            fn to_f64(self) -> f64 { self as f64 }
            fn wmul(self, o: Self) -> Self { self * o }
            fn wadd(self, o: Self) -> Self { self + o }
        }
    )*};
}
impl_int!(i8, i16, i32, i64, u8, u32, u64);
impl_float!(f32, f64);

fn sum<T: Num>(xs: &[T]) -> T {
    xs.iter().fold(T::ZERO, |a, &b| a.wadd(b))
}
fn product<T: Num>(xs: &[T]) -> T {
    xs.iter().fold(T::ONE, |a, &b| a.wmul(b))
}
fn dot<T: Num>(a: &[T], b: &[T]) -> T {
    a.iter().zip(b).fold(T::ZERO, |s, (&x, &y)| s.wadd(x.wmul(y)))
}
fn horner<T: Num>(coef: &[T], x: T) -> T {
    coef.iter().rev().fold(T::ZERO, |acc, &c| acc.wmul(x).wadd(c))
}
fn power<T: Num>(mut b: T, mut e: u32) -> T {
    let mut acc = T::ONE;
    while e > 0 {
        if e & 1 == 1 {
            acc = acc.wmul(b);
        }
        b = b.wmul(b);
        e >>= 1;
    }
    acc
}
fn min_max<T: Num>(xs: &[T]) -> Option<(T, T)> {
    let mut it = xs.iter();
    let first = *it.next()?;
    Some(it.fold((first, first), |(lo, hi), &x| (if x < lo { x } else { lo }, if x > hi { x } else { hi })))
}
fn stats<T: Num>(xs: &[T]) -> (f64, f64) {
    let n = xs.len() as f64;
    let mean = xs.iter().map(|x| x.to_f64()).sum::<f64>() / n;
    let var = xs.iter().map(|x| (x.to_f64() - mean).powi(2)).sum::<f64>() / n;
    (mean, var)
}
fn det3<T: Num>(m: [[T; 3]; 3]) -> T {
    m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
}
fn matmul3<T: Num>(a: [[T; 3]; 3], b: [[T; 3]; 3]) -> [[T; 3]; 3] {
    let mut r = [[T::ZERO; 3]; 3];
    for i in 0..3 {
        for j in 0..3 {
            for k in 0..3 {
                r[i][j] = r[i][j].wadd(a[i][k].wmul(b[k][j]));
            }
        }
    }
    r
}

trait Integer: Num + std::ops::Rem<Output = Self> {}
impl Integer for i32 {}
impl Integer for u64 {}
impl Integer for i64 {}
impl Integer for u8 {}
fn gcd<T: Integer>(a: T, b: T) -> T {
    if b == T::ZERO {
        a
    } else {
        gcd(b, a % b)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, PartialOrd)]
struct Complex<T> {
    re: T,
    im: T,
}
impl<T: Num> Add for Complex<T> {
    type Output = Self;
    fn add(self, o: Self) -> Self {
        Complex { re: self.re + o.re, im: self.im + o.im }
    }
}
impl<T: Num> Mul for Complex<T> {
    type Output = Self;
    fn mul(self, o: Self) -> Self {
        Complex { re: self.re * o.re - self.im * o.im, im: self.re * o.im + self.im * o.re }
    }
}
impl<T: Num> Display for Complex<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "({} + {}i)", self.re, self.im)
    }
}
fn cpow<T: Num>(c: Complex<T>, n: u32) -> Complex<T> {
    let mut acc = Complex { re: T::ONE, im: T::ZERO };
    for _ in 0..n {
        acc = acc * c;
    }
    acc
}

// Q16.16 fixed point that satisfies the same trait.
#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct Fx(i32);
impl Add for Fx {
    type Output = Fx;
    fn add(self, o: Fx) -> Fx {
        Fx(self.0.wrapping_add(o.0))
    }
}
impl Sub for Fx {
    type Output = Fx;
    fn sub(self, o: Fx) -> Fx {
        Fx(self.0.wrapping_sub(o.0))
    }
}
impl Mul for Fx {
    type Output = Fx;
    fn mul(self, o: Fx) -> Fx {
        Fx(((self.0 as i64 * o.0 as i64) >> 16) as i32)
    }
}
impl Div for Fx {
    type Output = Fx;
    fn div(self, o: Fx) -> Fx {
        if o.0 == 0 {
            Fx(i32::MAX)
        } else {
            Fx((((self.0 as i64) << 16) / o.0 as i64) as i32)
        }
    }
}
impl Neg for Fx {
    type Output = Fx;
    fn neg(self) -> Fx {
        Fx(self.0.wrapping_neg())
    }
}
impl Debug for Fx {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "Fx({:.5})", self.0 as f64 / 65536.0)
    }
}
impl Display for Fx {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:.5}", self.0 as f64 / 65536.0)
    }
}
impl Num for Fx {
    const ZERO: Self = Fx(0);
    const ONE: Self = Fx(1 << 16);
    const NAME: &'static str = "Q16.16";
    fn from_i32(v: i32) -> Self {
        Fx(v << 16)
    }
    fn to_f64(self) -> f64 {
        self.0 as f64 / 65536.0
    }
    fn wmul(self, o: Self) -> Self {
        self * o
    }
    fn wadd(self, o: Self) -> Self {
        self + o
    }
}

fn report<T: Num>(raw: &[i32]) {
    let xs: Vec<T> = raw.iter().map(|&v| T::from_i32(bb(v))).collect();
    let ys: Vec<T> = raw.iter().rev().map(|&v| T::from_i32(v)).collect();
    let (mean, var) = stats(&xs);
    println!(
        "{:>6}: sum {} prod {} dot {} horner(2) {} pow(x1,7) {} minmax {:?} mean {:.4} var {:.4}",
        T::NAME,
        sum(&xs),
        product(&xs),
        dot(&xs, &ys),
        horner(&xs, T::from_i32(2)),
        power(xs[1], 7),
        min_max(&xs),
        mean,
        var
    );
    let m = [[xs[0], xs[1], xs[2]], [xs[3], xs[4], xs[5]], [xs[1], xs[0], xs[3]]];
    let id = [[T::ONE, T::ZERO, T::ZERO], [T::ZERO, T::ONE, T::ZERO], [T::ZERO, T::ZERO, T::ONE]];
    let mm = matmul3(m, m);
    println!("{:>6}: det {} m*m row0 {:?} m*I==m {}", T::NAME, det3(m), mm[0], matmul3(m, id) == m);
    let sorted = {
        let mut v = xs.clone();
        v.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        v
    };
    println!("{:>6}: sorted {:?} halves {:?}", T::NAME, sorted, xs.iter().map(|&x| x / T::from_i32(2)).collect::<Vec<_>>());
}

fn main() {
    let raw = [3, 7, 2, 5, 4, 6];
    report::<i8>(&raw);
    report::<i16>(&raw);
    report::<i32>(&raw);
    report::<i64>(&raw);
    report::<u8>(&raw);
    report::<u32>(&raw);
    report::<u64>(&raw);
    report::<f32>(&raw);
    report::<f64>(&raw);
    report::<Fx>(&raw);
    let neg = [-100, 27, -3, 99, -128, 1];
    report::<i8>(&neg);
    report::<i64>(&neg);
    report::<f32>(&neg);
    for (a, b) in [(48i32, 18), (17, 5), (0, 9), (-12, 8)] {
        println!("gcd i32 {} {} = {}", a, b, gcd(bb(a), bb(b)));
    }
    println!("gcd u64 {} u8 {} i64 {}", gcd(bb(1_000_000_007u64 * 6), 1_000_000_007 * 4), gcd(bb(240u8), 54), gcd(bb(-(1i64 << 40)), 1 << 33));
    let c = Complex { re: bb(1.0f64), im: 1.0 };
    for n in [0, 1, 2, 4, 8, 13] {
        println!("(1+i)^{} = {}", n, cpow(c, n));
    }
    let ci = Complex { re: bb(2i64), im: -3 };
    println!("(2-3i)^5 = {} sum {}", cpow(ci, 5), ci + cpow(ci, 2));
    let cf = Complex { re: Fx::from_i32(1) / Fx::from_i32(2), im: Fx::from_i32(3) / Fx::from_i32(4) };
    println!("fixed complex^3 = {}", cpow(cf, 3));
    let cf32 = Complex { re: bb(0.6f32), im: 0.8 };
    println!("unit f32 complex^10 = {:?}", cpow(cf32, 10));
    let fx: Vec<Fx> = (1..=8).map(|i| Fx::from_i32(i) / Fx::from_i32(3)).collect();
    println!("fixed thirds {:?} sum {} prod {}", fx, sum(&fx), product(&fx));
    println!("fixed horner {} power {}", horner(&fx, Fx::from_i32(1) / Fx::from_i32(2)), power(Fx::from_i32(3) / Fx::from_i32(2), 9));
    println!("{}", -Fx::from_i32(5) / Fx::from_i32(4));
}
