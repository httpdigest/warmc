// Floating point: Double and Float arithmetic and the whole of kotlin.math --
// sqrt/cbrt/pow/exp/expm1/ln/ln1p/log10/log2/log(x, b), trigonometric and
// hyperbolic functions and their inverses, hypot, atan2, floor/ceil/round/
// truncate, roundToInt/roundToLong (half-even vs half-up), sign/withSign/
// absoluteValue, ulp/nextUp/nextDown/nextTowards, IEEErem, min/max with NaN and
// signed zeros -- plus Double/Float toString on awkward values, toBits/
// fromBits, NaN and infinity comparisons, Float<->Double widening and
// narrowing, and numeric kernels (Kahan summation, Simpson's rule, Newton's
// method, a Mandelbrot grid, Horner evaluation, a 3x3 inverse).
//
// Only sqrt/floor/ceil/trunc/nearest/min/max/abs/neg/copysign are wasm
// instructions; the transcendental functions are compiled from Kotlin's own
// (fdlibm-derived) implementations into wasm, so a miscompiled f64 operation
// anywhere in them changes printed digits.  Values are printed in full.
import kotlin.math.*

fun kahanSum(xs: DoubleArray): Double {
    var sum = 0.0; var c = 0.0
    for (x in xs) { val y = x - c; val t = sum + y; c = (t - sum) - y; sum = t }
    return sum
}

fun simpson(f: (Double) -> Double, a: Double, b: Double, n: Int): Double {
    val h = (b - a) / n
    var s = f(a) + f(b)
    for (i in 1 until n) s += f(a + i * h) * if (i % 2 == 1) 4 else 2
    return s * h / 3
}

fun newtonSqrt(x: Double): Double {
    if (x == 0.0) return 0.0
    var g = x
    repeat(60) { g = 0.5 * (g + x / g) }
    return g
}

fun mandel(cx: Double, cy: Double, maxIt: Int): Int {
    var x = 0.0; var y = 0.0; var i = 0
    while (i < maxIt && x * x + y * y <= 4.0) { val t = x * x - y * y + cx; y = 2 * x * y + cy; x = t; i++ }
    return i
}

fun horner(coef: DoubleArray, x: Double): Double { var r = 0.0; for (i in coef.indices.reversed()) r = r * x + coef[i]; return r }

fun inverse3(m: Array<DoubleArray>): Array<DoubleArray>? {
    val (a, b, c) = m[0].let { Triple(it[0], it[1], it[2]) }
    val (d, e, f) = m[1].let { Triple(it[0], it[1], it[2]) }
    val (g, h, i) = m[2].let { Triple(it[0], it[1], it[2]) }
    val det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if (abs(det) < 1e-12) return null
    return arrayOf(
        doubleArrayOf((e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det),
        doubleArrayOf((f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det),
        doubleArrayOf((d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det))
}

fun hexBits(d: Double) = d.toRawBits().toULong().toString(16).padStart(16, '0')

fun main() {
    println("== toString ==")
    val ds = listOf(0.0, -0.0, 1.0, -1.5, 0.1, 0.1 + 0.2, 1.0 / 3, 2.0 / 3, 100.0, 1e7, 1.0e-7, 123456789.0, 1e21, 1e22, 1.7976931348623157e308,
        4.9e-324, 2.2250738585072014e-308, PI, E, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, 9007199254740993.0, 0.000123, 5e-5, 12345.678)
    for (d in ds) println("  $d bits=${hexBits(d)} float=${d.toFloat()}")
    val fs = listOf(0.1f, 1.0f / 3, 16777217f, 3.4028235e38f, 1.4e-45f, -2.5f, 1e10f, 0.3f + 0.6f)
    for (f in fs) println("  f $f bits=${f.toRawBits().toUInt().toString(16)} double=${f.toDouble()}")
    println("  fromBits ${Double.fromBits(0x400921FB54442D18L)} ${Float.fromBits(0x3f800001)} ${Double.fromBits(0x7ff0000000000001L).isNaN()}")

    println("== comparisons ==")
    val nan = Double.NaN
    println("  nan==nan ${nan == nan} nan!=nan ${nan != nan} nan<1 ${nan < 1.0} nan>1 ${nan > 1.0} equals ${nan.equals(nan)} compareTo ${nan.compareTo(nan)} ${nan.compareTo(Double.POSITIVE_INFINITY)}")
    println("  0==-0 ${0.0 == -0.0} equals ${0.0.equals(-0.0)} compareTo ${0.0.compareTo(-0.0)} ${(-0.0).compareTo(0.0)} min ${min(0.0, -0.0)} max ${max(-0.0, 0.0)} 1/-0 ${1.0 / -0.0}")
    println("  min/max NaN ${min(1.0, nan)} ${max(nan, 1.0)} ${minOf(2.0, 1.0)} coerce ${5.5.coerceIn(0.0, 1.0)} ${(-0.5).coerceAtLeast(0.0)}")
    val withNaN = listOf(3.0, nan, -1.0, 0.0, -0.0, Double.NEGATIVE_INFINITY)
    println("  sorted ${withNaN.sorted()} max ${withNaN.maxOrNull()} min ${withNaN.minOrNull()}")
    println("  inf ${Double.POSITIVE_INFINITY - Double.POSITIVE_INFINITY} ${Double.POSITIVE_INFINITY * 0} ${1e308 * 10} ${-1e308 * 10} ${1.0 / 0} ${0.0 / 0} isFinite ${1e308.isFinite()} ${(1e308 * 10).isInfinite()}")

    println("== kotlin.math ==")
    val xs = listOf(0.0, 0.5, 1.0, 2.0, 3.0, 10.0, 0.001, 123.456, 1e-10, 700.0)
    for (x in xs) {
        println("  x=$x sqrt=${sqrt(x)} cbrt=${cbrt(x)} exp=${exp(x)} expm1=${expm1(x)} ln=${ln(x)} ln1p=${ln1p(x)} log10=${log10(x)} log2=${log2(x)}")
    }
    val angles = listOf(0.0, PI / 6, PI / 4, PI / 3, PI / 2, PI, 3 * PI / 2, 2 * PI, -1.0, 100.0, 1e6)
    for (a in angles) println("  a=$a sin=${sin(a)} cos=${cos(a)} tan=${tan(a)}")
    for (v in listOf(-1.0, -0.5, 0.0, 0.5, 1.0, 1.5)) {
        println("  v=$v asin=${asin(v)} acos=${acos(v)} atan=${atan(v)} sinh=${sinh(v)} cosh=${cosh(v)} tanh=${tanh(v)} asinh=${asinh(v)} acosh=${acosh(v + 1)} atanh=${atanh(v / 2)}")
    }
    for ((y, x) in listOf(1.0 to 1.0, 1.0 to -1.0, -1.0 to -1.0, 0.0 to -1.0, -0.0 to -1.0, 0.0 to 0.0, 3.0 to 4.0)) {
        println("  atan2($y,$x)=${atan2(y, x)} hypot=${hypot(y, x)} pow=${x.pow(y)} pow2=${y.pow(x + 0.5)}")
    }
    println("  pow ${2.0.pow(10)} ${2.0.pow(0.5)} ${(-8.0).pow(1.0 / 3)} ${0.0.pow(0)} ${nan.pow(0)} ${1.0.pow(nan)} ${10.0.pow(-3)} ${2.0.pow(1023)} ${2.0.pow(1024)}")
    println("  log ${log(8.0, 2.0)} ${log(1000.0, 10.0)} ${log(-1.0, 2.0)} ${ln(0.0)} ${sqrt(-1.0)} ${ln(E)} ${exp(1.0)} ${exp(-745.0)} ${exp(710.0)}")

    println("== rounding ==")
    for (v in listOf(0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 2.4999, 2.5001, -0.0, 1e15 + 0.5, 4.5e15, 1e300, -7.7)) {
        println("  $v floor=${floor(v)} ceil=${ceil(v)} round=${round(v)} trunc=${truncate(v)} roundToLong=${v.roundToLong()} roundToInt=${v.roundToInt()} sign=${sign(v)} ulp=${v.ulp}")
    }
    println("  roundToInt NaN ${try { nan.roundToInt() } catch (e: IllegalArgumentException) { "IAE" }} inf ${Double.POSITIVE_INFINITY.roundToInt()} ${(-1e20).roundToLong()}")
    println("  next ${1.0.nextUp()} ${1.0.nextDown()} ${0.0.nextUp()} ${1.0.nextTowards(0.0)} withSign ${3.0.withSign(-0.0)} ${(-3.0).withSign(1)} abs ${(-0.0).absoluteValue} ${abs(-2.5)}")
    println("  rem ${10.0.rem(3.0)} ${11.0.rem(-3.0)} ${(-10.0).rem(3.0)} % ${10.0 % 3.0} ${-10.0 % 3.0} ${10.5 % -3.0} ${5.0 % 0.0}")
    println("  float math ${sqrt(2f)} ${sin(1f)} ${exp(1f)} ${2f.pow(0.5f)} ${round(2.5f)} ${1.5f.roundToInt()} ${(0.1f + 0.2f)} ${0.1f * 3}")

    println("== conversions ==")
    for (l in listOf(1L shl 53, (1L shl 53) + 1, Long.MAX_VALUE, Long.MIN_VALUE, 123456789123456789L)) println("  $l toDouble=${l.toDouble()} back=${l.toDouble().toLong()} toFloat=${l.toFloat()}")
    for (d in listOf(1.1, 3.4e38, 3.5e38, 1e-46, -1e-40, 0.1, 16777217.0)) println("  $d toFloat=${d.toFloat()} back=${d.toFloat().toDouble()}")
    println("  int ops ${7 / 2.0} ${7.0 / 2} ${(7 / 2).toDouble()} ${1 / 3.0f} ${Int.MAX_VALUE.toFloat()} ${Int.MAX_VALUE.toDouble()} ${(Int.MAX_VALUE.toFloat()).toInt()}")

    println("== kernels ==")
    val many = DoubleArray(100000) { if (it % 2 == 0) 0.1 else 1e-9 }
    var naive = 0.0; for (x in many) naive += x
    println("  naive=$naive kahan=${kahanSum(many)}")
    println("  simpson sin 0..pi=${simpson(::sin, 0.0, PI, 100)} x^2 0..3=${simpson({ it * it }, 0.0, 3.0, 10)} exp=${simpson(::exp, 0.0, 1.0, 50)} gauss=${simpson({ exp(-it * it) }, -5.0, 5.0, 200)}")
    println("  newton ${listOf(2.0, 9.0, 1e-8, 1e20, 0.0).map { newtonSqrt(it) }} vs ${listOf(2.0, 9.0, 1e-8, 1e20, 0.0).map { sqrt(it) }}")
    for (row in 0 until 11) {
        val sb = StringBuilder("  ")
        for (col in 0 until 40) {
            val n = mandel(-2.2 + col * 0.075, -1.1 + row * 0.22, 50)
            sb.append(" .:-=+*#%@"[min(9, n / 5)])
        }
        println(sb)
    }
    val coef = doubleArrayOf(1.0, -3.0, 0.5, 2.0, -0.25)
    println("  horner ${(-2..2).map { horner(coef, it * 0.75) }}")
    val m = arrayOf(doubleArrayOf(2.0, -1.0, 0.0), doubleArrayOf(-1.0, 2.0, -1.0), doubleArrayOf(0.0, -1.0, 2.0))
    println("  inverse ${inverse3(m)?.map { r -> r.toList() }} singular ${inverse3(arrayOf(doubleArrayOf(1.0, 2.0, 3.0), doubleArrayOf(2.0, 4.0, 6.0), doubleArrayOf(0.0, 1.0, 1.0)))}")
    var x = 0.5
    val orbit = mutableListOf<Double>()
    repeat(40) { x = 3.9 * x * (1 - x); if (it % 8 == 7) orbit.add(x) }
    println("  logistic $orbit")
    val stats = (1..1000).map { sin(it.toDouble()) * 100 }
    val mean = stats.average()
    val sd = sqrt(stats.sumOf { (it - mean).pow(2) } / stats.size)
    println("  stats mean=$mean sd=$sd max=${stats.maxOrNull()} min=${stats.minOrNull()}")
}
