// Sequences and iterators: generateSequence (seeded and null-terminated),
// `sequence { yield(...); yieldAll(...) }` builders -- which Kotlin compiles
// to suspend-function state machines, so every yield suspends and resumes a
// coroutine -- `iterator { }`, lazy pipelines whose interleaved evaluation
// order is logged, infinite sequences cut by take/takeWhile, recursive
// builders (tree traversal, permutations) that yieldAll from nested
// sequences, sequence operators (windowed, chunked, zip, distinct, sorted,
// flatMap, scan), and hand-written Iterator/Iterable classes.
//
// A coroutine here is a heap-allocated continuation struct with a label field
// and spilled locals; resumption is a call through it.  A miscompile in the
// state machine shows as a skipped or repeated element.
val trace = StringBuilder()

fun fib() = sequence {
    var a = 0L; var b = 1L
    while (true) { yield(a); val t = a + b; a = b; b = t }
}

fun primes() = sequence {
    val found = mutableListOf<Int>()
    var n = 2
    while (true) {
        if (found.none { it * it <= n && n % it == 0 }) { found.add(n); yield(n) }
        n++
    }
}

fun countdown(from: Int) = sequence {
    trace.append("[start $from]")
    for (i in from downTo 1) { trace.append("<$i>"); yield(i) }
    trace.append("[liftoff]")
    yield(0)
}

fun interleave(a: Sequence<Int>, b: Sequence<Int>) = sequence {
    val ia = a.iterator(); val ib = b.iterator()
    while (ia.hasNext() || ib.hasNext()) {
        if (ia.hasNext()) yield(ia.next())
        if (ib.hasNext()) yield(ib.next())
    }
}

class TreeNode(val v: Int, val l: TreeNode? = null, val r: TreeNode? = null)

fun TreeNode?.inorder(): Sequence<Int> = sequence {
    if (this@inorder != null) {
        yieldAll(l.inorder())
        yield(v)
        yieldAll(r.inorder())
    }
}

fun TreeNode?.levels(): Sequence<List<Int>> = sequence {
    var level = listOfNotNull(this@levels)
    while (level.isNotEmpty()) {
        yield(level.map { it.v })
        level = level.flatMap { listOfNotNull(it.l, it.r) }
    }
}

fun <T> permutations(xs: List<T>): Sequence<List<T>> = sequence {
    if (xs.size <= 1) yield(xs)
    else for (i in xs.indices) {
        val rest = xs.take(i) + xs.drop(i + 1)
        for (p in permutations(rest)) yield(listOf(xs[i]) + p)
    }
}

fun collatz(n: Long) = generateSequence(n) { if (it == 1L) null else if (it % 2 == 0L) it / 2 else 3 * it + 1 }

class Range2D(val w: Int, val h: Int) : Iterable<Pair<Int, Int>> {
    override fun iterator() = object : Iterator<Pair<Int, Int>> {
        var i = 0
        override fun hasNext() = i < w * h
        override fun next(): Pair<Int, Int> { val p = Pair(i % w, i / w); i++; return p }
    }
}

class Countdown(val n: Int) : Iterator<Int> {
    private var cur = n
    override fun hasNext() = cur > 0
    override fun next() = cur--
}

fun main() {
    println("== generateSequence ==")
    println(generateSequence(1) { it * 2 }.take(12).toList())
    println(generateSequence(1) { if (it < 200) it * 3 else null }.toList())
    println(collatz(27).count().toString() + " max=" + collatz(27).maxOrNull() + " head=" + collatz(27).take(8).toList())
    for (n in listOf(1L, 6L, 7L, 97L)) println("  collatz($n) ${collatz(n).toList().let { "${it.size} steps, peak ${it.maxOrNull()}" }}")
    var seed = 12345
    val lcg = generateSequence { seed = seed * 1103515245 + 12345; (seed ushr 16) and 0x7fff }
    println("lcg ${lcg.take(8).toList()}")
    println(generateSequence(Pair(0, 1)) { (a, b) -> Pair(b, a + b) }.map { it.first }.takeWhile { it < 1000 }.toList())

    println("== builders ==")
    println("fib ${fib().take(20).toList()}")
    println("fib[90] ${fib().elementAt(90)} even fibs ${fib().filter { it % 2 == 0L }.take(8).toList()}")
    println("primes ${primes().take(25).toList()}")
    println("primes 1000th ${primes().elementAt(999)} twin ${primes().zipWithNext().filter { (a, b) -> b - a == 2 }.take(6).toList()}")
    println("countdown ${countdown(5).toList()} trace=$trace")
    trace.clear()
    val cd = countdown(3).iterator()
    println("first=${cd.next()} trace=$trace")
    println("second=${cd.next()} trace=$trace")
    while (cd.hasNext()) cd.next()
    println("drained trace=$trace")
    println("interleave ${interleave(sequenceOf(1, 3, 5, 7, 9), generateSequence(2) { it + 2 }.take(3)).toList()}")
    val nested = sequence {
        yield("start")
        yieldAll(listOf("a", "b"))
        yieldAll(sequence { for (i in 1..3) yield("n$i") })
        yieldAll(generateSequence(10) { it + 10 }.take(2).map { "g$it" })
        yield("end")
    }
    println("nested ${nested.toList()} count=${nested.count()}")
    val it = iterator { for (c in 'a'..'e') { yield(c); if (c == 'c') yield('!') } }
    val chars = StringBuilder(); while (it.hasNext()) chars.append(it.next())
    println("iterator {} $chars")

    println("== laziness ==")
    val log = mutableListOf<String>()
    val res = (1..10).asSequence()
        .onEach { log.add("src$it") }
        .map { log.add("map$it"); it * it }
        .filter { log.add("filter$it"); it % 2 == 1 }
        .take(3)
        .toList()
    println("res=$res")
    println("log=${log.joinToString(" ")}")
    log.clear()
    val eager = (1..5).map { log.add("m$it"); it }.filter { log.add("f$it"); it > 2 }.first()
    println("eager=$eager log=${log.joinToString(" ")}")
    log.clear()
    val lazyFirst = (1..5).asSequence().map { log.add("m$it"); it }.filter { log.add("f$it"); it > 2 }.first()
    println("lazy=$lazyFirst log=${log.joinToString(" ")}")
    log.clear()
    val sorted = sequenceOf(5, 1, 4, 2, 3).onEach { log.add("pre$it") }.sorted().onEach { log.add("post$it") }.take(2).toList()
    println("sorted=$sorted log=${log.joinToString(" ")}")

    println("== operators ==")
    val s = generateSequence(1) { it + 1 }
    println("windowed ${s.windowed(3, 2).take(4).toList()} chunked ${s.chunked(4).take(3).map { it.sum() }.toList()}")
    println("zip ${s.zip(s.map { it * it }).take(5).toList()} distinct ${s.map { it % 7 }.take(20).distinct().toList()}")
    println("flatMap ${s.take(4).flatMap { n -> sequenceOf(n, -n) }.toList()} scan ${s.take(6).scan(0) { a, b -> a + b }.toList()}")
    println("mapNotNull ${s.take(15).mapNotNull { if (it % 4 == 0) "q$it" else null }.toList()} indexed ${s.take(10).filterIndexed { i, _ -> i % 3 == 0 }.toList()}")
    println("groupBy ${s.take(12).groupBy { it % 3 }} associate ${s.take(4).associateWith { it * 1.5 }}")
    println("sums ${s.take(100).sum()} ${s.take(100).map { it.toLong() * it }.sum()} fold ${s.take(10).fold("") { acc, i -> acc + (i % 10) }}")
    println("minus ${(sequenceOf(1, 2, 3, 4, 2) - 2).toList()} plus ${(sequenceOf(1) + listOf(2, 3) + sequenceOf(4)).toList()}")
    println("constrained once: ${try { val once = sequenceOf(1, 2).constrainOnce(); once.toList(); once.toList(); "no" } catch (e: IllegalStateException) { "yes" }}")

    println("== recursive ==")
    val tree = TreeNode(8, TreeNode(4, TreeNode(2, TreeNode(1), TreeNode(3)), TreeNode(6, TreeNode(5), TreeNode(7))),
        TreeNode(12, TreeNode(10, null, TreeNode(11)), TreeNode(14, TreeNode(13))))
    println("inorder ${tree.inorder().toList()} levels ${tree.levels().toList()}")
    println("first>9 ${tree.inorder().first { it > 9 }} empty ${(null as TreeNode?).inorder().toList()}")
    val perms = permutations(listOf(1, 2, 3, 4))
    println("perms count=${perms.count()} first6=${perms.take(6).map { it.joinToString("") }.toList()}")
    println("perms abc ${permutations("abc".toList()).map { it.joinToString("") }.toList()}")
    println("perms sorted-check ${permutations((1..5).toList()).filter { p -> p.zipWithNext().all { (a, b) -> a < b } }.toList()}")

    println("== iterables ==")
    println("range2d ${Range2D(3, 2).toList()} sum ${Range2D(4, 4).sumOf { (x, y) -> x * y }}")
    val c = Countdown(5)
    val got = mutableListOf<Int>()
    for (x in c) got.add(x)
    println("countdown iterator $got ${Countdown(3).asSequence().map { it * 10 }.toList()}")
    println("withIndex ${Range2D(2, 2).withIndex().map { (i, p) -> "$i:${p.first}${p.second}" }}")
}

