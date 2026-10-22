// A switch on a string: an interpreter whose opcodes are names.  binaryen
// cannot make a br_table of it -- each case is a call to String.__eq against a
// constant string in the data segment, tried in source order, and only the
// nest of blocks that receives the answer looks like a switch.  __eq stays out
// of line: an identity check, null checks, a length compare from the object
// headers, then the characters four at a time through i64 loads when both
// strings are 8-byte aligned, then a u16 loop.  The program's opcode strings
// are built by concatenation, so they are never the same object as the case
// literals and the identity shortcut never fires; a late case pays for every
// earlier compare.
//
// switchtab is the integer switch, lowered to a br_table, and the pair of rows
// is the cost of switching on text instead.
import { print } from "../wasi";

function apply(op: string, acc: i32, v: i32): i32 {
  switch (op) {
    case "add": return acc + v;
    case "sub": return acc - v;
    case "mul": return acc * (v | 1);
    case "xor": return acc ^ v;
    case "shl": return acc << (v & 7);
    case "shr": return acc >> (v & 7);
    case "rotl": return rotl<i32>(acc, v & 31);
    case "neg": return -acc;
    case "inc": return acc + 1;
    case "dec": return acc - 1;
    case "and": return acc & (v | 0xff00);
    case "or": return acc | (v & 0xff);
    default: return acc;
  }
}

let names = ["add", "sub", "mul", "xor", "shl", "shr", "rotl", "neg",
             "inc", "dec", "and", "or", "nop", "addx"];
const NPROG = 2048;
// Built by concatenation rather than taken from `names`, so the case
// comparisons see equal strings at different addresses.
let prog = new Array<string>(NPROG);
let args = new Int32Array(NPROG);
let x: u32 = 362436069;
for (let i = 0; i < NPROG; i++) {
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  let nm = names[<i32>(x % <u32>names.length)];
  prog[i] = nm.substring(0, 1) + nm.substring(1);
  args[i] = <i32>(x >> 9);
}

let acc = 7;
for (let r = 0; r < 2100; r++) {
  args[r & (NPROG - 1)] = r;
  for (let i = 0; i < NPROG; i++) acc = apply(prog[i], acc, args[i]);
}
print("acc = " + acc.toString());
