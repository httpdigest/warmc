// Operator overloading: arithmetic (plus/minus/times/div/rem), unary
// (unaryMinus/unaryPlus/not/inc/dec), compound assignment (plusAssign on a
// mutable type, and `+=` on a `var` of an immutable type), indexed get/set
// with one and two indices, invoke, compareTo (so <, <=, >, >= and sorting),
// equals/hashCode, rangeTo and rangeUntil returning a custom iterable range,
// contains (`in`/`!in`), componentN for destructuring, and iterator().
//
// Each operator is an ordinary (often virtual) call on a GC struct, and most
// return a freshly allocated value, so the rational and matrix arithmetic
// below allocates on every step and every intermediate value is printed.
data class Vec(val x: Int, val y: Int) {
    operator fun plus(o: Vec) = Vec(x + o.x, y + o.y)
    operator fun minus(o: Vec) = Vec(x - o.x, y - o.y)
    operator fun times(k: Int) = Vec(x * k, y * k)
    operator fun times(o: Vec) = x * o.x + y * o.y  // dot product
    operator fun div(k: Int) = Vec(x / k, y / k)
    operator fun rem(k: Int) = Vec(x % k, y % k)
    operator fun unaryMinus() = Vec(-x, -y)
    operator fun unaryPlus() = Vec(kotlin.math.abs(x), kotlin.math.abs(y))
    operator fun not() = Vec(y, x)
    operator fun inc() = Vec(x + 1, y + 1)
    operator fun dec() = Vec(x - 1, y - 1)
    operator fun get(i: Int) = when (i) { 0 -> x; 1 -> y; else -> throw IndexOutOfBoundsException("Vec[$i]") }
    operator fun rangeTo(o: Vec) = VecRange(this, o)
}
operator fun Int.times(v: Vec) = v * this

class VecRange(val start: Vec, val end: Vec) : Iterable<Vec> {
    override fun iterator() = object : Iterator<Vec> {
        var cur = start
        override fun hasNext() = cur.x <= end.x && cur.y <= end.y
        override fun next(): Vec { val r = cur; cur = Vec(cur.x + 1, cur.y + if (cur.x % 2 == 0) 1 else 0); return r }
    }
    operator fun contains(v: Vec) = v.x in start.x..end.x && v.y in start.y..end.y
}

class Rational(n: Long, d: Long) : Comparable<Rational> {
    val num: Long
    val den: Long
    init {
        require(d != 0L) { "zero denominator" }
        val g = gcd(kotlin.math.abs(n), kotlin.math.abs(d)).coerceAtLeast(1)
        val s = if (d < 0) -1 else 1
        num = s * n / g; den = s * d / g
    }
    operator fun plus(o: Rational) = Rational(num * o.den + o.num * den, den * o.den)
    operator fun minus(o: Rational) = Rational(num * o.den - o.num * den, den * o.den)
    operator fun times(o: Rational) = Rational(num * o.num, den * o.den)
    operator fun div(o: Rational) = Rational(num * o.den, den * o.num)
    operator fun unaryMinus() = Rational(-num, den)
    operator fun plus(i: Int) = this + Rational(i.toLong(), 1)
    override fun compareTo(other: Rational) = (num * other.den).compareTo(other.num * den)
    override fun equals(other: Any?) = other is Rational && num == other.num && den == other.den
    override fun hashCode() = (num * 31 + den).hashCode()
    override fun toString() = if (den == 1L) "$num" else "$num/$den"
    operator fun component1() = num
    operator fun component2() = den
    companion object { fun gcd(a: Long, b: Long): Long = if (b == 0L) a else gcd(b, a % b) }
}
infix fun Int.over(d: Int) = Rational(toLong(), d.toLong())

class Matrix(val rows: Int, val cols: Int) {
    private val a = LongArray(rows * cols)
    operator fun get(i: Int, j: Int) = a[i * cols + j]
    operator fun set(i: Int, j: Int, v: Long) { a[i * cols + j] = v }
    operator fun times(o: Matrix): Matrix {
        val r = Matrix(rows, o.cols)
        for (i in 0 until rows) for (j in 0 until o.cols) {
            var s = 0L
            for (k in 0 until cols) s += this[i, k] * o[k, j]
            r[i, j] = s
        }
        return r
    }
    operator fun plus(o: Matrix): Matrix { val r = Matrix(rows, cols); for (i in a.indices) r.a[i] = a[i] + o.a[i]; return r }
    operator fun invoke(f: (Long) -> Long): Matrix { val r = Matrix(rows, cols); for (i in a.indices) r.a[i] = f(a[i]); return r }
    override fun equals(other: Any?) = other is Matrix && rows == other.rows && cols == other.cols && a.contentEquals(other.a)
    override fun hashCode() = a.contentHashCode()
    override fun toString() = (0 until rows).joinToString("; ", "[", "]") { i -> (0 until cols).joinToString(" ") { j -> this[i, j].toString() } }
    companion object {
        fun identity(n: Int) = Matrix(n, n).also { for (i in 0 until n) it[i, i] = 1 }
        fun of(rows: Int, vararg v: Long) = Matrix(rows, v.size / rows).also { m -> v.forEachIndexed { i, x -> m.a[i] = x } }
    }
}

fun Matrix.pow(e: Int): Matrix {
    var result = Matrix.identity(rows); var base = this; var n = e
    while (n > 0) { if (n and 1 == 1) result = result * base; base = base * base; n = n shr 1 }
    return result
}

class Bag<T> : Iterable<T> {
    private val counts = LinkedHashMap<T, Int>()
    operator fun plusAssign(x: T) { counts[x] = (counts[x] ?: 0) + 1 }
    operator fun minusAssign(x: T) { val c = counts[x] ?: return; if (c == 1) counts.remove(x) else counts[x] = c - 1 }
    operator fun get(x: T) = counts[x] ?: 0
    operator fun contains(x: T) = x in counts
    override operator fun iterator(): Iterator<T> = counts.flatMap { (k, v) -> List(v) { k } }.iterator()
    override fun toString() = counts.toString()
}

class Polynomial(vararg c: Int) {
    val coef = c.toList().dropLastWhile { it == 0 }
    operator fun invoke(x: Long): Long = coef.foldRight(0L) { c, acc -> acc * x + c }
    operator fun plus(o: Polynomial) = Polynomial(*IntArray(maxOf(coef.size, o.coef.size)) { coef.getOrElse(it) { 0 } + o.coef.getOrElse(it) { 0 } })
    operator fun times(o: Polynomial): Polynomial {
        if (coef.isEmpty() || o.coef.isEmpty()) return Polynomial()
        val r = IntArray(coef.size + o.coef.size - 1)
        for (i in coef.indices) for (j in o.coef.indices) r[i + j] += coef[i] * o.coef[j]
        return Polynomial(*r)
    }
    override fun toString() = if (coef.isEmpty()) "0" else coef.withIndex().reversed().filter { it.value != 0 }
        .joinToString(" + ") { (i, c) -> when (i) { 0 -> "$c"; 1 -> "${c}x"; else -> "${c}x^$i" } }
}

class Version(val parts: List<Int>) : Comparable<Version> {
    constructor(s: String) : this(s.split(".").map { it.toInt() })
    override fun compareTo(other: Version): Int {
        for (i in 0 until maxOf(parts.size, other.parts.size)) {
            val c = parts.getOrElse(i) { 0 }.compareTo(other.parts.getOrElse(i) { 0 })
            if (c != 0) return c
        }
        return 0
    }
    override fun toString() = parts.joinToString(".")
}

fun main() {
    println("== Vec ==")
    var v = Vec(3, -4)
    val w = Vec(1, 2)
    println("${v + w} ${v - w} ${v * 3} ${3 * v} ${v * w} ${v / 2} ${v % 2} ${-v} ${+v} ${!v}")
    println("${v[0]} ${v[1]} ${try { v[2] } catch (e: IndexOutOfBoundsException) { e.message }}")
    v++; println("inc $v"); ++v; println("preinc $v"); v--; println("dec $v")
    val before = v++
    println("post: before=$before after=$v")
    v += w; println("plusAssign(var) $v")
    v *= 2; println("timesAssign(var) $v")
    val range = Vec(0, 0)..Vec(5, 3)
    println("range ${range.toList()}")
    println("in ${Vec(2, 2) in range} ${Vec(6, 0) in range} ${Vec(1, 4) !in range}")
    val (vx, vy) = Vec(8, 9)
    println("destructure $vx $vy")

    println("== Rational ==")
    val h = 1 over 2; val t = 1 over 3; val q = Rational(-6, -8)
    println("$h $t $q ${h + t} ${h - t} ${h * t} ${h / t} ${-q} ${q + 2} ${Rational(10, -4)}")
    var harmonic = Rational(0, 1)
    for (i in 1..20) { harmonic += Rational(1, i.toLong()); if (i % 4 == 0 || i < 5) println("  H($i) = $harmonic") }
    println("cmp ${h > t} ${h < t} ${h >= Rational(2, 4)} ${h == Rational(3, 6)} ${h.compareTo(t)}")
    val rs = listOf(3 over 4, 1 over 8, -1 over 2, 5 over 6, 2 over 3, 0 over 9)
    println("sorted ${rs.sorted()} max ${rs.maxOrNull()} sum ${rs.reduce { a, b -> a + b }}")
    val (n, d) = 22 over 7
    println("components $n $d set=${setOf(h, Rational(2, 4), t, Rational(-1, -3)).size}")
    var x = Rational(1, 1)
    repeat(6) { x = (x + Rational(2, 1) / x) / Rational(2, 1); println("  sqrt2 ~ $x") }
    try { Rational(1, 0) } catch (e: IllegalArgumentException) { println("caught ${e.message}") }

    println("== Matrix ==")
    val fib = Matrix.of(2, 1, 1, 1, 0)
    for (e in listOf(1, 2, 5, 10, 30, 60, 90)) println("  fib^$e = ${fib.pow(e)}")
    val m = Matrix.of(3, 1, 2, 3, 4, 5, 6, 7, 8, 9)
    println("  m=$m")
    println("  m*m=${m * m}")
    println("  m+I=${m + Matrix.identity(3)}")
    println("  m(sq)=${m { it * it }} neg=${m { -it }}")
    val mt = Matrix.of(3, 1, 2, 3, 4, 5, 6)
    val tm = Matrix.of(2, 7, 8, 9, 10, 11, 12)
    println("  3x2*2x3=${mt * tm} 2x3*3x2=${tm * mt}")
    m[1, 1] = 50
    println("  set=$m get=${m[1, 1]} eq=${m == Matrix.of(3, 1, 2, 3, 4, 50, 6, 7, 8, 9)} ${m == Matrix.identity(3)}")

    println("== Bag ==")
    val bag = Bag<String>()
    for (wd in "a b c a b a d e a".split(" ")) bag += wd
    println("  $bag a=${bag["a"]} z=${bag["z"]} ${"d" in bag} ${"z" in bag}")
    bag -= "a"; bag -= "d"; bag -= "zz"
    println("  $bag list=${bag.toList()} count=${bag.count()}")

    println("== Polynomial ==")
    val p = Polynomial(1, 2, 3)      // 3x^2 + 2x + 1
    val p2 = Polynomial(-1, 0, 0, 1) // x^3 - 1
    println("  p=$p p2=$p2 p+p2=${p + p2} p*p2=${p * p2} p*p=${p * p}")
    println("  values ${(-3L..3L).map { p(it) }} ${(-3L..3L).map { (p * p2)(it) }}")
    println("  zero=${Polynomial(0, 0)} ${Polynomial() * p}")

    println("== Version ==")
    val vs = listOf("1.2.10", "1.2.9", "1.10", "1.2", "1.2.0.0", "0.99.1", "2").map(::Version)
    println("  ${vs.sorted()} max=${vs.maxOrNull()}")
    println("  ${Version("1.2") == Version("1.2.0")} ${Version("1.2").compareTo(Version("1.2.0"))} ${Version("3.1") > Version("3.0.9")}")
    println("  ${vs.zipWithNext { a, b -> if (a < b) '<' else if (a > b) '>' else '=' }.joinToString("")}")
}
