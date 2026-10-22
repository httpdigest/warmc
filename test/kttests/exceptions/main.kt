// Exceptions: a custom hierarchy with messages and causes, catch clauses
// tried in order (most specific first, then a base type), finally blocks and
// the order they run in when nested and when control leaves by return, break,
// continue or a throw, a finally that overrides the result, rethrow and
// wrapping with a cause, try as an expression, exceptions thrown through
// lambdas passed to non-inline functions, through inline functions and
// across several frames of recursion, from constructors and init blocks,
// from stdlib (NumberFormat, IndexOutOfBounds, NoSuchElement, Arithmetic,
// ClassCast, IllegalArgument/State via require/check/error), and
// runCatching/Result with map/recover/getOrElse/fold.
//
// Kotlin/Wasm uses the exception-handling proposal: `throw` with a single
// Throwable tag and `try_table` landing pads that rethrow when no clause
// matches, and a finally block is duplicated onto the normal and exceptional
// paths.  The trace printed below records every catch and finally in order.
open class AppException(msg: String, cause: Throwable? = null) : Exception(msg, cause)
open class ValidationException(val field: String, msg: String) : AppException("invalid $field: $msg")
class RangeException(field: String, val value: Int, val lo: Int, val hi: Int) : ValidationException(field, "$value not in $lo..$hi")
class NotFoundException(val key: String) : AppException("not found: $key")
class FatalError(msg: String) : Error(msg)

val trace = mutableListOf<String>()
fun t(s: String) { trace.add(s) }
fun flush(label: String) { println("  $label: ${trace.joinToString(" ")}"); trace.clear() }

fun validate(age: Int, name: String): String {
    if (name.isBlank()) throw ValidationException("name", "blank")
    if (age !in 0..150) throw RangeException("age", age, 0, 150)
    if (name == "ghost") throw NotFoundException(name)
    if (name == "boom") throw IllegalStateException("boom state")
    if (name == "fatal") throw FatalError("fatal!")
    return "$name($age)"
}

fun classify(age: Int, name: String): String = try {
    validate(age, name)
} catch (e: RangeException) {
    "range[${e.field} ${e.value} ${e.lo}-${e.hi}]: ${e.message}"
} catch (e: ValidationException) {
    "validation[${e.field}]: ${e.message}"
} catch (e: AppException) {
    "app: ${e.message} (${e::class.simpleName})"
} catch (e: RuntimeException) {
    "runtime: ${e.message}"
} catch (e: Throwable) {
    "throwable: ${e.message} isError=${e is Error}"
}

fun finallyOrder(mode: Int): Int {
    t("enter$mode")
    try {
        t("outer-try")
        try {
            t("inner-try")
            when (mode) {
                1 -> throw IllegalArgumentException("m1")
                2 -> return 2
                3 -> throw NotFoundException("m3")
            }
            t("inner-end")
        } catch (e: IllegalArgumentException) {
            t("inner-catch")
        } finally {
            t("inner-finally")
        }
        t("outer-end")
    } catch (e: AppException) {
        t("outer-catch")
        return -3
    } finally {
        t("outer-finally")
    }
    return mode * 10
}

@Suppress("RETURN_IN_FINALLY")
fun finallyOverrides(): Int {
    try { t("try"); throw IllegalStateException("lost") } finally { t("finally-return"); return 42 }
}

fun loopControl(): Int {
    var sum = 0
    for (i in 0 until 6) {
        try {
            if (i == 1) continue
            if (i == 4) break
            if (i == 2) throw ArithmeticException("two")
            sum += i
        } catch (e: ArithmeticException) {
            t("caught-${e.message}"); sum += 100
        } finally {
            t("f$i")
        }
    }
    return sum
}

fun rethrowWrap(x: String): Int = try {
    try { x.toInt() } catch (e: NumberFormatException) { t("wrap"); throw AppException("bad number '$x'", e) }
} catch (e: AppException) {
    t("rethrow?${e.cause is NumberFormatException}")
    if (x.isEmpty()) throw e
    -1
}

fun deep(n: Int): Int {
    try {
        if (n == 0) throw NotFoundException("bottom")
        return deep(n - 1) + 1
    } finally {
        if (n % 5 == 0) t("unwind$n")
    }
}

fun <T> withRetry(times: Int, block: (Int) -> T): T {
    var last: Throwable? = null
    for (attempt in 1..times) {
        try { return block(attempt) } catch (e: AppException) { t("retry$attempt:${e.message}"); last = e }
    }
    throw AppException("gave up after $times", last)
}

inline fun <T> guarded(label: String, block: () -> T): T? = try {
    block()
} catch (e: IllegalArgumentException) {
    t("$label:IAE:${e.message}"); null
} finally {
    t("$label:done")
}

fun firstBad(xs: List<String>): String? {
    xs.forEach { s -> guarded("chk") { require(s.length < 4) { "too long $s" }; s } ?: return s }
    return null
}

class Resource(val name: String) {
    var closed = false
    fun close() { closed = true; t("close-$name") }
}
inline fun <R> Resource.useIt(block: (Resource) -> R): R {
    var thrown: Throwable? = null
    try { return block(this) } catch (e: Throwable) { thrown = e; throw e } finally {
        try { close() } catch (e: Throwable) { if (thrown == null) throw e }
    }
}

class Fragile(val v: Int) {
    val inv: Int
    init {
        t("init$v")
        check(v != 0) { "zero is fragile" }
        inv = 100 / v
    }
}

fun main() {
    println("== hierarchy ==")
    for ((age, name) in listOf(30 to "ann", 200 to "bob", -1 to "cy", 5 to " ", 5 to "ghost", 5 to "boom", 5 to "fatal")) {
        println("  ${classify(age, name)}")
    }
    val e: Throwable = RangeException("x", 9, 0, 5)
    println("  is ${e is ValidationException} ${e is AppException} ${e is Exception} ${e is RuntimeException} msg=${e.message} cause=${e.cause}")

    println("== finally order ==")
    for (m in 0..3) { val r = finallyOrder(m); flush("mode $m -> $r") }
    val fo = finallyOverrides(); flush("override -> $fo")
    val lc = loopControl(); flush("loop -> $lc")

    println("== rethrow / wrap ==")
    for (s in listOf("12", "x", "")) {
        val r = try { rethrowWrap(s).toString() } catch (ex: AppException) { "escaped: ${ex.message} cause=${ex.cause?.let { it::class.simpleName }}" }
        flush("'$s' -> $r")
    }
    val d = try { deep(17) } catch (ex: NotFoundException) { -ex.key.length }
    flush("deep -> $d")

    println("== lambdas / inline ==")
    val ok = withRetry(5) { n -> if (n < 3) throw NotFoundException("try$n") else "ok on $n" }
    flush("retry -> $ok")
    val gaveUp = try { withRetry(2) { n -> throw ValidationException("f", "#$n") } } catch (ex: AppException) { "${ex.message} / ${ex.cause?.message}" }
    flush("retry -> $gaveUp")
    flush("firstBad -> ${firstBad(listOf("a", "bb", "cccc", "d"))}")
    flush("firstBad -> ${firstBad(listOf("a", "b"))}")
    val mapped = try { listOf(1, 2, 0, 4).map { 12 / it } } catch (ex: ArithmeticException) { listOf(-1) }
    println("  map with div0 -> $mapped")
    var count = 0
    val partial = runCatching { (1..10).forEach { if (it == 7) throw IllegalStateException("at $it"); count += it } }
    println("  partial count=$count failure=${partial.exceptionOrNull()?.message}")

    println("== resources / constructors ==")
    val r1 = Resource("a")
    val v1 = r1.useIt { "used ${it.name}" }
    flush("use ok -> $v1 closed=${r1.closed}")
    val r2 = Resource("b")
    val v2 = try { r2.useIt { throw NotFoundException("inside") } } catch (ex: NotFoundException) { "caught ${ex.key}" }
    flush("use throw -> $v2 closed=${r2.closed}")
    for (v in listOf(4, 0, -5)) {
        val r = try { "inv=${Fragile(v).inv}" } catch (ex: IllegalStateException) { "ISE ${ex.message}" }
        flush("Fragile($v) -> $r")
    }

    println("== stdlib exceptions ==")
    val probes: List<Pair<String, () -> Any?>> = listOf(
        "toInt" to { "12x".toInt() },
        "list[5]" to { listOf(1, 2)[5] },
        "first" to { emptyList<Int>().first() },
        "single" to { listOf(1, 2).single() },
        "div0" to { 1 / (trace.size) },
        "cast" to { val a: Any = "s"; a as Int },
        "require" to { require(trace.size > 5) { "need more" } },
        "check" to { check(false) { "checked" } },
        "error" to { error("custom error") },
        "requireNotNull" to { requireNotNull(null as String?) { "was null" } },
        "iterator" to { val it = listOf(1).iterator(); it.next(); it.next() },
        "map.getValue" to { mapOf(1 to 2).getValue(3) },
        "removeLast" to { mutableListOf<Int>().removeLast() },
        "substring" to { "abc".substring(2, 1) },
        "ok" to { "fine" },
    )
    for ((name, f) in probes) {
        val res = try { "value ${f()}" } catch (ex: NumberFormatException) { "NumberFormatException" }
        catch (ex: IndexOutOfBoundsException) { "IndexOutOfBounds" }
        catch (ex: NoSuchElementException) { "NoSuchElement" }
        catch (ex: ArithmeticException) { "Arithmetic" }
        catch (ex: ClassCastException) { "ClassCast" }
        catch (ex: IllegalArgumentException) { "IllegalArgument(${ex.message})" }
        catch (ex: IllegalStateException) { "IllegalState(${ex.message})" }
        catch (ex: RuntimeException) { "Runtime ${ex::class.simpleName}" }
        println("  $name -> $res")
    }

    println("== Result ==")
    fun parse(s: String): Result<Int> = runCatching { s.trim().toInt() }
    for (s in listOf("5", " 42 ", "x", "", "-7")) {
        val r = parse(s)
        println("  '$s' ok=${r.isSuccess} value=${r.getOrNull()} orElse=${r.getOrElse { -1 }} map=${r.map { it * 2 }.getOrDefault(0)}" +
            " recover=${r.recover { 0 }.getOrThrow()} fold=${r.fold({ "v$it" }, { "e:${it::class.simpleName}" })}")
    }
    val chained = parse("10").mapCatching { 100 / (it - 10) }
    println("  chained failure=${chained.isFailure} ${chained.exceptionOrNull()?.let { it::class.simpleName }}")
    val total = listOf("1", "2", "three", "4").map(::parse).filter { it.isSuccess }.sumOf { it.getOrThrow() }
    println("  total=$total")
    val nested = runCatching { runCatching { throw FatalError("inner") }.getOrThrow() }
    println("  nested ${nested.exceptionOrNull()?.message} ${nested.exceptionOrNull() is Error}")
    println("  trace left: ${trace.size}")
}
