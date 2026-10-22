// kotlin.math: transcendental functions compiled into the module.
//
// WASI has no Math to import, so Kotlin/Wasm ships a port of fdlibm in its
// standard library, and sin, cos, exp, ln, pow, atan2, tanh and hypot are
// ordinary wasm functions in this module: f64 arithmetic, coefficient tables read
// from DoubleArrays, and a double's high and low words taken apart with
// `toRawBits` and shifts, with calls to UInt value-class helpers in between. Only
// sqrt and floor are single instructions, `f64.sqrt` and `f64.floor`. It is the
// numeric kernel of this corpus, and its result is checked bit for bit, so an
// optimiser that reassociates or fuses anything shows up here.
import kotlin.math.*

fun main() {
    var acc = 0.0
    var x = 0.001
    for (i in 0 until 420_000) {
        val s = sin(x)
        val c = cos(x * 0.5)
        val e = exp(-x * 0.01)
        val l = ln(1.0 + x)
        val r = sqrt(x * x + 1.0)
        val a = atan2(s, c)
        val p = (1.0 + x * 1e-3).pow(1.5)
        val t = tanh(s - c)
        acc += s * c + e * l + r * 1e-3 + a + p + t + hypot(s, c) + floor(x) * 1e-6
        x += 0.000731
    }
    // The bits, not a decimal rendering: one ulp of disagreement is the point.
    println("acc = ${acc.toBits()} x = ${x.toBits()}")
}
