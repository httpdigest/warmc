// Extension functions and properties: on primitives, String, List, nullable
// receivers and generic receivers; extension properties with getters (and a
// setter backed by the receiver's state); extensions declared as class members
// (dispatch receiver plus extension receiver, overridden virtually); static
// resolution (the declared type picks the extension, not the runtime type);
// members shadowing extensions; infix functions; extensions on companion
// objects; and chains of scope functions.
//
// An extension is a static function whose first parameter is the receiver, so
// these exercise ordinary calls with an extra leading argument -- except the
// member extensions, which are virtual in the dispatch receiver.
import kotlin.math.sqrt

fun Int.isPrime(): Boolean {
    if (this < 2) return false
    var d = 2
    while (d * d <= this) { if (this % d == 0) return false; d++ }
    return true
}
fun Int.digits(): List<Int> = if (this == 0) listOf(0) else generateSequence(kotlin.math.abs(this)) { it / 10 }.takeWhile { it > 0 }.map { it % 10 }.toList().reversed()
val Int.squared get() = this * this
val Int.isEven: Boolean get() = this % 2 == 0
fun Long.collatzSteps(): Int { var n = this; var s = 0; while (n != 1L) { n = if (n % 2 == 0L) n / 2 else 3 * n + 1; s++ }; return s }
val Double.half get() = this / 2

fun String.isPalindrome(): Boolean { val t = filter { it.isLetterOrDigit() }.lowercase(); return t == t.reversed() }
fun String.vowels() = count { it.lowercaseChar() in "aeiou" }
fun String.caesar(k: Int) = map { c ->
    when (c) {
        in 'a'..'z' -> 'a' + (c - 'a' + k).mod(26)
        in 'A'..'Z' -> 'A' + (c - 'A' + k).mod(26)
        else -> c
    }
}.joinToString("")
val String.initials get() = split(" ").filter { it.isNotEmpty() }.joinToString("") { it.first().uppercase() }
fun String.wordFreq(): List<Pair<String, Int>> = lowercase().split(Regex("[^a-z]+")).filter { it.isNotEmpty() }
    .groupingBy { it }.eachCount().toList().sortedWith(compareByDescending<Pair<String, Int>> { it.second }.thenBy { it.first })

fun String?.orDash(): String = if (this.isNullOrEmpty()) "-" else this
fun Any?.describe(): String = when (this) { null -> "null!"; is Int -> "int $this"; else -> "thing ${toString()}" }

fun <T> List<T>.second(): T? = if (size >= 2) this[1] else null
fun <T> List<T>.rotate(k: Int): List<T> = if (isEmpty()) this else { val s = k.mod(size); drop(s) + take(s) }
fun <T : Comparable<T>> List<T>.isSorted() = zipWithNext().all { (a, b) -> a <= b }
val <T> List<T>.middle: T? get() = if (isEmpty()) null else this[size / 2]
fun List<Double>.mean() = if (isEmpty()) 0.0 else sum() / size
fun List<Double>.stddev(): Double { val m = mean(); return sqrt(map { (it - m) * (it - m) }.mean()) }
fun <K, V : Comparable<V>> Map<K, V>.argMax(): K? = entries.maxByOrNull { it.value }?.key
fun IntArray.prefixSums(): IntArray { val r = IntArray(size); var s = 0; for (i in indices) { s += this[i]; r[i] = s }; return r }

infix fun Int.pow(e: Int): Long { var r = 1L; repeat(e) { r *= this }; return r }
infix fun <A, B> A.with2(b: B) = "$this&$b"
infix fun String.times(n: Int) = repeat(n)

class Temperature(var celsius: Double)
var Temperature.fahrenheit: Double
    get() = celsius * 9 / 5 + 32
    set(v) { celsius = (v - 32) * 5 / 9 }

open class Shape { open fun name() = "shape" }
class Circle : Shape() { override fun name() = "circle" }
fun Shape.kind() = "ext-shape"
fun Circle.kind() = "ext-circle"

class Widget { fun label() = "member"; fun size() = 1 }
fun Widget.label() = "extension"          // shadowed by the member
fun Widget.label(prefix: String) = "$prefix-extension"  // overload, used
fun Widget.size(scale: Int) = size() * scale

open class Formatter(val tag: String) {
    open fun Int.fmt() = "$tag:$this"
    open fun String.fmt() = "$tag'$this'"
    fun render(xs: List<Any>) = xs.joinToString(" ") { if (it is Int) it.fmt() else it.toString().fmt() }
}
class Loud(tag: String) : Formatter(tag) {
    override fun Int.fmt() = "$tag!${this * 10}"
}

class Money(val cents: Long) { companion object; override fun toString() = "$" + cents / 100 + "." + (cents % 100).toString().padStart(2, '0') }
fun Money.Companion.of(d: Int, c: Int) = Money(d * 100L + c)
operator fun Money.plus(o: Money) = Money(cents + o.cents)
fun Iterable<Money>.total() = fold(Money(0)) { a, b -> a + b }

fun main() {
    println("== Int / Long / Double ==")
    for (n in listOf(0, 1, 2, 17, 21, 97, 100, 7919, -7, 1234567)) {
        println("  $n prime=${n.isPrime()} digits=${n.digits()} sq=${n.squared} even=${n.isEven}")
    }
    println("  primes<60 ${(1..60).filter(Int::isPrime)}")
    println("  collatz ${(1L..12L).map { it.collatzSteps() }} 27->${27L.collatzSteps()} ${837799L.collatzSteps()}")
    println("  half ${7.0.half} ${(-3.0).half} pow ${2 pow 10} ${3 pow 30} ${(-2) pow 63}")

    println("== String ==")
    for (s in listOf("A man, a plan, a canal: Panama", "hello", "Was it a car or a cat I saw?", "", "ab")) {
        println("  '$s' pal=${s.isPalindrome()} vowels=${s.vowels()} initials=${s.initials} rot13=${s.caesar(13)} back=${s.caesar(13).caesar(13) == s}")
    }
    println("  " + "the cat and the dog and THE bird; a cat!".wordFreq())
    println("  ${"ab" times 3} ${1 with2 "x"} ${"k" with2 listOf(1)}")
    val ns: List<String?> = listOf("x", null, "")
    println("  ${ns.map { it.orDash() }} ${listOf(1, null, "s").map { it.describe() }}")

    println("== List ==")
    val xs = listOf(5, 3, 9, 1, 7)
    println("  second=${xs.second()} ${emptyList<Int>().second()} middle=${xs.middle} sorted=${xs.isSorted()} ${xs.sorted().isSorted()}")
    for (k in -6..6 step 3) println("  rotate $k ${xs.rotate(k)}")
    val ds = listOf(2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0)
    println("  mean=${ds.mean()} sd=${ds.stddev()} empty=${emptyList<Double>().mean()}")
    println("  argMax=${mapOf("a" to 3, "b" to 9, "c" to 1).argMax()} ${emptyMap<String, Int>().argMax()}")
    println("  prefix=${intArrayOf(1, 2, 3, 4, 5, -15).prefixSums().toList()}")

    println("== properties ==")
    val t = Temperature(100.0)
    println("  ${t.celsius} ${t.fahrenheit}")
    t.fahrenheit = -40.0
    println("  ${t.celsius} ${t.fahrenheit}")
    t.fahrenheit += 18.0
    println("  ${t.celsius} ${t.fahrenheit}")

    println("== static resolution ==")
    val c = Circle()
    val s: Shape = c
    println("  ${c.kind()} ${s.kind()} ${c.name()} ${s.name()}")
    val shapes = listOf(Shape(), Circle())
    println("  ${shapes.map { it.kind() }} ${shapes.map { it.name() }}")
    val w = Widget()
    println("  ${w.label()} ${w.label("pre")} ${w.size(5)}")

    println("== member extensions ==")
    val fm = Formatter("f")
    val ld = Loud("L")
    val items = listOf<Any>(1, "two", 3, 4.5)
    println("  ${fm.render(items)}")
    println("  ${ld.render(items)}")
    with(ld) { println("  ${42.fmt()} ${"s".fmt()}") }

    println("== companion extension ==")
    val wallet = listOf(Money.of(1, 99), Money.of(0, 1), Money(12345), Money.of(10, 50))
    println("  $wallet total=${wallet.total()}")

    println("== scope chains ==")
    val result = mutableListOf<Int>().apply { for (i in 1..10) add(i * i) }
        .filter { it.isEven }
        .also { println("  evens squared $it") }
        .let { it.sum() to it.size }
        .run { "sum=$first n=$second" }
    println("  $result")
    val sb = StringBuilder().apply { append("x"); append(1) }.also { it.append('!') }
    println("  $sb len=${sb.length}")
}
