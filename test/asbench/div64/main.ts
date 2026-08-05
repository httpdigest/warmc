// 64-bit signed and unsigned division and remainder.  arm64 has the divide but
// not the remainder, so every `%` here is a divide and a multiply-subtract, and
// the divisor varies so nothing turns into a shift.
import { print } from "../wasi";

let acc: i64 = 0;
let uacc: u64 = 0;
for (let r = 1; r < 50000000; r++) {
  let n = <i64>r * 6364136223846793005;
  let d = <i64>r + 3;
  acc += n / d + n % d;
  let un = <u64>n | 1;
  let ud = <u64>r * 7 + 5;
  uacc += un / ud + un % ud;
}
print("acc = " + acc.toString() + " uacc = " + uacc.toString());
