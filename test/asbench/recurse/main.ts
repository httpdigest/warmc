// Naive recursive fib: two calls per frame and nothing else in it, so what is
// being measured is the prologue, the epilogue and the return.  Nothing here is
// a tail call, so nothing here can become a jump.
import { print } from "../wasi";

function fib(n: i32): i32 {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

let total = 0;
for (let r = 0; r < 350; r++) total += fib(25 + (r & 1));
print("total = " + total.toString());
