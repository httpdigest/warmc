// Control flow: switch statements that binaryen turns into br_table (dense
// cases, one of them 48 wide), into if-chains (sparse cases), with fall-through, default in the
// middle, cases on i64 and on strings; for/while/do-while with break and
// continue at several nesting depths (AssemblyScript has no labeled
// statements, so leaving an outer loop goes through a flag or a return, as
// real AS code does it); loops that run zero times; ternary chains and
// short-circuit && / || with side effects whose order must be kept; and
// early returns from inside loops.  Every path records what it did, so a
// mis-targeted branch shows as a different trace.
import { print } from "../wasi";

let trace = "";
function t(s: string): bool { trace += s; return true; }
function f(s: string): bool { trace += s; return false; }

function dense(x: i32): string {
  switch (x) {
    case 0: return "zero";
    case 1: return "one";
    case 2: return "two";
    case 3: return "three";
    case 4: return "four";
    case 5: return "five";
    case 6: return "six";
    case 7: return "seven";
    case 8: return "eight";
    case 9: return "nine";
    default: return "many";
  }
}

// A 48-way table switch: one br_table with 48 targets plus a default, the
// jump-table emission and its bounds check.
function big(x: i32): i32 {
  switch (x) {
    case 0: return -500000;
    case 1: return -72201;
    case 2: return 355598;
    case 3: return -216606;
    case 4: return 211193;
    case 5: return -361011;
    case 6: return 66788;
    case 7: return 494587;
    case 8: return -77617;
    case 9: return 350182;
    case 10: return -222022;
    case 11: return 205777;
    case 12: return -366427;
    case 13: return 61372;
    case 14: return 489171;
    case 15: return -83033;
    case 16: return 344766;
    case 17: return -227438;
    case 18: return 200361;
    case 19: return -371843;
    case 20: return 55956;
    case 21: return 483755;
    case 22: return -88449;
    case 23: return 339350;
    case 24: return -232854;
    case 25: return 194945;
    case 26: return -377259;
    case 27: return 50540;
    case 28: return 478339;
    case 29: return -93865;
    case 30: return 333934;
    case 31: return -238270;
    case 32: return 189529;
    case 33: return -382675;
    case 34: return 45124;
    case 35: return 472923;
    case 36: return -99281;
    case 37: return 328518;
    case 38: return -243686;
    case 39: return 184113;
    case 40: return -388091;
    case 41: return 39708;
    case 42: return 467507;
    case 43: return -104697;
    case 44: return 323102;
    case 45: return -249102;
    case 46: return 178697;
    case 47: return -393507;
    default: return -1;
  }
}

function sparse(x: i32): i32 {
  switch (x) {
    case -1000000: return 1;
    case -7: return 2;
    case 0: return 3;
    case 13: return 4;
    case 1000: return 5;
    case 65536: return 6;
    case 0x7fffffff: return 7;
  }
  return 0;
}

function fallthrough(x: i32): string {
  let s = "";
  switch (x & 7) {
    case 0: s += "a";
    case 1: s += "b"; break;
    case 2:
    case 3: s += "c";
    default: s += "d";
    case 5: s += "e"; break;
    case 6: { s += "f"; if (x > 10) break; s += "g"; }
    case 7: s += "h";
  }
  return s;
}

function wide(x: i64): i32 {
  switch (x) {
    case 0: return 0;
    case 1: return 1;
    case 0x100000000: return 2;
    case -1: return 3;
    case 0x7fffffffffffffff: return 4;
    default: return -1;
  }
}

function strSwitch(s: string): i32 {
  switch (s) {
    case "red": return 1;
    case "green": return 2;
    case "blue": return 3;
    case "": return 4;
    default: return 0;
  }
}

// Find the first pair (i, j) with i*j == target: break out of two loops by
// flag, then the same by return.
function findPairFlag(target: i32): string {
  let fi = -1, fj = -1;
  let done = false;
  for (let i = 1; i < 50 && !done; i++) {
    for (let j = i; j < 50; j++) {
      if (i * j > target) break;
      if ((i + j) % 7 == 0) continue;
      if (i * j == target) { fi = i; fj = j; done = true; break; }
    }
  }
  return fi.toString() + "*" + fj.toString();
}
function findPairRet(target: i32): string {
  for (let i = 1; i < 50; i++) {
    for (let j = i; j < 50; j++) {
      if (i * j > target) break;
      if ((i + j) % 7 == 0) continue;
      if (i * j == target) return i.toString() + "*" + j.toString();
    }
  }
  return "none";
}

function classify(x: i32): string {
  return x < 0 ? (x < -100 ? "very-neg" : "neg") : x == 0 ? "zero" : x < 10 ? "small" : x < 100 ? "medium" : x % 2 == 0 ? "big-even" : "big-odd";
}

function run(): void {
  let line = "";
  for (let i = -2; i < 13; i++) line += dense(i) + " ";
  print("dense " + line);
  line = "";
  for (let i = -3; i < 52; i += 2) line += big(i).toString() + " ";
  print("big " + line);
  line = "";
  for (let i = 0; i < 48; i++) line = (big(i ^ 5) + big(47 - i)).toString() + " " + line;
  print("big2 " + line);
  const sp: i32[] = [-1000000, -7, 0, 13, 1000, 65536, 0x7fffffff, 1, -1, 14, 999, -0x80000000];
  line = "";
  for (let i = 0; i < sp.length; i++) line += sparse(sp[i]).toString();
  print("sparse " + line);
  line = "";
  for (let i = 0; i < 24; i++) line += fallthrough(i) + "|";
  print("fallthrough " + line);
  const wv: i64[] = [0, 1, 0x100000000, -1, 0x7fffffffffffffff, 2, 0x100000001, -0x8000000000000000];
  line = "";
  for (let i = 0; i < wv.length; i++) line += wide(wv[i]).toString() + " ";
  print("i64 switch " + line);
  const ss = ["red", "green", "blue", "", "Red", "gree", "greenish", "blue"];
  line = "";
  for (let i = 0; i < ss.length; i++) line += strSwitch(ss[i]).toString();
  print("str switch " + line);
  line = "";
  for (let tg = 1; tg < 200; tg += 13) line += findPairFlag(tg) + "/" + findPairRet(tg) + " ";
  print("pairs " + line);
  line = "";
  const cv: i32[] = [-500, -5, 0, 3, 42, 100, 101, 1000];
  for (let i = 0; i < cv.length; i++) line += classify(cv[i]) + " ";
  print("ternary " + line);
  // Short-circuit order.
  for (let k = 0; k < 8; k++) {
    trace = "";
    let a = (k & 1) != 0, b = (k & 2) != 0, c = (k & 4) != 0;
    let r1 = (a ? t("A") : f("a")) && (b ? t("B") : f("b")) || (c ? t("C") : f("c"));
    let r2 = (a ? t("A") : f("a")) || (b ? t("B") : f("b")) && (c ? t("C") : f("c"));
    let r3 = !((a ? t("A") : f("a")) && !(b ? t("B") : f("b")));
    print("short " + k.toString() + " " + (r1 ? "1" : "0") + (r2 ? "1" : "0") + (r3 ? "1" : "0") + " " + trace);
  }
  // Loops of every shape.
  let n = 0;
  for (let i = 0; i < 0; i++) n++;
  let w = 10;
  while (w < 10) { n++; w++; }
  do { n += 100; } while (false);
  print("zero-trip n=" + n.toString());
  let collatzMax = 0, collatzArg = 0;
  for (let s = 1; s < 300; s++) {
    let x: i64 = s;
    let steps = 0;
    while (x != 1) { x = (x & 1) ? 3 * x + 1 : x >> 1; steps++; }
    if (steps > collatzMax) { collatzMax = steps; collatzArg = s; }
  }
  print("collatz " + collatzArg.toString() + " " + collatzMax.toString());
  // Nested loops with continue at both levels and a count of each exit kind.
  let cont1 = 0, cont2 = 0, brk = 0, body = 0;
  for (let i = 0; i < 30; i++) {
    if (i % 5 == 4) { cont1++; continue; }
    let j = 0;
    while (true) {
      j++;
      if (j > i) { brk++; break; }
      if ((i ^ j) & 1) { cont2++; continue; }
      body += i * j;
    }
  }
  print("nested cont1=" + cont1.toString() + " cont2=" + cont2.toString() + " brk=" + brk.toString() + " body=" + body.toString());
  // do-while with a computed exit, and a loop whose condition has side effects.
  let k = 0, acc = 1;
  do { acc = acc * 3 % 1000003; k++; } while (acc != 1 && k < 100000);
  print("pow3 loop k=" + k.toString() + " acc=" + acc.toString());
  let calls = 0;
  let idx = 0;
  const data: i32[] = [5, 3, 8, 1, 9, 2, 7];
  while ((calls++, idx < data.length) && data[idx] != 9) idx++;
  print("search idx=" + idx.toString() + " calls=" + calls.toString());
  // A state machine: tokenise a string by character class with a switch.
  let src = "x1 = 42 + y_2*(7-3) // done";
  let state = 0, toks: string[] = [], cur = "";
  for (let i = 0; i <= src.length; i++) {
    let c = i < src.length ? src.charCodeAt(i) : 32;
    let cls = (c >= 48 && c <= 57) ? 1 : ((c >= 97 && c <= 122) || c == 95) ? 2 : c == 32 ? 0 : 3;
    switch (state) {
      case 0:
        if (cls == 1) { state = 1; cur = String.fromCharCode(c); }
        else if (cls == 2) { state = 2; cur = String.fromCharCode(c); }
        else if (cls == 3) toks.push("op:" + String.fromCharCode(c));
        break;
      case 1:
        if (cls == 1) cur += String.fromCharCode(c);
        else { toks.push("num:" + cur); state = 0; i--; }
        break;
      case 2:
        if (cls == 1 || cls == 2) cur += String.fromCharCode(c);
        else { toks.push("id:" + cur); state = 0; i--; }
        break;
    }
  }
  print("tokens " + toks.join(" "));
}
run();
