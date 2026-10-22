// Sealed classes, matched by an exhaustive `when` with no `else`.
//
// An expression tree over six subclasses of a sealed class, evaluated by
// structural recursion. Kotlin/Wasm does not dispatch this through a table: each
// arm is a `ref.test` against the subclass's struct type (all of them `sub
// final`, so an exact type compare), tried in source order, then a `ref.cast` to
// the same type before every field's `struct.get` -- casts that repeat a test
// just made -- and a call to throwNoBranchMatchedException after the last arm,
// although the compiler has proved it unreachable. So an arm costs more the
// later it is written. `iface` asks the same dispatch question of a virtual call;
// this is the answer Kotlin code gives when the set of cases is closed, and the
// one an optimiser can do most with, since a cast dominated by a test of the same
// type is free.
sealed class Expr
class Num(val v: Int) : Expr()
class Var(val i: Int) : Expr()
class Add(val l: Expr, val r: Expr) : Expr()
class Mul(val l: Expr, val r: Expr) : Expr()
class Neg(val e: Expr) : Expr()
class Sel(val c: Expr, val t: Expr, val f: Expr) : Expr()

fun eval(e: Expr, env: IntArray): Int = when (e) {
    is Num -> e.v
    is Var -> env[e.i]
    is Add -> eval(e.l, env) + eval(e.r, env)
    is Mul -> eval(e.l, env) * eval(e.r, env)
    is Neg -> -eval(e.e, env)
    is Sel -> if (eval(e.c, env) > 0) eval(e.t, env) else eval(e.f, env)
}

fun size(e: Expr): Int = when (e) {
    is Num, is Var -> 1
    is Add -> 1 + size(e.l) + size(e.r)
    is Mul -> 1 + size(e.l) + size(e.r)
    is Neg -> 1 + size(e.e)
    is Sel -> 1 + size(e.c) + size(e.t) + size(e.f)
}

var seed = 0x2545F491
fun rnd(n: Int): Int { seed = seed * 1103515245 + 12345; return (seed ushr 8) % n }

fun leaf(): Expr = if (rnd(2) == 0) Num(rnd(19) - 9) else Var(rnd(8))

fun gen(d: Int): Expr = if (d == 0) leaf() else when (rnd(6)) {
    0 -> leaf()
    1 -> Add(gen(d - 1), gen(d - 1))
    2 -> Mul(gen(d - 1), gen(d - 1))
    3 -> Add(gen(d - 1), Neg(gen(d - 1)))
    4 -> Neg(gen(d - 1))
    else -> Sel(gen(d - 1), gen(d - 1), gen(d - 1))
}

fun main() {
    val trees = Array(64) { gen(10) }
    var nodes = 0L
    for (t in trees) nodes += size(t)
    val env = IntArray(8)
    var s = 0L
    for (r in 0 until 950) {
        for (j in 0 until 8) env[j] = (r * 31 + j * 7) % 23 - 11
        for (t in trees) s = s * 31 + eval(t, env)
    }
    println("nodes = $nodes s = $s")
}
