// Operator overloading on a small immutable class: a Mandelbrot set in Complex.
//
// `z = z * z + c` calls `Complex.times` and then `Complex.plus`, and each returns a
// new Complex through `<init>`, which allocates with `struct.new` (fields zeroed)
// and then fills both fields with `struct.set`. That is two allocations per
// iteration, each dead before the next, which is the shape escape analysis
// exists for: at -O2 warmc inlines the operators and keeps the fields in
// registers, and turning off -fescape or -finline makes this several times
// slower. wasmtime allocates every one, so this measures what proving an
// object never escapes is worth on code written in the natural Kotlin style.
class Complex(val re: Double, val im: Double) {
    operator fun plus(o: Complex) = Complex(re + o.re, im + o.im)
    operator fun times(o: Complex) = Complex(re * o.re - im * o.im, re * o.im + im * o.re)
    val abs2: Double get() = re * re + im * im
}

fun mandel(c: Complex, limit: Int): Int {
    var z = Complex(0.0, 0.0)
    var n = 0
    while (n < limit && z.abs2 <= 4.0) {
        z = z * z + c
        n++
    }
    return n
}

fun main() {
    val w = 320
    val h = 240
    var s = 0L
    for (y in 0 until h) {
        for (x in 0 until w) {
            val c = Complex(-2.2 + 3.0 * x / w, -1.2 + 2.4 * y / h)
            s += mandel(c, 1_500)
        }
    }
    println("s = $s")
}
