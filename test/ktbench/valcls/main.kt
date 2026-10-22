// Value classes: a 16.16 fixed-point type that is an Int until it is boxed.
//
// `value class Fx(val raw: Int)` is erased to its i32 wherever the static type is
// Fx: locals and parameters are plain i32, and each operator is a static function
// such as `Fx__plus-impl` taking and returning i32. Put one into an ArrayList and
// it is boxed with `struct.new $Fx`; read it back and it is unboxed with a
// `ref.cast`; sort the list and each comparison goes through a bridge that casts
// the boxed argument and reads its field before calling the real compareTo. The
// filter loop is i32 and i64 arithmetic behind calls inlining should erase; the
// collection is the price of using the type generically. (`@JvmInline` is
// JVM-only, so Kotlin/Wasm takes the bare `value class`.)
value class Fx(val raw: Int) : Comparable<Fx> {
    operator fun plus(o: Fx) = Fx(raw + o.raw)
    operator fun minus(o: Fx) = Fx(raw - o.raw)
    operator fun times(o: Fx) = Fx(((raw.toLong() * o.raw) shr 16).toInt())
    operator fun div(o: Fx) = Fx(((raw.toLong() shl 16) / o.raw).toInt())
    override operator fun compareTo(other: Fx) = raw.compareTo(other.raw)

    companion object {
        fun of(i: Int) = Fx(i shl 16)
        fun frac(num: Int, den: Int) = Fx(((num.toLong() shl 16) / den).toInt())
    }
}

// Newton's method for the square root, unboxed throughout.
fun fxSqrt(x: Fx): Fx {
    if (x.raw <= 0) return Fx(0)
    var g = if (x > Fx.of(1)) x / Fx.of(2) else Fx.of(1)
    for (i in 0 until 12) g = (g + x / g) / Fx.of(2)
    return g
}

fun main() {
    // A two-pole filter over a sawtooth, in locals: Fx is an Int here.
    val a = Fx.frac(3, 2)
    val b = Fx.frac(-7, 10)
    var y1 = Fx(0)
    var y2 = Fx(0)
    var acc = 0L
    val kept = ArrayList<Fx>()
    for (i in 0 until 12_000_000) {
        val x = Fx.frac(i % 97 - 48, 64)
        val y = x + a * y1 + b * y2
        y2 = y1
        y1 = y
        acc += y.raw
        // Into a collection, where it has to be boxed.
        if (i and 63 == 0) kept.add(fxSqrt(Fx(y.raw and 0x7FFFFF)))
    }
    kept.sort()
    var med = 0L
    for (i in 0 until kept.size step 97) med += kept[i].raw
    println("acc = $acc kept = ${kept.size} med = $med")
}
