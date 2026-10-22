// Arbitrary-precision unsigned integers on Vec<u32> limbs: addition with
// carry, subtraction with borrow, schoolbook multiplication through u64
// partial products, division by a small divisor and long division by a big
// one (Knuth D, normalised), comparison, shifting, decimal and hex
// conversion both ways, and modular exponentiation.  Used to print 100!,
// 2^1000, Fibonacci(1000), large powers, and to check identities
// (a*b/b == a, (a+b)-b == a, gcd, Fermat tests).
//
// This is the integer core of every bignum library: 32x32->64 multiplies,
// carries recovered from the high half, and loops whose trip counts are data.
// Every operation here is checked by an inverse operation, so a wrong carry
// shows twice: in the printed digits and in a failed identity.

use std::cmp::Ordering;
use std::fmt;
use std::hint::black_box as bb;

#[derive(Clone, PartialEq, Eq, Debug)]
struct Big {
    d: Vec<u32>, // little-endian limbs, no trailing zeros
}

impl Big {
    fn zero() -> Big {
        Big { d: vec![] }
    }
    fn from_u64(v: u64) -> Big {
        let mut b = Big { d: vec![v as u32, (v >> 32) as u32] };
        b.trim();
        b
    }
    fn trim(&mut self) {
        while self.d.last() == Some(&0) {
            self.d.pop();
        }
    }
    fn is_zero(&self) -> bool {
        self.d.is_empty()
    }
    fn add(&self, o: &Big) -> Big {
        let n = self.d.len().max(o.d.len());
        let mut r = Vec::with_capacity(n + 1);
        let mut carry = 0u64;
        for i in 0..n {
            let s = *self.d.get(i).unwrap_or(&0) as u64 + *o.d.get(i).unwrap_or(&0) as u64 + carry;
            r.push(s as u32);
            carry = s >> 32;
        }
        if carry > 0 {
            r.push(carry as u32);
        }
        Big { d: r }
    }
    // Requires self >= o.
    fn sub(&self, o: &Big) -> Big {
        let mut r = Vec::with_capacity(self.d.len());
        let mut borrow = 0i64;
        for i in 0..self.d.len() {
            let mut s = self.d[i] as i64 - *o.d.get(i).unwrap_or(&0) as i64 - borrow;
            borrow = 0;
            if s < 0 {
                s += 1 << 32;
                borrow = 1;
            }
            r.push(s as u32);
        }
        let mut b = Big { d: r };
        b.trim();
        b
    }
    fn mul(&self, o: &Big) -> Big {
        if self.is_zero() || o.is_zero() {
            return Big::zero();
        }
        let mut r = vec![0u32; self.d.len() + o.d.len()];
        for (i, &a) in self.d.iter().enumerate() {
            let mut carry = 0u64;
            for (j, &b) in o.d.iter().enumerate() {
                let t = a as u64 * b as u64 + r[i + j] as u64 + carry;
                r[i + j] = t as u32;
                carry = t >> 32;
            }
            let mut k = i + o.d.len();
            while carry > 0 {
                let t = r[k] as u64 + carry;
                r[k] = t as u32;
                carry = t >> 32;
                k += 1;
            }
        }
        let mut b = Big { d: r };
        b.trim();
        b
    }
    fn mul_small(&self, m: u32) -> Big {
        let mut r = Vec::with_capacity(self.d.len() + 1);
        let mut carry = 0u64;
        for &x in &self.d {
            let t = x as u64 * m as u64 + carry;
            r.push(t as u32);
            carry = t >> 32;
        }
        if carry > 0 {
            r.push(carry as u32);
        }
        let mut b = Big { d: r };
        b.trim();
        b
    }
    fn divmod_small(&self, m: u32) -> (Big, u32) {
        let mut q = vec![0u32; self.d.len()];
        let mut rem = 0u64;
        for i in (0..self.d.len()).rev() {
            let cur = (rem << 32) | self.d[i] as u64;
            q[i] = (cur / m as u64) as u32;
            rem = cur % m as u64;
        }
        let mut b = Big { d: q };
        b.trim();
        (b, rem as u32)
    }
    fn shl(&self, bits: usize) -> Big {
        let (w, b) = (bits / 32, bits % 32);
        let mut r = vec![0u32; w];
        let mut carry = 0u32;
        for &x in &self.d {
            if b == 0 {
                r.push(x);
            } else {
                r.push((x << b) | carry);
                carry = x >> (32 - b);
            }
        }
        if carry > 0 {
            r.push(carry);
        }
        let mut out = Big { d: r };
        out.trim();
        out
    }
    fn shr(&self, bits: usize) -> Big {
        let (w, b) = (bits / 32, bits % 32);
        if w >= self.d.len() {
            return Big::zero();
        }
        let src = &self.d[w..];
        let mut r = Vec::with_capacity(src.len());
        for i in 0..src.len() {
            let lo = src[i] >> b;
            let hi = if b > 0 && i + 1 < src.len() { src[i + 1] << (32 - b) } else { 0 };
            r.push(lo | hi);
        }
        let mut out = Big { d: r };
        out.trim();
        out
    }
    fn bits(&self) -> usize {
        match self.d.last() {
            None => 0,
            Some(&top) => (self.d.len() - 1) * 32 + (32 - top.leading_zeros() as usize),
        }
    }
    // Knuth algorithm D.
    fn divmod(&self, v: &Big) -> (Big, Big) {
        if v.d.len() == 1 {
            let (q, r) = self.divmod_small(v.d[0]);
            return (q, Big::from_u64(r as u64));
        }
        if self.cmp(v) == Ordering::Less {
            return (Big::zero(), self.clone());
        }
        let s = v.d[v.d.len() - 1].leading_zeros() as usize;
        let vn = v.shl(s);
        let mut un = self.shl(s).d;
        un.push(0);
        if un.len() < self.d.len() + 1 {
            un.resize(self.d.len() + 1, 0);
        }
        let n = vn.d.len();
        let m = un.len() - n - 1;
        let mut q = vec![0u32; m + 1];
        let b = 1u64 << 32;
        for j in (0..=m).rev() {
            let num = ((un[j + n] as u64) << 32) | un[j + n - 1] as u64;
            let mut qhat = num / vn.d[n - 1] as u64;
            let mut rhat = num % vn.d[n - 1] as u64;
            while qhat >= b || qhat * vn.d[n - 2] as u64 > ((rhat << 32) | un[j + n - 2] as u64) {
                qhat -= 1;
                rhat += vn.d[n - 1] as u64;
                if rhat >= b {
                    break;
                }
            }
            let mut borrow = 0i64;
            let mut carry = 0u64;
            for i in 0..n {
                let p = qhat * vn.d[i] as u64 + carry;
                carry = p >> 32;
                let t = un[i + j] as i64 - borrow - (p & 0xffff_ffff) as i64;
                un[i + j] = t as u32;
                borrow = if t < 0 { 1 } else { 0 };
            }
            let t = un[j + n] as i64 - borrow - carry as i64;
            un[j + n] = t as u32;
            if t < 0 {
                qhat -= 1;
                let mut c = 0u64;
                for i in 0..n {
                    let s = un[i + j] as u64 + vn.d[i] as u64 + c;
                    un[i + j] = s as u32;
                    c = s >> 32;
                }
                un[j + n] = un[j + n].wrapping_add(c as u32);
            }
            q[j] = qhat as u32;
        }
        let mut qb = Big { d: q };
        qb.trim();
        un.truncate(n);
        let mut rb = Big { d: un };
        rb.trim();
        (qb, rb.shr(s))
    }
    fn pow(&self, mut e: u32) -> Big {
        let mut acc = Big::from_u64(1);
        let mut b = self.clone();
        while e > 0 {
            if e & 1 == 1 {
                acc = acc.mul(&b);
            }
            b = b.mul(&b);
            e >>= 1;
        }
        acc
    }
    fn powmod(&self, e: &Big, m: &Big) -> Big {
        let mut acc = Big::from_u64(1);
        let mut b = self.divmod(m).1;
        for i in 0..e.bits() {
            if e.d[i / 32] >> (i % 32) & 1 == 1 {
                acc = acc.mul(&b).divmod(m).1;
            }
            b = b.mul(&b).divmod(m).1;
        }
        acc
    }
    fn parse(s: &str) -> Option<Big> {
        let mut r = Big::zero();
        for c in s.chars() {
            let d = c.to_digit(10)?;
            r = r.mul_small(10).add(&Big::from_u64(d as u64));
        }
        Some(r)
    }
    fn to_hex(&self) -> String {
        if self.is_zero() {
            return "0".into();
        }
        let mut s = format!("{:x}", self.d[self.d.len() - 1]);
        for x in self.d.iter().rev().skip(1) {
            s.push_str(&format!("{:08x}", x));
        }
        s
    }
}

impl Ord for Big {
    fn cmp(&self, o: &Big) -> Ordering {
        self.d.len().cmp(&o.d.len()).then_with(|| self.d.iter().rev().cmp(o.d.iter().rev()))
    }
}
impl PartialOrd for Big {
    fn partial_cmp(&self, o: &Big) -> Option<Ordering> {
        Some(self.cmp(o))
    }
}

impl fmt::Display for Big {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if self.is_zero() {
            return f.pad("0");
        }
        let mut chunks = vec![];
        let mut cur = self.clone();
        while !cur.is_zero() {
            let (q, r) = cur.divmod_small(1_000_000_000);
            chunks.push(r);
            cur = q;
        }
        let mut s = chunks.last().map(|c| c.to_string()).unwrap_or_default();
        for c in chunks.iter().rev().skip(1) {
            s.push_str(&format!("{:09}", c));
        }
        f.pad(&s)
    }
}

fn gcd(a: &Big, b: &Big) -> Big {
    let (mut a, mut b) = (a.clone(), b.clone());
    while !b.is_zero() {
        let r = a.divmod(&b).1;
        a = b;
        b = r;
    }
    a
}

fn main() {
    let mut f = Big::from_u64(1);
    for i in 1..=100u32 {
        f = f.mul_small(bb(i));
        if i % 10 == 0 || i == 25 {
            println!("{}! = {}", i, f);
        }
    }
    let digit_sum: u32 = f.to_string().chars().filter_map(|c| c.to_digit(10)).sum();
    println!("100! digits {} digit sum {} bits {}", f.to_string().len(), digit_sum, f.bits());
    let two = Big::from_u64(2);
    let p1000 = two.pow(1000);
    println!("2^1000 = {}", p1000);
    println!("2^1000 hex {} bits {}", p1000.to_hex(), p1000.bits());
    let (mut a, mut b) = (Big::zero(), Big::from_u64(1));
    for i in 0..=1000 {
        if i % 100 == 0 {
            println!("fib({}) = {}", i, a);
        }
        let c = a.add(&b);
        a = b;
        b = c;
    }
    for (x, e) in [(3u64, 200u32), (7, 77), (123456789, 9), (u64::MAX, 4)] {
        println!("{}^{} = {}", x, e, Big::from_u64(bb(x)).pow(e));
    }

    let x = Big::parse("123456789012345678901234567890123456789012345678901234567890").unwrap_or(Big::zero());
    let y = Big::parse("987654321098765432109876543210987").unwrap_or(Big::zero());
    let prod = x.mul(&y);
    let (q, r) = prod.divmod(&y);
    println!("x*y = {}", prod);
    println!("x*y / y == x {} rem zero {}", q == x, r.is_zero());
    let (q2, r2) = x.divmod(&y);
    println!("x / y = {} rem {}", q2, r2);
    println!("q*y+r == x {}", q2.mul(&y).add(&r2) == x);
    println!("(x+y)-y == x {} x > y {} y < x {}", x.add(&y).sub(&y) == x, x > y, y < x);
    println!("shl/shr {} {}", x.shl(77).shr(77) == x, x.shl(5) == x.mul_small(32));
    println!("gcd(100!, 2^1000) = {}", gcd(&f, &p1000));
    let a6 = Big::parse("3048000000000000000000000000000").unwrap_or(Big::zero());
    let b6 = Big::parse("1234567891011121314151617181920").unwrap_or(Big::zero());
    println!("gcd big = {}", gcd(&a6, &b6));

    // Fermat tests with powmod on numbers near 2^64 and 2^89.
    let m89 = two.pow(89).sub(&Big::from_u64(1)); // Mersenne prime
    let m90 = two.pow(90).sub(&Big::from_u64(1)); // composite
    for (name, m) in [("2^89-1", &m89), ("2^90-1", &m90)] {
        let e = m.sub(&Big::from_u64(1));
        let res: Vec<String> = [2u64, 3, 5, 7].iter().map(|&w| Big::from_u64(w).powmod(&e, m).to_string()).collect();
        println!("fermat {} = {} -> {:?}", name, m, res);
    }
    let modulus = Big::parse("1000000000000000000000007").unwrap_or(Big::zero());
    println!("3^(10^20) mod p = {}", Big::from_u64(3).powmod(&Big::parse("100000000000000000000").unwrap_or(Big::zero()), &modulus));

    // Division by many divisors, checked by reconstruction.
    let mut ok = 0;
    let mut total = 0;
    let mut d = Big::from_u64(0xffff_ffff_ffff_fff1);
    for k in 0..40u32 {
        let (q, r) = f.divmod(&d);
        total += 1;
        if q.mul(&d).add(&r) == f && r < d {
            ok += 1;
        }
        if k % 4 == 0 {
            println!("100! / d{} : q bits {} r {}", k, q.bits(), r);
        }
        d = d.mul_small(0x9e37).add(&Big::from_u64(k as u64 * 12345 + 1));
    }
    println!("division identities {}/{}", ok, total);
    println!("pad [{:>30}] [{:<12}]", Big::from_u64(1234567890123), Big::zero());
}
