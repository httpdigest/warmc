// Lambdas and higher-order functions: lambdas stored, passed and returned;
// closures capturing and mutating local state (a captured `var` becomes a
// shared box); currying and composition; inline functions with non-local
// returns, `noinline` and `crossinline`; function references to top-level
// functions, members, bound receivers, constructors and properties; function
// types with receivers; anonymous functions; local and recursive local
// functions; fun interfaces (SAM); and labeled returns out of lambdas.
//
// A non-inlined Kotlin/Wasm lambda is a struct implementing a FunctionN
// interface, called through an itable lookup and call_ref; its captured vars
// are fields, and a captured `var` is a separate box struct shared by every
// closure that sees it.  Each result prints, so a stale or unshared box shows.
fun twice(f: (Int) -> Int): (Int) -> Int = { f(f(it)) }
fun adder(n: Int): (Int) -> Int = { it + n }
fun curry(f: (Int, Int) -> Int): (Int) -> (Int) -> Int = { a -> { b -> f(a, b) } }
fun <T> applyN(n: Int, x: T, f: (T) -> T): T { var r = x; repeat(n) { r = f(r) }; return r }

fun square(x: Int) = x * x
fun isOdd(x: Int) = x % 2 != 0
fun concat(a: String, b: String) = a + b

class Scaler(val k: Int) {
    fun scale(x: Int) = x * k
    fun scaleAll(xs: List<Int>) = xs.map(::scale)
}

data class Pt(val x: Int, val y: Int)

fun interface IntOp { fun apply(a: Int, b: Int): Int }
fun interface Validator { fun check(s: String): Boolean }

fun fold(xs: List<Int>, init: Int, op: IntOp): Int { var acc = init; for (x in xs) acc = op.apply(acc, x); return acc }

inline fun <T> measure(label: String, block: () -> T): T {
    println("  enter $label")
    val r = block()
    println("  leave $label -> $r")
    return r
}

inline fun findFirst(xs: List<Int>, pred: (Int) -> Boolean): Int? {
    for (x in xs) if (pred(x)) return x
    return null
}

fun firstNegativeIndex(xs: List<Int>): Int {
    xs.forEachIndexed { i, x -> if (x < 0) return i }  // non-local return
    return -1
}

inline fun runTwice(noinline a: () -> Unit, b: () -> Unit): () -> Unit { a(); b(); return a }

inline fun deferred(crossinline f: () -> String): () -> String = { "deferred(" + f() + ")" }

fun buildString2(block: StringBuilder.() -> Unit): String { val sb = StringBuilder(); sb.block(); return sb.toString() }

fun Int.applyOp(op: Int.(Int) -> Int, other: Int) = this.op(other)

fun makeCounter(): Pair<() -> Int, () -> Unit> {
    var count = 0
    return Pair({ ++count }, { count = 0 })
}

fun <T, R> memoize(f: (T) -> R): (T) -> R {
    val cache = LinkedHashMap<T, R>()
    return { x -> cache.getOrPut(x) { f(x) } }
}

fun main() {
    println("== basics ==")
    val inc: (Int) -> Int = { it + 1 }
    val mul = { a: Int, b: Int -> a * b }
    val noArgs = { "constant" }
    println("${inc(1)} ${mul(6, 7)} ${noArgs()} ${twice(inc)(5)} ${twice(twice(adder(3)))(0)}")
    val c = curry(mul)
    println("${c(3)(4)} ${(1..5).map(c(10))} ${applyN(5, 1) { it * 3 }} ${applyN(3, "a") { it + it }}")
    val ops: List<Pair<String, (Int, Int) -> Int>> = listOf(
        "+" to { a, b -> a + b }, "-" to { a, b -> a - b }, "*" to mul,
        "max" to ::maxOf, "pow" to { a, b -> var r = 1; repeat(b) { r *= a }; r })
    for ((name, f) in ops) println("  $name: ${listOf(2 to 5, 7 to 3, -4 to 2).map { (a, b) -> f(a, b) }}")

    println("== closures ==")
    val (next, reset) = makeCounter()
    println("${next()} ${next()} ${next()}")
    reset()
    println("${next()} ${next()}")
    val (n2, _) = makeCounter()
    println("independent ${n2()} ${next()}")
    var total = 0
    val adders = (1..5).map { i -> { total += i * 10; total } }
    println(adders.map { it() } + adders.reversed().map { it() } + listOf(total))
    val captured = mutableListOf<() -> Int>()
    for (i in 0 until 4) { val j = i * i; captured.add { j + i } }
    var k = 0
    while (k < 3) { val kk = k; captured.add { kk * 100 }; k++ }
    println(captured.map { it() })
    var shared = 1
    val doubler = { shared *= 2 }
    val reader = { shared }
    repeat(5) { doubler() }
    shared += 1
    println("shared=${reader()}")

    println("== references ==")
    val nums = (1..10).toList()
    println(nums.map(::square).filter(::isOdd))
    println(listOf("a" to "b", "c" to "d").map { (x, y) -> concat(x, y) })
    val sc = Scaler(7)
    val bound = sc::scale
    println("${bound(6)} ${sc.scaleAll(nums.take(3))} ${listOf(Scaler(2), Scaler(3)).map { it.scale(5) }}")
    val unbound = Scaler::scale
    println(unbound(Scaler(11), 3))
    val ctor = ::Pt
    println((1..3).map { ctor(it, -it) })
    val getX = Pt::x
    println(listOf(Pt(3, 1), Pt(1, 2), Pt(2, 3)).sortedBy(getX).map(Pt::y))
    val strLen = String::length
    println(listOf("aa", "b", "cccc").map(strLen))
    val toUpper: (String) -> String = String::uppercase
    println(toUpper("shout"))
    val maxRef: (Int, Int) -> Int = ::maxOf
    println(listOf(3, 9, 2).reduce(maxRef))

    println("== inline / non-local ==")
    val r = measure("outer") { measure("inner") { 6 * 7 } + 1 }
    println("r=$r")
    println("findFirst ${findFirst(nums) { it > 6 }} ${findFirst(nums) { it > 60 }}")
    println("negIdx ${firstNegativeIndex(listOf(3, 4, -1, 5))} ${firstNegativeIndex(listOf(1))}")
    var log = ""
    val kept = runTwice({ log += "a" }, { log += "b" })
    kept(); kept()
    println("log=$log")
    val d = deferred { "x" + log.length }
    println(d())

    println("== receivers ==")
    val s = buildString2 {
        append("hello")
        append(' ')
        repeat(3) { append(it) }
        append(length)
    }
    println(s)
    val plusR: Int.(Int) -> Int = { this + it }
    val timesR: Int.(Int) -> Int = { this * it }
    println("${5.applyOp(plusR, 3)} ${5.applyOp(timesR, 3)} ${5.plusR(10)} ${plusR(1, 2)}")
    val greet: String.(String) -> String = { other -> "$this meets $other" }
    println("Ann".greet("Bob"))

    println("== anonymous / local functions ==")
    val anon = fun(x: Int): Int { if (x < 0) return 0; return x * 2 }
    println(listOf(-3, 4, -1, 10).map(anon))
    val evens = nums.filter(fun(x): Boolean { return x % 2 == 0 })
    println(evens)
    fun fact(n: Int): Long = if (n <= 1) 1L else n * fact(n - 1)
    println((0..15).map(::fact))
    var depth = 0
    fun hanoi(n: Int, from: Char, to: Char, via: Char, out: MutableList<String>) {
        depth++
        if (n == 0) return
        hanoi(n - 1, from, via, to, out)
        out.add("$from$to")
        hanoi(n - 1, via, to, from, out)
    }
    val moves = mutableListOf<String>()
    hanoi(4, 'A', 'C', 'B', moves)
    println("moves=${moves.size} calls=$depth ${moves.joinToString(" ")}")

    println("== SAM ==")
    val add = IntOp { a, b -> a + b }
    val sub = IntOp { a, b -> a - b }
    val maxOp = IntOp(::maxOf)
    println("${fold(nums, 0, add)} ${fold(nums, 100, sub)} ${fold(listOf(3, 99, 7), Int.MIN_VALUE, maxOp)}")
    val validators = listOf(Validator { it.isNotEmpty() }, Validator { it.all(Char::isLetter) }, Validator { it.length < 6 })
    for (w in listOf("", "abc", "abc1", "abcdefg", "Zz")) println("  '$w' ${validators.map { it.check(w) }}")

    println("== labeled returns ==")
    val out = mutableListOf<Int>()
    nums.forEach { if (it % 3 == 0) return@forEach; out.add(it) }
    println(out)
    val found = run search@{
        for (a in 1..9) for (b in a..9) if (a * b == 42) return@search "$a*$b"
        "none"
    }
    println(found)
    val firsts = listOf(listOf(1, 2, -3), listOf(-1), emptyList(), listOf(5, 6)).map lst@{ l ->
        l.forEach { if (it < 0) return@lst it }
        0
    }
    println(firsts)

    println("== memo ==")
    var computed = 0
    val slowSquare = memoize { x: Int -> computed++; x * x }
    println(listOf(3, 4, 3, 3, 5, 4, 12).map(slowSquare) + computed)
    lateinit var fib: (Int) -> Long
    fib = memoize { n -> if (n < 2) n.toLong() else fib(n - 1) + fib(n - 2) }
    println((0..60 step 6).map { fib(it) })
}
