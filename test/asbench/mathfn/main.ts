// Math.sin, cos, exp, log, sqrt, pow.  AssemblyScript's Math is its own libm
// compiled into the module -- not a host call and not Darwin's -- so this is a
// few hundred f64 instructions per iteration that warmc has to schedule, and
// the one kernel here where the f64 register file is the constraint.
import { print, bits } from "../wasi";

let s: f64 = 0;
for (let r = 1; r < 5000000; r++) {
  let x = <f64>r * 0.0001;
  s += Math.sin(x) * Math.cos(x) + Math.sqrt(x) - Math.log(x + 1.0);
  s += Math.exp(-x) + Math.pow(x, 1.5);
}
print("s = " + bits(s));
