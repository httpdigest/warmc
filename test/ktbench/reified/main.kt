// Inline functions with reified type parameters, over a heterogeneous array.
//
// `inline fun <reified T> Array<Any>.countIs()` is expanded at each call site with
// T known, so `x is T` becomes a `ref.test` against a concrete struct type --
// kotlin.Int, kotlin.Long, kotlin.String or a user class -- and the lambda
// passed in is pasted into the loop instead of being allocated and called
// through `call_ref` as in `lambda`. The stdlib's inline `count {}` and
// `fold {}` go the same way, with one catch: fold's accumulator keeps the erased
// type of R even after inlining, so every step boxes a fresh kotlin.Long with
// `struct.new` and unboxes the last one with a `ref.cast`. So this is type tests
// in the most direct form a producer emits them, plus a per-element allocation
// that never escapes the loop.
class Pt(val x: Int, val y: Int)
class Seg(val a: Pt, val b: Pt)

inline fun <reified T> Array<Any>.countIs(): Int {
    var c = 0
    for (x in this) if (x is T) c++
    return c
}

inline fun <reified T> Array<Any>.sumIs(f: (T) -> Long): Long {
    var s = 0L
    for (x in this) if (x is T) s += f(x)
    return s
}

var seed = 0x0BADF00D
fun rnd(n: Int): Int { seed = seed * 1103515245 + 12345; return (seed ushr 8) % n }

fun item(i: Int): Any = when (rnd(5)) {
    0 -> i
    1 -> i.toLong() * 3
    2 -> "s$i"
    3 -> Pt(i, rnd(100))
    else -> Seg(Pt(i, 1), Pt(rnd(50), i))
}

fun main() {
    val xs = Array(4_096) { item(it) }
    var s = 0L
    for (r in 0 until 1_100) {
        s += xs.countIs<String>()
        s += xs.sumIs<Int> { (it xor r).toLong() }
        s += xs.sumIs<Long> { it + r }
        s += xs.sumIs<Pt> { (it.x * it.y + r).toLong() }
        s += xs.sumIs<Seg> { (it.a.x - it.b.y).toLong() }
        s += xs.count { it is Pt && it.y > r % 100 }
        s += xs.fold(0L) { acc, x -> if (x is String) acc + x.length else acc }
    }
    println("s = $s")
}
