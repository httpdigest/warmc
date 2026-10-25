// java.lang.Math's transcendental functions.
//
// TeaVM leaves sin, cos, exp, log, pow and atan2 to its host; under
// test/teavm/wasi.py they are test/teavm/FdLibm.java, compiled by TeaVM into
// the module, so this is fdlibm as wasm: f64 arithmetic, coefficient tables in
// arrays, and a double's words taken apart with doubleToRawLongBits.  sqrt and
// floor are single instructions, and tanh, hypot and cbrt TeaVM's own Java on
// top of the others.  The result is printed as bits: one ulp is a difference.
public class Main {
    public static void main(String[] args) {
        double acc = 0.0;
        double x = 0.001;
        for (int i = 0; i < 420_000; i++) {
            double s = Math.sin(x);
            double c = Math.cos(x * 0.5);
            double e = Math.exp(-x * 0.01);
            double l = Math.log(1.0 + x);
            double r = Math.sqrt(x * x + 1.0);
            double a = Math.atan2(s, c);
            double p = Math.pow(1.0 + x * 1e-3, 1.5);
            double t = Math.tanh(s - c);
            acc += s * c + e * l + r * 1e-3 + a + p + t + Math.hypot(s, c) + Math.floor(x) * 1e-6;
            x += 0.000731;
        }
        System.out.println("acc = " + Double.doubleToLongBits(acc) + " x = " + Double.doubleToLongBits(x));
    }
}
