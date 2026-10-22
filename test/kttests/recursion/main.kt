// Recursion: tailrec functions (turned into loops by the compiler, so they
// run a million deep without a stack), plain and mutual recursion,
// Ackermann, memoised recursion over a map, backtracking (N-queens, subset
// sum, permutations, a Sudoku-style grid fill), divide and conquer
// (merge sort, quickselect, Karatsuba-style multiplication on longs, fast
// exponentiation), a recursive-descent parser and evaluator for arithmetic
// expressions, flood fill, and moderately deep non-tail recursion.
//
// Non-tail recursion is real wasm call depth with GC references live across
// every call, so the collector has to find them in each frame; the deepest
// chains here stay at a few thousand frames, well inside both engines' stacks.
tailrec fun sumTo(n: Long, acc: Long = 0): Long = if (n == 0L) acc else sumTo(n - 1, acc + n)
tailrec fun gcd(a: Int, b: Int): Int = if (b == 0) a else gcd(b, a % b)
tailrec fun collatzLen(n: Long, steps: Int = 0): Int = if (n == 1L) steps else collatzLen(if (n % 2 == 0L) n / 2 else 3 * n + 1, steps + 1)
tailrec fun findFixpoint(x: Double, f: (Double) -> Double, n: Int = 0): Pair<Double, Int> {
    val y = f(x)
    return if (y == x || n > 1000) Pair(y, n) else findFixpoint(y, f, n + 1)
}

fun isEven(n: Int): Boolean = if (n == 0) true else isOdd(n - 1)
fun isOdd(n: Int): Boolean = if (n == 0) false else isEven(n - 1)

fun ackermann(m: Long, n: Long): Long = when {
    m == 0L -> n + 1
    n == 0L -> ackermann(m - 1, 1)
    else -> ackermann(m - 1, ackermann(m, n - 1))
}

val memo = HashMap<Long, Long>()
fun partitions(n: Int, max: Int): Long {
    if (n == 0) return 1
    if (n < 0 || max == 0) return 0
    val key = n.toLong() * 1000 + max
    memo[key]?.let { return it }
    val r = partitions(n - max, max) + partitions(n, max - 1)
    memo[key] = r
    return r
}

fun queens(n: Int): Pair<Int, List<Int>?> {
    val cols = IntArray(n)
    var count = 0
    var first: List<Int>? = null
    fun ok(row: Int, c: Int): Boolean {
        for (r in 0 until row) { val d = cols[r] - c; if (d == 0 || d == row - r || d == r - row) return false }
        return true
    }
    fun place(row: Int) {
        if (row == n) { count++; if (first == null) first = cols.toList(); return }
        for (c in 0 until n) if (ok(row, c)) { cols[row] = c; place(row + 1) }
    }
    place(0)
    return Pair(count, first)
}

fun subsetSums(xs: List<Int>, target: Int): List<List<Int>> {
    val out = mutableListOf<List<Int>>()
    fun go(i: Int, remaining: Int, chosen: MutableList<Int>) {
        if (remaining == 0) { out.add(chosen.toList()); return }
        if (i == xs.size || remaining < 0) return
        chosen.add(xs[i]); go(i + 1, remaining - xs[i], chosen); chosen.removeAt(chosen.size - 1)
        go(i + 1, remaining, chosen)
    }
    go(0, target, mutableListOf())
    return out
}

fun <T> perms(xs: List<T>): List<List<T>> =
    if (xs.size <= 1) listOf(xs) else xs.indices.flatMap { i -> perms(xs.take(i) + xs.drop(i + 1)).map { listOf(xs[i]) + it } }

fun latinSquare(n: Int): Array<IntArray>? {
    val g = Array(n) { IntArray(n) }
    fun fits(r: Int, c: Int, v: Int): Boolean {
        for (k in 0 until n) if (g[r][k] == v || g[k][c] == v) return false
        return true
    }
    fun fill(pos: Int): Boolean {
        if (pos == n * n) return true
        val r = pos / n; val c = pos % n
        for (v in ((r * 2 + c) % n + 1..n) + (1..(r * 2 + c) % n)) {
            if (fits(r, c, v)) { g[r][c] = v; if (fill(pos + 1)) return true; g[r][c] = 0 }
        }
        return false
    }
    return if (fill(0)) g else null
}

fun mergeSort(xs: List<Int>): List<Int> {
    if (xs.size <= 1) return xs
    val l = mergeSort(xs.subList(0, xs.size / 2)); val r = mergeSort(xs.subList(xs.size / 2, xs.size))
    val out = ArrayList<Int>(xs.size)
    var i = 0; var j = 0
    while (i < l.size && j < r.size) out.add(if (l[i] <= r[j]) l[i++] else r[j++])
    while (i < l.size) out.add(l[i++]); while (j < r.size) out.add(r[j++])
    return out
}

fun quickselect(a: IntArray, k: Int, lo: Int = 0, hi: Int = a.size - 1): Int {
    if (lo == hi) return a[lo]
    val pivot = a[(lo + hi) / 2]
    var i = lo; var j = hi
    while (i <= j) {
        while (a[i] < pivot) i++
        while (a[j] > pivot) j--
        if (i <= j) { val t = a[i]; a[i] = a[j]; a[j] = t; i++; j-- }
    }
    return when { k <= j -> quickselect(a, k, lo, j); k >= i -> quickselect(a, k, i, hi); else -> a[k] }
}

fun power(b: Long, e: Int): Long = when {
    e == 0 -> 1
    e % 2 == 0 -> power(b, e / 2).let { it * it }
    else -> b * power(b, e - 1)
}

fun karatsuba(x: Long, y: Long): Long {
    if (x < 10 || y < 10) return x * y
    val m = maxOf(x.toString().length, y.toString().length) / 2
    val p = power(10, m)
    val a = x / p; val b = x % p; val c = y / p; val d = y % p
    val ac = karatsuba(a, c); val bd = karatsuba(b, d)
    val mid = karatsuba(a + b, c + d) - ac - bd
    return ac * p * p + mid * p + bd
}

class Parser(private val s: String) {
    private var pos = 0
    private fun peek(): Char = if (pos < s.length) s[pos] else '\u0000'
    private fun skip() { while (peek() == ' ') pos++ }
    fun parse(): Long { val v = expr(); skip(); require(pos == s.length) { "trailing input at $pos" }; return v }
    private fun expr(): Long {
        var v = term()
        while (true) { skip(); when (peek()) { '+' -> { pos++; v += term() }; '-' -> { pos++; v -= term() }; else -> return v } }
    }
    private fun term(): Long {
        var v = factor()
        while (true) {
            skip()
            when (peek()) {
                '*' -> { pos++; v *= factor() }
                '/' -> { pos++; val d = factor(); require(d != 0L) { "division by zero" }; v /= d }
                '%' -> { pos++; v %= factor() }
                else -> return v
            }
        }
    }
    private fun factor(): Long {
        skip()
        val base = unary()
        skip()
        return if (peek() == '^') { pos++; power(base, factor().toInt()) } else base
    }
    private fun unary(): Long {
        skip()
        return when (peek()) {
            '-' -> { pos++; -unary() }
            '(' -> { pos++; val v = expr(); skip(); require(peek() == ')') { "expected ) at $pos" }; pos++; v }
            in '0'..'9' -> { var v = 0L; while (peek() in '0'..'9') { v = v * 10 + (peek() - '0'); pos++ }; v }
            else -> throw IllegalArgumentException("unexpected '${peek()}' at $pos")
        }
    }
}

fun floodFill(grid: Array<CharArray>, r: Int, c: Int, from: Char, to: Char): Int {
    if (r !in grid.indices || c !in grid[0].indices || grid[r][c] != from) return 0
    grid[r][c] = to
    return 1 + floodFill(grid, r + 1, c, from, to) + floodFill(grid, r - 1, c, from, to) +
        floodFill(grid, r, c + 1, from, to) + floodFill(grid, r, c - 1, from, to)
}

data class TreeNode(val v: Int, val kids: List<TreeNode>)
fun buildTree(depth: Int, seed: Int): TreeNode =
    TreeNode(seed, if (depth == 0) emptyList() else (0 until (seed % 3) + 1).map { buildTree(depth - 1, seed * 7 + it + 1) })
fun TreeNode.sum(): Long = v + kids.sumOf { it.sum() }
fun TreeNode.depth(): Int = 1 + (kids.maxOfOrNull { it.depth() } ?: 0)
fun TreeNode.leaves(): Int = if (kids.isEmpty()) 1 else kids.sumOf { it.leaves() }

fun chainDepth(n: Int): Int = if (n == 0) 0 else 1 + chainDepth(n - 1)
fun listOfDepth(n: Int): List<Int> = if (n == 0) emptyList() else listOf(n) + listOfDepth(n - 1).take(3)

fun main() {
    println("== tailrec ==")
    println("sumTo ${sumTo(10)} ${sumTo(1_000_000)} gcd ${gcd(1071, 462)} ${gcd(17, 5)} collatz ${(1L..15L).map { collatzLen(it) }} ${collatzLen(837799)}")
    println("fixpoint cos ${findFixpoint(1.0, { kotlin.math.cos(it) })} sqrt2 ${findFixpoint(1.0, { (it + 2 / it) / 2 })}")

    println("== mutual / ackermann ==")
    println("even ${(0..10).map { isEven(it) }} odd(999)=${isOdd(999)} even(2000)=${isEven(2000)}")
    for (m in 0L..3L) println("  A($m, n) = ${(0L..4L).map { ackermann(m, it) }}")

    println("== memo ==")
    println("partitions ${(1..30).map { partitions(it, it) }}")
    println("p(100)=${partitions(100, 100)} p(200)=${partitions(200, 200)} memo size=${memo.size}")

    println("== backtracking ==")
    for (n in 1..9) { val (c, f) = queens(n); println("  queens($n) = $c first=$f") }
    println("subsets ${subsetSums(listOf(3, 34, 4, 12, 5, 2, 7, 1), 15)}")
    val p4 = perms(listOf('a', 'b', 'c', 'd'))
    println("perms ${p4.size} ${p4.take(8).map { it.joinToString("") }} last=${p4.last().joinToString("")}")
    for (n in listOf(3, 4, 5)) println("  latin($n) = ${latinSquare(n)?.map { it.toList() }}")

    println("== divide and conquer ==")
    val rnd = kotlin.random.Random(2024)
    val data = List(200) { rnd.nextInt(-1000, 1000) }
    val ms = mergeSort(data)
    println("mergeSort ok=${ms == data.sorted()} head=${ms.take(6)} tail=${ms.takeLast(3)}")
    val arr = data.toIntArray()
    println("quickselect ${listOf(0, 1, 50, 100, 199).map { quickselect(arr.copyOf(), it) }} vs ${listOf(0, 1, 50, 100, 199).map { ms[it] }}")
    println("power ${power(3, 13)} ${power(2, 62)} ${power(-2, 63)} ${power(7, 0)}")
    for ((x, y) in listOf(1234L to 5678L, 99999L to 99999L, 123456789L to 987654321L, 3L to 1000000007L)) println("  karatsuba($x,$y)=${karatsuba(x, y)} check=${x * y}")

    println("== parser ==")
    for (src in listOf("1 + 2 * 3", "(1 + 2) * 3", "2 ^ 3 ^ 2", "-(4 - 10) * -2", "100 / 7 % 4", "((((7))))", "2 * (3 + 4) * (5 - 1) - 6 / 2",
            "10 - 2 - 3", "2^62 + 2^62", "1 / 0", "3 + * 4", "(1 + 2", "12 34")) {
        val r = try { Parser(src).parse().toString() } catch (e: IllegalArgumentException) { "error: ${e.message}" }
        println("  $src = $r")
    }

    println("== flood fill / trees / depth ==")
    val grid = arrayOf("..#.....", ".##.###.", "....#...", "###.#.##", "....#...").map { it.toCharArray() }.toTypedArray()
    println("filled ${floodFill(grid, 0, 0, '.', 'a')} ${floodFill(grid, 0, 7, '.', 'b')} ${floodFill(grid, 0, 2, '#', 'c')}")
    grid.forEach { println("  ${it.concatToString()}") }
    val tree = buildTree(7, 1)
    println("tree sum=${tree.sum()} depth=${tree.depth()} leaves=${tree.leaves()} hash=${tree.hashCode()}")
    println("chainDepth ${chainDepth(5000)} listOfDepth ${listOfDepth(2000)}")
}
