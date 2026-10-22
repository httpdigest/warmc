// Value classes: `value class` wrappers over Int, Long, Double, String, a
// reference type and a nullable type; operators and init-block validation on
// them; value classes implementing interfaces (boxed when used through the
// interface), used as generic arguments, as nullable types, in collections and
// arrays, as map keys and set members (so their generated equals/hashCode run
// on boxes), in `when`, through extension functions, and as properties of
// data classes; plus the generated toString.
//
// Kotlin/Wasm erases a value class to its underlying value wherever the static
// type is the value class itself, and boxes it into a struct whenever it
// escapes as Any, an interface, a nullable or a type argument.  This test
// moves the same values back and forth across that line and prints them at
// every step.
import kotlin.math.sqrt

interface Measure { fun describe(): String; val magnitude: Double }

value class Meters(val v: Double) : Measure, Comparable<Meters> {
    operator fun plus(o: Meters) = Meters(v + o.v)
    operator fun times(k: Double) = Meters(v * k)
    override fun compareTo(other: Meters) = v.compareTo(other.v)
    override fun describe() = "${v}m"
    override val magnitude get() = v
    fun toFeet() = Feet(v * 3.28084)
}

value class Feet(val v: Double) : Measure {
    override fun describe() = "${(v * 100).toLong() / 100.0}ft"
    override val magnitude get() = v / 3.28084
}

value class UserId(val id: Int) {
    init { require(id > 0) { "id must be positive: $id" } }
    val isAdmin get() = id < 100
    fun next() = UserId(id + 1)
}

value class Cents(val amount: Long) : Comparable<Cents> {
    operator fun plus(o: Cents) = Cents(amount + o.amount)
    operator fun minus(o: Cents) = Cents(amount - o.amount)
    operator fun times(n: Int) = Cents(amount * n)
    operator fun div(n: Int) = Cents(amount / n)
    override fun compareTo(other: Cents) = amount.compareTo(other.amount)
    override fun toString() = (if (amount < 0) "-" else "") + "$" + kotlin.math.abs(amount) / 100 + "." + (kotlin.math.abs(amount) % 100).toString().padStart(2, '0')
    companion object { fun of(d: Int, c: Int) = Cents(d * 100L + c) }
}

value class Email(val raw: String) {
    init { require('@' in raw) { "not an email: $raw" } }
    val domain get() = raw.substringAfter('@')
    val user get() = raw.substringBefore('@')
}

value class Tags(val list: List<String>) {
    operator fun plus(t: String) = Tags(list + t)
    operator fun contains(t: String) = t in list
    val size get() = list.size
}

value class MaybeName(val name: String?) {
    fun orDefault() = name ?: "anonymous"
}

value class Vec2(val packed: Long) {
    constructor(x: Int, y: Int) : this((x.toLong() shl 32) or (y.toLong() and 0xffffffffL))
    val x get() = (packed shr 32).toInt()
    val y get() = packed.toInt()
    operator fun plus(o: Vec2) = Vec2(x + o.x, y + o.y)
    fun len() = sqrt((x.toLong() * x + y.toLong() * y).toDouble())
    override fun toString() = "Vec2($x, $y)"
}

data class Order(val id: UserId, val total: Cents, val email: Email?)

fun Meters.double() = this * 2.0
fun UserId?.label() = this?.let { "user#${it.id}" } ?: "nobody"
fun <T : Comparable<T>> largest(xs: List<T>): T? = xs.maxOrNull()
fun <T> echo(x: T): T = x
fun totalMagnitude(ms: List<Measure>) = ms.sumOf { it.magnitude }

fun main() {
    println("== Meters / Feet ==")
    val a = Meters(3.5); val b = Meters(1.25)
    println("${a + b} ${a * 2.0} ${a.double()} ${a > b} ${a.compareTo(b)} ${a.describe()} ${a.toFeet().describe()} ${a.toFeet()}")
    val ms: List<Measure> = listOf(a, b, Feet(10.0), a.toFeet(), Meters(-2.0))
    println(ms.map { it.describe() } + totalMagnitude(ms).let { (it * 1000).toLong() })
    println("is ${ms[0] is Meters} ${ms[2] is Meters} ${ms[2] is Feet} as ${(ms[0] as Meters).v} ${(ms[3] as? Meters)?.v}")
    val sorted = listOf(Meters(5.0), Meters(-1.0), Meters(2.5), Meters(0.0)).sorted()
    println("$sorted largest=${largest(sorted)} echo=${echo(a)} eq=${echo(a) == a} ${Meters(1.0) == Meters(1.0)} ${Meters(0.0) == Meters(-0.0)}")
    println("hash ${a.hashCode() == Meters(3.5).hashCode()} ${a.hashCode() == a.v.hashCode()} set=${setOf(Meters(1.0), Meters(1.0), Meters(2.0)).size}")

    println("== UserId ==")
    val ids = (95..104).map { UserId(it) }
    println(ids.map { "${it.id}${if (it.isAdmin) "A" else ""}" })
    println("next ${ids.last().next()} ${ids.first().next().next().id} label ${UserId(7).label()} ${(null as UserId?).label()}")
    val bad = listOf(0, -5, 12).map { runCatching { UserId(it) }.fold({ "ok ${it.id}" }, { e -> "err ${e.message}" }) }
    println(bad)
    val byId = LinkedHashMap<UserId, String>()
    for (i in listOf(3, 1, 3, 2, 1, 3)) byId[UserId(i)] = (byId[UserId(i)] ?: "") + "x"
    println("map $byId keys=${byId.keys.map { it.id }} get=${byId[UserId(3)]} missing=${byId[UserId(9)]}")
    val nullableIds: List<UserId?> = listOf(UserId(5), null, UserId(500))
    println(nullableIds.map { it?.id } + nullableIds.map { it.label() } + nullableIds.filterNotNull().map { it.isAdmin })
    val arr = Array(4) { UserId(it + 10) }
    arr[2] = arr[2].next().next()
    println("array ${arr.map { it.id }} contains ${UserId(14) in arr} ${UserId(12) in arr}")

    println("== Cents ==")
    val prices = listOf(Cents.of(19, 99), Cents.of(5, 0), Cents(-250), Cents.of(0, 7), Cents.of(1200, 50))
    println("$prices sum=${prices.fold(Cents(0)) { x, y -> x + y }} max=${prices.maxOrNull()} min=${prices.minOrNull()}")
    var balance = Cents.of(100, 0)
    for (p in prices) { balance -= p; println("  pay $p -> $balance") }
    println("split ${Cents.of(10, 0) / 3} x3 ${Cents.of(3, 33) * 3} sorted ${prices.sortedDescending()}")
    val grouped = prices.groupBy { it > Cents(0) }
    println("grouped ${grouped[true]} ${grouped[false]}")

    println("== Email / Tags / MaybeName ==")
    for (s in listOf("ann@example.com", "bob@x.org", "no-at-sign", "c@d")) {
        val e = runCatching { Email(s) }
        println("  $s -> ${e.map { "${it.user} at ${it.domain} $it" }.getOrElse { "invalid: ${it.message}" }}")
    }
    var tags = Tags(emptyList())
    for (t in listOf("red", "green", "red", "blue")) tags += t
    println("tags $tags size=${tags.size} red=${"red" in tags} pink=${"pink" in tags} ${tags.list.distinct()}")
    val names = listOf(MaybeName("zed"), MaybeName(null), MaybeName(""))
    println(names.map { it.orDefault() } + names.map { it.name?.length } + names.map { it.toString() })
    println("eq ${MaybeName(null) == MaybeName(null)} ${MaybeName("a") == MaybeName("a")} ${MaybeName("a") == MaybeName(null)}")

    println("== Vec2 ==")
    val vs = listOf(Vec2(3, 4), Vec2(-1, -1), Vec2(Int.MAX_VALUE, Int.MIN_VALUE), Vec2(0, 0))
    for (v in vs) println("  $v packed=${v.packed.toULong().toString(16)} len=${v.len()} plus=${v + Vec2(1, -1)}")
    var walk = Vec2(0, 0)
    repeat(10) { walk += Vec2(it, -it * 2) }
    println("  walk $walk ${walk.len()}")

    println("== in data classes ==")
    val orders = listOf(Order(UserId(1), Cents.of(9, 99), Email("a@b.c")), Order(UserId(2), Cents(150), null),
        Order(UserId(1), Cents.of(9, 99), Email("a@b.c")))
    orders.forEach { println("  $it hash=${it.hashCode()}") }
    println("  eq ${orders[0] == orders[2]} ${orders[0] == orders[1]} distinct=${orders.distinct().size}")
    val (id, total, email) = orders[1]
    println("  destructured ${id.id} $total ${email?.domain}")
    println("  copy ${orders[0].copy(total = orders[0].total * 2, email = null)}")
    println("  when ${orders.map { when (it.id) { UserId(1) -> "first"; UserId(2) -> "second"; else -> "?" } }}")
}
