// First-class functions kept in an array and called through it: a 4096-step
// "program" in which every step calls one of eight small functions, chosen by
// a PRNG, and threads an accumulator through them.  Each step loads a Function
// object out of the Array (a bounds check, then a null check before it can be
// called), stores the argument count to $~argumentsLength, loads the table
// index from the object and makes a call_indirect.  The eight targets come in
// a random order, so unlike hofarr, where a callback is fixed for a whole
// loop, the target changes from one call to the next.
//
// switchtab dispatches the same kind of tiny operation through a br_table
// inside one function; this is that idea with a real call and return per step,
// and an indirect branch the predictor can only learn from history.
import { print } from "../wasi";

type Op = (acc: i32, x: i32) => i32;

function opAdd(acc: i32, x: i32): i32 { return acc + x; }
function opXor(acc: i32, x: i32): i32 { return acc ^ (x << 3); }
function opMul(acc: i32, x: i32): i32 { return acc * (x | 1); }
function opRot(acc: i32, x: i32): i32 { return rotl<i32>(acc, x & 31); }
function opSub(acc: i32, x: i32): i32 { return acc - (x >> 1); }
function opMix(acc: i32, x: i32): i32 { return (acc ^ (acc >>> 15)) + x; }
function opAnd(acc: i32, x: i32): i32 { return (acc & 0x7fffffff) | (x & 1); }
function opNeg(acc: i32, x: i32): i32 { return x - acc; }

const NPROG = 4096;
let ops: Array<Op> = [opAdd, opXor, opMul, opRot, opSub, opMix, opAnd, opNeg];
let prog = new Array<Op>(NPROG);
let args = new Int32Array(NPROG);
let x: u32 = 88172645;
for (let i = 0; i < NPROG; i++) {
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  prog[i] = ops[x & 7];
  args[i] = <i32>(x >> 8);
}

let acc = 1;
for (let r = 0; r < 5200; r++) {
  // Rotate one entry each round, so the program is not the same one twice.
  prog[r & (NPROG - 1)] = ops[(r * 5) & 7];
  for (let i = 0; i < NPROG; i++) acc = prog[i](acc, args[i]);
}
print("acc = " + acc.toString());
