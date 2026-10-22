// Data classes: the generated equals, hashCode, toString, copy and componentN,
// over fields of every primitive width, nullable fields, generic fields, nested
// data classes and collections of them; destructuring in declarations, loops
// and lambda parameters; data classes as HashMap/HashSet keys; Pair/Triple.
//
// The generated hashCode folds each field's own hashCode with *31, so printing
// it checks Byte/Short/Char/Float/Double/Long/Boolean hashing and the i32
// wrap-around of the fold, all done in wasm code.  toString walks every field
// through its own toString, including Float and Double formatting.
import kotlin.math.abs

data class Point(val x: Int, val y: Int) {
    operator fun plus(o: Point) = Point(x + o.x, y + o.y)
    fun manhattan() = abs(x) + abs(y)
    var visits = 0  // not part of equals/hashCode/toString
}

data class Person(val name: String, val age: Int, val email: String? = null,
                  val tags: List<String> = emptyList())

data class Box<T>(val value: T, val count: Long)

data class Mixed(val b: Byte, val s: Short, val c: Char, val f: Float, val d: Double,
                 val l: Long, val z: Boolean, val u: UInt, val n: Int?)

data class Tree(val id: Int, val kids: List<Tree> = emptyList()) {
    fun size(): Int = 1 + kids.sumOf { it.size() }
    fun depth(): Int = 1 + (kids.maxOfOrNull { it.depth() } ?: 0)
}

data class Line(val from: Point, val to: Point) {
    val length2 get() = (to.x - from.x) * (to.x - from.x) + (to.y - from.y) * (to.y - from.y)
}

data class Version(val major: Int, val minor: Int, val patch: Int) : Comparable<Version> {
    override fun compareTo(other: Version) =
        compareValuesBy(this, other, { it.major }, { it.minor }, { it.patch })
    override fun toString() = "v$major.$minor.$patch"   // user override wins
}

data class Rgb(val r: Int, val g: Int, val b: Int) {
    init { require(r in 0..255 && g in 0..255 && b in 0..255) { "bad colour $r,$g,$b" } }
    fun mix(o: Rgb) = Rgb((r + o.r) / 2, (g + o.g) / 2, (b + o.b) / 2)
}

fun stats(xs: List<Int>): Triple<Int, Int, Double> =
    Triple(xs.minOrNull() ?: 0, xs.maxOrNull() ?: 0, if (xs.isEmpty()) 0.0 else xs.sum().toDouble() / xs.size)

fun divmod(a: Int, b: Int) = Pair(a / b, a % b)

fun main() {
    println("== Point ==")
    val p = Point(3, -4)
    val q = p.copy(y = 4)
    println("$p $q eq=${p == q} hash=${p.hashCode()} ${q.hashCode()}")
    println("plus=${p + q} manhattan=${(p + q).manhattan()}")
    val (px, py) = p
    println("destructured $px $py comp=${p.component1()},${p.component2()}")
    p.visits = 9
    val p2 = Point(3, -4)
    println("visits ignored: eq=${p == p2} hash=${p.hashCode() == p2.hashCode()} str=$p2")
    for (x in -2..2) for (y in -1..1) {
        val pt = Point(x, y)
        println("  $pt hash=${pt.hashCode()} m=${pt.manhattan()}")
    }

    println("== Person ==")
    val people = listOf(
        Person("Ada", 36, "ada@x.org", listOf("math", "engines")),
        Person("Bob", 25),
        Person("Cy", 25, null, listOf("x")),
        Person("Ada", 36, "ada@x.org", listOf("math", "engines")),
        Person("Dee", 41, "dee@y.com"),
    )
    for (pp in people) println("  $pp hash=${pp.hashCode()}")
    println("0==3 ${people[0] == people[3]} 0===3 ${people[0] === people[3]}")
    println("distinct=${people.distinct().size} set=${people.toSet().size}")
    val older = people.map { it.copy(age = it.age + 1, tags = it.tags + "older") }
    older.forEach { println("  older $it") }
    for ((name, age, email) in people) println("  name=$name age=$age email=${email ?: "-"}")
    val byAge = people.groupBy { it.age }
    for (k in byAge.keys.sorted()) println("  age $k -> ${byAge.getValue(k).map { it.name }}")
    println(people.sortedWith(compareBy<Person> { it.age }.thenByDescending { it.name }).map { it.name })

    println("== Mixed ==")
    val mixes = listOf(
        Mixed(1, 2, 'a', 1.5f, 2.25, 3L, true, 4u, 5),
        Mixed(-128, -32768, '\u0000', -0.0f, -0.0, Long.MIN_VALUE, false, UInt.MAX_VALUE, null),
        Mixed(127, 32767, '￿', Float.MAX_VALUE, Double.MIN_VALUE, Long.MAX_VALUE, true, 0u, Int.MIN_VALUE),
        Mixed(0, 0, 'Z', Float.NaN, Double.NEGATIVE_INFINITY, 1L shl 40, false, 1u shl 31, 0),
        Mixed(0, 0, 'Z', Float.NaN, Double.NEGATIVE_INFINITY, 1L shl 40, false, 1u shl 31, 0),
    )
    for (mx in mixes) println("  $mx\n    hash=${mx.hashCode()}")
    println("nan eq: ${mixes[3] == mixes[4]} self ${mixes[3] == mixes[3]}")
    println("0.0 vs -0.0: ${Mixed(0, 0, 'a', 0.0f, 0.0, 0, true, 0u, 0) == Mixed(0, 0, 'a', -0.0f, 0.0, 0, true, 0u, 0)}")
    val (b, s, c, f, d, l, z, u, n) = mixes[0]
    println("components $b $s $c $f $d $l $z $u $n")

    println("== generic Box ==")
    val boxes = listOf(Box("s", 1), Box(42, 2), Box(Point(1, 1), 3), Box(listOf(1, 2), 4), Box<Any?>(null, 5), Box(Box('x', 6), 7))
    for (bx in boxes) println("  $bx hash=${bx.hashCode()}")
    println("box eq ${Box(listOf(1, 2), 4) == boxes[3]} ${Box(2, 4) == Box(2L, 4)}")

    println("== Tree ==")
    fun build(id: Int, depth: Int): Tree =
        if (depth == 0) Tree(id) else Tree(id, (1..(depth % 3 + 1)).map { build(id * 10 + it, depth - 1) })
    val t = build(1, 4)
    println("size=${t.size()} depth=${t.depth()} hash=${t.hashCode()}")
    println(build(2, 2))
    println("rebuild equal: ${build(1, 4) == t} different: ${build(1, 3) == t}")

    println("== Line / Version / Rgb ==")
    val lines = (0 until 6).map { Line(Point(it, it * 2), Point(it * it, 7 - it)) }
    lines.forEach { println("  $it len2=${it.length2}") }
    val (from, to) = lines[4]
    val (fx, fy) = from
    println("nested destructure $fx $fy ${to.x}")
    val versions = listOf(Version(1, 2, 3), Version(1, 10, 0), Version(0, 9, 9), Version(1, 2, 10), Version(1, 2, 3))
    println(versions.sorted())
    println("max=${versions.maxOrNull()} cmp=${Version(1, 2, 3).compareTo(Version(1, 2, 4))} eq=${versions[0] == versions[4]}")
    println("hash v=${versions[0].hashCode()}")
    val red = Rgb(255, 0, 0); val blue = Rgb(0, 0, 255)
    println("mix=${red.mix(blue)} ${red.mix(blue).mix(Rgb(0, 255, 0))}")
    try { Rgb(256, 0, 0) } catch (e: IllegalArgumentException) { println("caught ${e.message}") }

    println("== Pair / Triple ==")
    for (a in listOf(17, -17, 100, 7)) for (bb in listOf(5, -5, 3)) {
        val (qq, rr) = divmod(a, bb)
        println("  divmod($a,$bb)=${divmod(a, bb)} q=$qq r=$rr")
    }
    println(stats(listOf(4, 8, 15, 16, 23, 42)))
    println(stats(emptyList()))
    val (lo, hi, avg) = stats(listOf(-3, 3, 9))
    println("lo=$lo hi=$hi avg=$avg")
    val pairs = listOf("a" to 1, "b" to 2, "c" to 3)
    println(pairs.toMap())
    println(pairs.unzip())
    println(pairs.map { (k, v) -> "$k$v" })
    println(("x" to ("y" to "z")).hashCode() == ("x" to ("y" to "z")).hashCode())
    println(Triple(1, "two", 3.0).toList())

    println("== keys ==")
    val counts = LinkedHashMap<Point, Int>()
    for (i in 0 until 40) {
        val k = Point(i % 4, (i * 7) % 3)
        counts[k] = (counts[k] ?: 0) + 1
    }
    for ((k, v) in counts) println("  $k -> $v")
    val set = HashSet<Person>()
    people.forEach { set.add(it) }
    set.add(Person("Bob", 25))
    println("set size ${set.size} contains ${Person("Cy", 25, null, listOf("x")) in set} ${Person("Cy", 26) in set}")
}
