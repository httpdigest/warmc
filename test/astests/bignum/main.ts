// Arbitrary-precision unsigned integers on u32 limbs with u64 intermediates:
// add with carry, subtract with borrow, schoolbook multiply, multiply and
// divide by a small word, long division (Knuth's algorithm D, normalising
// with clz and estimating each quotient digit with a 64-by-32 division),
// comparison, shifts by any bit count, and conversion to decimal by repeated
// division by 10^9.  Computed: factorials up to 120!, 2^n, Fibonacci(400),
// a big product divided back by each factor, a modular exponentiation, and
// the digits of e by the spigot series.  This is carry-chain code -- u64
// adds whose high halves feed the next limb, wrapping u32 subtraction, and
// shifts by 32 - s that are 32 when s is 0 -- where a miscompile of a
// single wrap or extension corrupts every later digit.
import { print } from "../wasi";

class Big {
  limbs: u32[]; // little-endian
  constructor(limbs: u32[] = []) { this.limbs = limbs; this.trim(); }
  static of(v: u64): Big { return new Big([<u32>v, <u32>(v >> 32)]); }
  trim(): Big { while (this.limbs.length > 0 && this.limbs[this.limbs.length - 1] == 0) this.limbs.pop(); return this; }
  get isZero(): bool { return this.limbs.length == 0; }
  clone(): Big { return new Big(this.limbs.slice(0)); }

  add(o: Big): Big {
    let a = this.limbs, b = o.limbs;
    let n = max(a.length, b.length);
    let r = new Array<u32>(n + 1);
    let carry: u64 = 0;
    for (let i = 0; i < n; i++) {
      let s = <u64>(i < a.length ? a[i] : 0) + <u64>(i < b.length ? b[i] : 0) + carry;
      r[i] = <u32>s; carry = s >> 32;
    }
    r[n] = <u32>carry;
    return new Big(r);
  }
  // requires this >= o
  sub(o: Big): Big {
    let a = this.limbs, b = o.limbs;
    let r = new Array<u32>(a.length);
    let borrow: u32 = 0;
    for (let i = 0; i < a.length; i++) {
      let bi: u32 = i < b.length ? b[i] : 0;
      let d = a[i] - bi - borrow;
      borrow = (<u64>a[i] < <u64>bi + <u64>borrow) ? 1 : 0;
      r[i] = d;
    }
    return new Big(r);
  }
  mul(o: Big): Big {
    let a = this.limbs, b = o.limbs;
    if (a.length == 0 || b.length == 0) return new Big();
    let r = new Array<u32>(a.length + b.length);
    for (let i = 0; i < a.length; i++) {
      let carry: u64 = 0;
      let ai = <u64>a[i];
      for (let j = 0; j < b.length; j++) {
        let t = ai * <u64>b[j] + <u64>r[i + j] + carry;
        r[i + j] = <u32>t; carry = t >> 32;
      }
      r[i + b.length] = <u32>carry;
    }
    return new Big(r);
  }
  mulSmall(m: u32): Big {
    let r = new Array<u32>(this.limbs.length + 1);
    let carry: u64 = 0;
    for (let i = 0; i < this.limbs.length; i++) {
      let t = <u64>this.limbs[i] * <u64>m + carry;
      r[i] = <u32>t; carry = t >> 32;
    }
    r[this.limbs.length] = <u32>carry;
    return new Big(r);
  }
  // returns quotient, stores remainder in lastRem
  divSmall(d: u32): Big {
    let r = new Array<u32>(this.limbs.length);
    let rem: u64 = 0;
    for (let i = this.limbs.length - 1; i >= 0; i--) {
      let cur = (rem << 32) | <u64>this.limbs[i];
      r[i] = <u32>(cur / <u64>d);
      rem = cur % <u64>d;
    }
    lastRem = <u32>rem;
    return new Big(r);
  }
  cmp(o: Big): i32 {
    let a = this.limbs, b = o.limbs;
    if (a.length != b.length) return a.length < b.length ? -1 : 1;
    for (let i = a.length - 1; i >= 0; i--) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
  }
  shl(s: i32): Big {
    let w = s >> 5, bits = s & 31;
    let r = new Array<u32>(this.limbs.length + w + 1);
    for (let i = 0; i < this.limbs.length; i++) {
      let v = <u64>this.limbs[i] << <u64>bits;
      r[i + w] |= <u32>v;
      r[i + w + 1] |= <u32>(v >> 32);
    }
    return new Big(r);
  }
  shr(s: i32): Big {
    let w = s >> 5, bits = s & 31;
    if (w >= this.limbs.length) return new Big();
    let r = new Array<u32>(this.limbs.length - w);
    for (let i = w; i < this.limbs.length; i++) {
      let lo = this.limbs[i] >> <u32>bits;
      let hi: u32 = (bits != 0 && i + 1 < this.limbs.length) ? this.limbs[i + 1] << <u32>(32 - bits) : 0;
      r[i - w] = lo | hi;
    }
    return new Big(r);
  }
  // Knuth D: returns [quotient, remainder]
  divmod(dv: Big): Big[] {
    if (dv.limbs.length == 1) { let q = this.divSmall(dv.limbs[0]); return [q, Big.of(lastRem)]; }
    if (this.cmp(dv) < 0) return [new Big(), this.clone()];
    let s = <i32>clz(dv.limbs[dv.limbs.length - 1]);
    let v = dv.shl(s).limbs, u = this.shl(s).limbs;
    let n = v.length;
    while (u.length < this.limbs.length + 1) u.push(0);
    if (u.length == this.limbs.length) u.push(0);
    let m = u.length - n - 1;
    let q = new Array<u32>(m + 1);
    let vtop = <u64>v[n - 1], vnext = <u64>v[n - 2];
    for (let j = m; j >= 0; j--) {
      let num = (<u64>u[j + n] << 32) | <u64>u[j + n - 1];
      let qhat = num / vtop, rhat = num % vtop;
      while (qhat >= 0x100000000 || qhat * vnext > ((rhat << 32) | <u64>u[j + n - 2])) {
        qhat--; rhat += vtop;
        if (rhat >= 0x100000000) break;
      }
      // multiply and subtract
      let borrow: i64 = 0, carry: u64 = 0;
      for (let i = 0; i < n; i++) {
        let p = qhat * <u64>v[i] + carry;
        carry = p >> 32;
        let t = <i64>u[i + j] - borrow - <i64>(p & 0xffffffff);
        u[i + j] = <u32>t;
        borrow = t < 0 ? 1 : 0;
      }
      let t = <i64>u[j + n] - borrow - <i64>carry;
      u[j + n] = <u32>t;
      if (t < 0) {
        qhat--;
        let c: u64 = 0;
        for (let i = 0; i < n; i++) { let s2 = <u64>u[i + j] + <u64>v[i] + c; u[i + j] = <u32>s2; c = s2 >> 32; }
        u[j + n] = <u32>(<u64>u[j + n] + c);
      }
      q[j] = <u32>qhat;
    }
    let rem = new Big(u.slice(0, n)).shr(s);
    return [new Big(q), rem];
  }
  toString(): string {
    if (this.isZero) return "0";
    let parts: string[] = [];
    let cur = this.clone();
    while (!cur.isZero) {
      cur = cur.divSmall(1000000000);
      parts.unshift(lastRem.toString());
    }
    let s = parts[0];
    for (let i = 1; i < parts.length; i++) s += parts[i].padStart(9, "0");
    return s;
  }
}
let lastRem: u32 = 0;

function powmod(b: Big, e: u32, m: Big): Big {
  let r = Big.of(1);
  let base = b.divmod(m)[1];
  while (e != 0) {
    if (e & 1) r = r.mul(base).divmod(m)[1];
    base = base.mul(base).divmod(m)[1];
    e >>= 1;
  }
  return r;
}

function run(): void {
  let f = Big.of(1);
  for (let i: u32 = 1; i <= 120; i++) {
    f = f.mulSmall(i);
    if (i % 15 == 0) print(i.toString() + "! = " + f.toString());
  }
  let p = Big.of(1);
  for (let e = 0; e <= 512; e++) {
    if (e % 64 == 0 || e == 33 || e == 255) print("2^" + e.toString() + " = " + p.toString() + " limbs=" + p.limbs.length.toString());
    p = p.add(p);
  }
  let a = Big.of(0), b = Big.of(1);
  for (let i = 0; i < 400; i++) { let t = a.add(b); a = b; b = t; }
  print("fib400 = " + a.toString());
  // Product of factors, divided back one by one.
  const factors: u64[] = [0xffffffff, 0x100000001, 1000000007, 0xfffffffffffffff, 3, 0x123456789abcdef, 999999999989, 0x8000000000000000];
  let prod = Big.of(1);
  for (let i = 0; i < factors.length; i++) prod = prod.mul(Big.of(factors[i]));
  print("product = " + prod.toString());
  let cur = prod;
  for (let i = factors.length - 1; i >= 0; i--) {
    let qr = cur.divmod(Big.of(factors[i]));
    print("  / " + factors[i].toString() + " = " + qr[0].toString() + " rem " + qr[1].toString());
    cur = qr[0];
  }
  // divmod of a big by a big with a remainder, checked by multiplying back.
  let n = f.add(Big.of(0x123456789));
  let d = p.sub(Big.of(12345)).shr(100);
  let qr = n.divmod(d);
  let back = qr[0].mul(d).add(qr[1]);
  print("divmod q=" + qr[0].toString() + " r=" + qr[1].toString() + " check=" + (back.cmp(n) == 0 ? "ok" : "BAD") + " r<d=" + (qr[1].cmp(d) < 0 ? "1" : "0"));
  // Subtraction chain and shifts.
  let s = f;
  for (let i = 0; i < 5; i++) { s = s.sub(p.shr(i * 50 + 3)); print("sub " + i.toString() + " ..." + s.toString().slice(-60) + " cmp=" + s.cmp(f).toString()); }
  print("shift " + Big.of(0xdeadbeefcafebabe).shl(77).toString() + " " + Big.of(0xdeadbeefcafebabe).shl(77).shr(77).toString() + " " + Big.of(0xdeadbeefcafebabe).shl(64).shr(32).toString());
  // Modular exponentiation: 3^(10^6) mod a 128-bit modulus, and Fermat checks.
  let m = Big.of(0xffffffffffffffc5).mul(Big.of(0xfffffffffffffe95)).add(Big.of(2));
  print("powmod " + powmod(Big.of(3), 1000000, m).toString());
  print("fermat " + powmod(Big.of(2), 1000000006, Big.of(1000000007)).toString() + " " + powmod(Big.of(5), 0xfffffffa, Big.of(0xfffffffb)).toString());
  // e to 200 digits via sum 1/k!: scale 10^210 and add 10^210/k!.
  let scale = Big.of(1);
  for (let i = 0; i < 21; i++) scale = scale.mulSmall(1000000000).mulSmall(10);
  let term = scale.clone(), e = scale.clone();
  for (let k: u32 = 1; k < 160; k++) { term = term.divSmall(k); e = e.add(term); }
  print("e = " + e.toString().substring(0, 200));
}
run();
