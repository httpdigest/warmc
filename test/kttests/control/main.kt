// Control flow, ranges and destructuring: closed, half-open (`until`, `..<`)
// and descending (`downTo`) ranges with `step`, the first/last/step a
// progression normalises to, empty and reversed progressions, ranges that end
// at Int.MAX_VALUE / start at Int.MIN_VALUE (whose loops must still stop),
// Long/Char/UInt ranges, membership tests, coerceIn; labeled break and
// continue across nested loops, do-while, while with complex conditions,
// `when` with and without a subject, `if` and `when` as expressions,
// destructuring of pairs, data classes, map entries and indexed values, with
// `_` placeholders and in lambda parameters; and `repeat`.
//
// Each range loop is printed, so a loop that runs one time too many or too
// few -- the classic overflow at the top of the range -- shows as a diff.
data class Rect(val x: Int, val y: Int, val w: Int, val h: Int)
data class Item(val name: String, val qty: Int, val price: Double)

fun progressions() {
    println("== progressions ==")
    println("1..10 ${(1..10).toList()} until ${(1 until 5).toList()} rangeUntil ${(1..<5).toList()} downTo ${(10 downTo 1).toList()}")
    println("step ${(1..10 step 3).toList()} ${(10 downTo 1 step 4).toList()} ${(0 until 20 step 7).toList()} ${(1..10).reversed().toList()} ${(1..10 step 3).reversed().toList()}")
    for (p in listOf(1..10 step 4, 1..9 step 4, 10 downTo 0 step 3, 5..1, 1 until 1, 0..0, 7 downTo 7, 3..12 step 5)) {
        println("  $p first=${p.first} last=${p.last} step=${p.step} empty=${p.isEmpty()} count=${p.count()} sum=${p.sum()}")
    }
    println("empty ${(5..1).toList()} ${(1 downTo 5).toList()} ${(1 until 1).toList()} size ${(1..0).count()}")
    println("contains ${5 in 1..10} ${10 in 1 until 10} ${0 !in 1..10} ${7 in (1..10 step 3)} ${3 in 10 downTo 1}")
    var n = 0
    for (i in Int.MAX_VALUE - 3..Int.MAX_VALUE) { n++; print("$i ") }
    println("| $n iterations")
    n = 0
    for (i in Int.MIN_VALUE + 2 downTo Int.MIN_VALUE) { n++; print("$i ") }
    println("| $n iterations")
    n = 0
    for (i in Int.MAX_VALUE - 10..Int.MAX_VALUE step 4) { n++; print("$i ") }
    println("| $n iterations")
    for (i in Long.MAX_VALUE - 2..Long.MAX_VALUE) print("$i ")
    println()
    println("long ${(1L..20L step 6).toList()} ${(10_000_000_000L downTo 9_999_999_990L step 5).toList()}")
    println("char ${('a'..'k').joinToString("")} ${('z' downTo 'q' step 2).joinToString("")} ${'m' in 'a'..'z'} ${('A'..'Z').count { it.code % 3 == 0 }}")
    println("uint ${(4294967290u..UInt.MAX_VALUE).toList()} ${(0u..10u step 5).toList()} ${(10u downTo 0u step 3).toList()}")
    println("coerce ${15.coerceIn(1, 10)} ${(-5).coerceIn(1..10)} ${5.coerceAtMost(3)} ${2.5.coerceIn(0.0, 1.0)}")
    println("indices ${listOf("a", "b", "c").indices} lastIndex ${listOf(1, 2).lastIndex} ${"hello".indices.reversed().toList()}")
}

fun labeled() {
    println("== labeled loops ==")
    val found = mutableListOf<String>()
    outer@ for (i in 1..9) {
        for (j in 1..9) {
            if (j > i) continue@outer
            if (i * j > 50) break@outer
            if ((i + j) % 4 == 0) continue
            if (i * j % 7 == 0) found.add("$i*$j")
        }
    }
    println("found $found")
    var pairs = 0
    rows@ for (r in 0 until 6) {
        cols@ for (c in 0 until 6) {
            when {
                c == r -> continue@rows
                (r + c) % 5 == 0 -> continue@cols
                r * c == 12 -> break@rows
            }
            pairs++
        }
    }
    println("pairs $pairs")
    var k = 0
    var log = ""
    loop@ while (true) {
        k++
        when {
            k % 2 == 0 -> continue@loop
            k > 15 -> break@loop
        }
        log += k
    }
    println("while log $log k=$k")
    var d = 100
    var iters = 0
    do { d = if (d % 2 == 0) d / 2 else 3 * d + 1; iters++ } while (d != 1 && iters < 1000)
    println("do-while iters=$iters")
    var a = 0; var b = 50
    while (a < b && (a + b) % 7 != 0 || a == 0) { a += 3; b -= 2 }
    println("while complex a=$a b=$b")
    var noLoop = 0
    do { noLoop++ } while (false)
    println("do-once $noLoop")
    val grid = Array(5) { r -> IntArray(5) { c -> (r * 7 + c * 3) % 10 } }
    var firstNine: Pair<Int, Int>? = null
    search@ for (r in grid.indices) for (c in grid[r].indices) if (grid[r][c] == 9) { firstNine = r to c; break@search }
    println("first nine at $firstNine")
}

fun whenExpressions() {
    println("== when / if ==")
    for (x in listOf(-15, -1, 0, 1, 7, 15, 30, 42, 99, 100, 1000)) {
        val size = when {
            x < 0 -> "negative"
            x == 0 -> "zero"
            x < 10 -> "small"
            x < 100 -> "medium"
            else -> "large"
        }
        val fizz = when (0) { x % 15 -> "FizzBuzz"; x % 5 -> "Buzz"; x % 3 -> "Fizz"; else -> x.toString() }
        val parity = if (x % 2 == 0) "even" else if (x % 2 == 1) "odd" else "neg-odd"
        val bucket = when (x) { in Int.MIN_VALUE..-1 -> 'N'; 0 -> 'Z'; in 1..9, in 10..99 -> 'S'; else -> 'L' }
        println("  $x $size $fizz $parity $bucket")
    }
    val days = listOf("mon", "tue", "sat", "sun", "xyz")
    println(days.map { when (it) { "sat", "sun" -> "weekend"; "mon", "tue", "wed", "thu", "fri" -> "weekday"; else -> "?" } })
    val v: Any = 42L
    val desc = when (v) { is Int -> "int"; is Long -> if (v > 40) "big long" else "long"; else -> "other" }
    println(desc)
    var counter = 0
    val sideEffects = when (counter++) { 0 -> "first:$counter"; else -> "other:$counter" }
    println(sideEffects)
    val max = if (counter > 5) counter else { counter += 10; counter * 2 }
    println("max=$max counter=$counter")
}

fun destructuring() {
    println("== destructuring ==")
    val (a, b) = Pair("left", 2)
    val (x, _, z) = Triple(1, "skip", 3.0)
    println("$a $b $x $z")
    val rects = listOf(Rect(0, 0, 3, 4), Rect(1, 2, 5, 5), Rect(-2, -2, 1, 10))
    for ((rx, ry, w, h) in rects) println("  rect at ($rx,$ry) area=${w * h} far=(${rx + w},${ry + h})")
    val (_, _, w0, h0) = rects[1]
    println("  second w=$w0 h=$h0")
    val inventory = listOf(Item("apple", 3, 0.5), Item("pear", 0, 0.75), Item("fig", 12, 0.2))
    println("  total ${inventory.sumOf { (_, q, p) -> q * p }} names ${inventory.map { (name) -> name.uppercase() }}")
    val m = linkedMapOf("one" to 1, "two" to 2, "three" to 3)
    for ((k, v) in m) print("$k=$v ")
    println()
    println("  ${m.map { (k, v) -> k.length * v }} ${m.entries.filter { (_, v) -> v % 2 == 1 }.map { it.key }}")
    for ((i, s) in listOf("x", "y", "z").withIndex()) print("$i:$s ")
    println()
    val (q, r) = 17 / 5 to 17 % 5
    println("  q=$q r=$r")
    val swapped = listOf(1 to "a", 2 to "b").map { (n, s) -> s to n }
    println("  $swapped")
    val nested = listOf(Pair(1, Pair("a", true)), Pair(2, Pair("b", false)))
    for ((n, inner) in nested) { val (s, flag) = inner; println("  $n $s $flag") }
    val (first, second, third) = "a,b,c,d".split(",")
    println("  $first $second $third")
    val (lo, hi) = listOf(5, 3, 9, 1).let { it.min() to it.max() }
    println("  lo=$lo hi=$hi")
}

fun repeats() {
    println("== repeat ==")
    val sb = StringBuilder()
    repeat(4) { i -> repeat(i + 1) { j -> sb.append(i * j) }; sb.append('|') }
    println(sb)
    var total = 0
    repeat(0) { total += 1000 }
    repeat(100) { if (it % 7 == 0) total += it }
    println("total $total")
    val triangle = (1..6).map { r -> (1..r).joinToString(" ") { c -> binom(r - 1, c - 1).toString() } }
    triangle.forEach { println("  $it") }
}

fun binom(n: Int, k: Int): Long { var r = 1L; for (i in 1..k) r = r * (n - k + i) / i; return r }

fun main() {
    progressions()
    labeled()
    whenExpressions()
    destructuring()
    repeats()
}
