// Sealed classes and interfaces with exhaustive `when`: an expression tree that
// is evaluated, simplified, differentiated and printed; a generic sealed Result
// type with covariant `out T`; a sealed-interface state machine whose states
// include `data object`s; and `when` in all its forms -- with a subject, with
// `is`, with `in` ranges, with several conditions per branch, without a
// subject, and as an expression whose value is used.
//
// Kotlin/Wasm turns each `is` branch into a ref.test against the subclass's
// struct type followed by a ref.cast, in source order, so a miscompiled type
// test or cast picks the wrong branch and the printed tree or trace differs.
sealed class Expr {
    data class Num(val v: Long) : Expr()
    data class Var(val name: String) : Expr()
    data class Add(val l: Expr, val r: Expr) : Expr()
    data class Mul(val l: Expr, val r: Expr) : Expr()
    data class Neg(val e: Expr) : Expr()
    data class Pow(val base: Expr, val exp: Int) : Expr()
    data object Zero : Expr()
}

fun show(e: Expr): String = when (e) {
    is Expr.Num -> if (e.v < 0) "(${e.v})" else e.v.toString()
    is Expr.Var -> e.name
    is Expr.Add -> "(${show(e.l)} + ${show(e.r)})"
    is Expr.Mul -> "${show(e.l)}*${show(e.r)}"
    is Expr.Neg -> "-${show(e.e)}"
    is Expr.Pow -> "${show(e.base)}^${e.exp}"
    Expr.Zero -> "0"
}

fun eval(e: Expr, env: Map<String, Long>): Long = when (e) {
    is Expr.Num -> e.v
    is Expr.Var -> env[e.name] ?: 0L
    is Expr.Add -> eval(e.l, env) + eval(e.r, env)
    is Expr.Mul -> eval(e.l, env) * eval(e.r, env)
    is Expr.Neg -> -eval(e.e, env)
    is Expr.Pow -> { var r = 1L; val b = eval(e.base, env); repeat(e.exp) { r *= b }; r }
    Expr.Zero -> 0L
}

fun simplify(e: Expr): Expr = when (e) {
    is Expr.Add -> {
        val l = simplify(e.l); val r = simplify(e.r)
        when {
            l == Expr.Zero || l == Expr.Num(0) -> r
            r == Expr.Zero || r == Expr.Num(0) -> l
            l is Expr.Num && r is Expr.Num -> Expr.Num(l.v + r.v)
            else -> Expr.Add(l, r)
        }
    }
    is Expr.Mul -> {
        val l = simplify(e.l); val r = simplify(e.r)
        when {
            l == Expr.Zero || r == Expr.Zero || l == Expr.Num(0) || r == Expr.Num(0) -> Expr.Zero
            l == Expr.Num(1) -> r
            r == Expr.Num(1) -> l
            l is Expr.Num && r is Expr.Num -> Expr.Num(l.v * r.v)
            else -> Expr.Mul(l, r)
        }
    }
    is Expr.Neg -> when (val inner = simplify(e.e)) {
        is Expr.Num -> Expr.Num(-inner.v)
        is Expr.Neg -> inner.e
        Expr.Zero -> Expr.Zero
        else -> Expr.Neg(inner)
    }
    is Expr.Pow -> when (e.exp) {
        0 -> Expr.Num(1)
        1 -> simplify(e.base)
        else -> Expr.Pow(simplify(e.base), e.exp)
    }
    is Expr.Num, is Expr.Var, Expr.Zero -> e
}

fun deriv(e: Expr, x: String): Expr = when (e) {
    is Expr.Num, Expr.Zero -> Expr.Zero
    is Expr.Var -> if (e.name == x) Expr.Num(1) else Expr.Zero
    is Expr.Add -> Expr.Add(deriv(e.l, x), deriv(e.r, x))
    is Expr.Mul -> Expr.Add(Expr.Mul(deriv(e.l, x), e.r), Expr.Mul(e.l, deriv(e.r, x)))
    is Expr.Neg -> Expr.Neg(deriv(e.e, x))
    is Expr.Pow -> Expr.Mul(Expr.Mul(Expr.Num(e.exp.toLong()), Expr.Pow(e.base, e.exp - 1)), deriv(e.base, x))
}

fun count(e: Expr): IntArray {
    val c = IntArray(7)
    fun go(e: Expr) {
        when (e) {
            is Expr.Num -> c[0]++
            is Expr.Var -> c[1]++
            is Expr.Add -> { c[2]++; go(e.l); go(e.r) }
            is Expr.Mul -> { c[3]++; go(e.l); go(e.r) }
            is Expr.Neg -> { c[4]++; go(e.e) }
            is Expr.Pow -> { c[5]++; go(e.base) }
            Expr.Zero -> c[6]++
        }
    }
    go(e)
    return c
}

sealed class Res<out T> {
    data class Ok<T>(val value: T) : Res<T>()
    data class Err(val code: Int, val msg: String) : Res<Nothing>()
    data object Pending : Res<Nothing>()
}

fun <T, R> Res<T>.map(f: (T) -> R): Res<R> = when (this) {
    is Res.Ok -> Res.Ok(f(value))
    is Res.Err -> this
    Res.Pending -> Res.Pending
}

fun <T> Res<T>.orElse(d: T): T = when (this) {
    is Res.Ok -> value
    else -> d
}

fun parse(s: String): Res<Int> = when {
    s.isEmpty() -> Res.Pending
    s.all { it.isDigit() } -> Res.Ok(s.toInt())
    s.startsWith("-") && s.length > 1 && s.drop(1).all { it.isDigit() } -> Res.Ok(-s.drop(1).toInt())
    else -> Res.Err(s.length, "not a number: $s")
}

sealed interface State
data object Idle : State
data class Running(val job: String, val progress: Int) : State
data class Failed(val job: String, val reason: String) : State
data object Done : State

sealed interface Event
data class Start(val job: String) : Event
data class Tick(val amount: Int) : Event
data class Fail(val reason: String) : Event
data object Reset : Event

fun step(s: State, ev: Event): State = when (s) {
    Idle -> when (ev) {
        is Start -> Running(ev.job, 0)
        else -> s
    }
    is Running -> when (ev) {
        is Tick -> if (s.progress + ev.amount >= 100) Done else s.copy(progress = s.progress + ev.amount)
        is Fail -> Failed(s.job, ev.reason)
        Reset -> Idle
        is Start -> s
    }
    is Failed, Done -> if (ev == Reset) Idle else s
}

fun classify(x: Int): String = when (x) {
    0 -> "zero"
    1, 2, 3 -> "small"
    in 4..9 -> "digit"
    in 10..99 step 2 -> "even2"
    !in -1000..1000 -> "huge"
    else -> if (x < 0) "negative" else "other"
}

fun kind(a: Any?): String = when (a) {
    null -> "null"
    is Int -> if (a > 0) "posint" else "int"
    is Long, is Short -> "wide/narrow ${a::class.simpleName}"
    is String -> when {
        a.isEmpty() -> "empty string"
        a.length < 4 -> "short string '$a'"
        else -> "string of ${a.length}"
    }
    is Expr -> "expr ${show(a)}"
    is List<*> -> "list of ${a.size}"
    is Res<*> -> "res $a"
    else -> "other"
}

fun main() {
    val x = Expr.Var("x"); val y = Expr.Var("y")
    val exprs = listOf(
        Expr.Add(Expr.Mul(Expr.Num(3), x), Expr.Num(5)),
        Expr.Mul(Expr.Add(x, y), Expr.Add(x, Expr.Neg(y))),
        Expr.Pow(Expr.Add(x, Expr.Num(1)), 3),
        Expr.Add(Expr.Mul(Expr.Num(0), x), Expr.Mul(Expr.Num(1), Expr.Neg(Expr.Neg(y)))),
        Expr.Neg(Expr.Add(Expr.Zero, Expr.Mul(Expr.Pow(x, 2), Expr.Pow(y, 0)))),
        Expr.Mul(Expr.Pow(x, 4), Expr.Add(Expr.Pow(y, 2), Expr.Num(-7))),
    )
    val envs = listOf(mapOf("x" to 2L, "y" to 3L), mapOf("x" to -5L, "y" to 11L), mapOf("x" to 1000L))
    for ((i, e) in exprs.withIndex()) {
        println("e$i = ${show(e)}")
        println("  counts=${count(e).joinToString(",")}")
        println("  values=${envs.map { eval(e, it) }}")
        val s = simplify(e)
        println("  simplified=${show(s)} same=${s == e}")
        val dx = simplify(deriv(e, "x"))
        println("  d/dx=${show(dx)} at=${envs.map { eval(dx, it) }}")
        val dy = simplify(deriv(e, "y"))
        println("  d/dy=${show(dy)}")
    }
    println(Expr.Zero)
    println(Expr.Add(Expr.Num(1), Expr.Zero))

    println("== Res ==")
    for (s in listOf("42", "-17", "", "abc", "12x", "-", "007")) {
        val r = parse(s)
        val doubled = r.map { it * 2 }
        val text = r.map { "n=$it" }
        println("  '$s' -> $r doubled=$doubled text=$text orElse=${doubled.orElse(-1)}")
    }
    val results: List<Res<Any>> = listOf(Res.Ok(1), Res.Ok("s"), Res.Err(1, "e"), Res.Pending)
    println(results.map { kind(it) })

    println("== state machine ==")
    val events = listOf(Tick(5), Start("build"), Tick(30), Start("other"), Tick(30), Fail("disk"), Tick(1),
        Reset, Start("test"), Tick(50), Tick(49), Tick(1), Tick(1), Reset, Reset, Start("deploy"), Reset, Tick(3))
    var st: State = Idle
    for (ev in events) {
        val next = step(st, ev)
        println("  $st --$ev--> $next")
        st = next
    }

    println("== when ==")
    for (v in listOf(0, 1, 3, 4, 9, 10, 11, 12, 98, 99, 100, 500, -1, -999, -1001, 1001, Int.MIN_VALUE)) println("  $v ${classify(v)}")
    val things: List<Any?> = listOf(null, 5, -5, 7L, 3.toShort(), "", "abc", "abcdef", x, listOf(1, 2), 2.5, Res.Pending)
    for (t in things) println("  ${kind(t)}")
}
