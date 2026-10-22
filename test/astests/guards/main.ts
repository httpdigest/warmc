// Operations that can trap, kept behind the conditions that make them safe:
// integer division and remainder by a loop-invariant divisor that is zero
// (or INT_MIN / -1) on the paths where the guard is false, loads from an
// invariant address that is out of bounds of linear memory when the guard
// is false, and indirect calls through a table slot that is only valid when
// the guard holds.  A compiler may hoist an invariant computation out of a
// loop or turn a branch into a select, but only if the computation cannot
// trap: here every one would, so the program only finishes (and prints) if
// each stays behind its guard.  The guards are data-dependent, so neither
// binaryen nor warmc can prove them false.
import { print } from "../wasi";

let divisors: i32[] = [0, 7, -1, 0, 3];
let dividends: i32[] = [100, -0x80000000, 55, -9];
let flags: bool[] = [false, true, false, false, true];

function divGuard(a: i32, d: i32, n: i32): i32 {
  let s = 0;
  for (let i = 0; i < n; i++) {
    if (d != 0 && !(a == -0x80000000 && d == -1)) s += a / d + a % d;
    else s += i;
  }
  return s;
}

function divGuard64(a: i64, d: i64, n: i32): i64 {
  let s: i64 = 0;
  for (let i = 0; i < n; i++) {
    s += (d != 0 && d != -1) ? a / d - <i64>(<u64>a % <u64>d) : <i64>i;
  }
  return s;
}

function loadGuard(p: usize, ok: bool, n: i32): i32 {
  let s = 0;
  for (let i = 0; i < n; i++) {
    if (ok) s += load<i32>(p);
    s ^= i;
  }
  return s;
}

function loadGuardTernary(p: usize, ok: bool): i64 {
  return ok ? load<i64>(p, 8) : -1;
}

type Fn = (x: i32) => i32;
function twice(x: i32): i32 { return x * 2; }
function plus9(x: i32): i32 { return x + 9; }
let fns: Fn[] = [twice, plus9];

function callGuard(k: i32, n: i32): i32 {
  let s = 0;
  for (let i = 0; i < n; i++) {
    if (k >= 0 && k < fns.length) s += fns[k](i);
    else s -= 1;
  }
  return s;
}

// Division whose divisor passes through zero (and -1) partway through the loop.
function countdown(a: i32): string {
  let out = "";
  for (let d = 5; d >= -2; d--) {
    out += (d == 0 ? "x" : (d == -1 && a == -0x80000000) ? "ovf" : (a / d).toString() + "r" + (a % d).toString()) + " ";
  }
  return out;
}

function run(): void {
  let bad: usize = <usize>(memory.size() << 16) + 64; // past the end of memory
  let good = changetype<usize>(dividends); // any valid address
  for (let i = 0; i < divisors.length; i++) {
    for (let j = 0; j < dividends.length; j++) {
      print("div a=" + dividends[j].toString() + " d=" + divisors[i].toString() + " -> " + divGuard(dividends[j], divisors[i], 5).toString() +
        " " + divGuard64(<i64>dividends[j] << 20, <i64>divisors[i], 4).toString());
    }
  }
  for (let i = 0; i < flags.length; i++) {
    let p = flags[i] ? good : bad;
    print("load ok=" + (flags[i] ? "1" : "0") + " -> " + loadGuard(p, flags[i], 6).toString() + " " +
      (loadGuardTernary(p, flags[i]) != 0 ? "nz" : "z"));
  }
  for (let k = -2; k <= 3; k++) print("call k=" + k.toString() + " -> " + callGuard(k, 5).toString());
  for (let j = 0; j < dividends.length; j++) print("countdown " + dividends[j].toString() + ": " + countdown(dividends[j]));
  // Short-circuit guards on array bounds: a[i] read only when i is in range.
  let a: i32[] = [3, 1, 4, 1, 5, 9, 2, 6];
  let line = "";
  for (let i = -2; i < 11; i++) line += ((i >= 0 && i < a.length && a[i] > 2) ? "Y" : "n");
  print("bounds " + line);
}
run();
