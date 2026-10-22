// Sequences: one pipeline lazily and eagerly, and a coroutine generator.
//
// `asSequence().map{}.filter{}.take()` builds a chain of TransformingSequence and
// FilteringSequence objects whose iterators pull one element at a time: each
// stage's `next` is a `call_ref` through an interface itable to the stage above,
// then a second `call_ref` to the lambda's `invoke`, and every element is a boxed
// Int or Long because the stages are generic. The eager version over Iterable
// inlines its lambdas instead, but still iterates through itable calls and
// builds a boxed ArrayList per stage. The third shape is `sequence { yield(x) }`,
// a suspend lambda compiled to a state machine: a continuation struct with the
// locals as fields, and a `br_table` on its state at every resume. No other
// kernel in this corpus reaches the coroutine machinery.
fun lazyRun(n: Int, k: Int): Long =
    (0 until n).asSequence()
        .map { it * k + 7 }
        .filter { it % 3 != 0 }
        .map { (it xor (it ushr 3)).toLong() }
        .take(n / 2)
        .sum()

fun eagerRun(n: Int, k: Int): Long =
    (0 until n)
        .map { it * k + 7 }
        .filter { it % 3 != 0 }
        .map { (it xor (it ushr 3)).toLong() }
        .take(n / 2)
        .sum()

// A generator: `sequence {}` is a coroutine, suspended at every `yield`.
fun collatz(start: Long): Sequence<Long> = sequence {
    var x = start
    while (x != 1L) {
        yield(x)
        x = if (x % 2 == 0L) x / 2 else 3 * x + 1
    }
    yield(1L)
}

fun main() {
    var a = 0L
    var b = 0L
    for (r in 0 until 42) {
        a += lazyRun(20_000, r + 3)
        b += eagerRun(20_000, r + 3)
    }
    var steps = 0L
    var peak = 0L
    for (s in 1L..4_200L) {
        steps += collatz(s * 7 + 1).count()
        peak = maxOf(peak, generateSequence(s) { if (it % 2 == 0L) it / 2 else if (it == 1L) null else 3 * it + 1 }.max())
    }
    println("lazy = $a eager = $b steps = $steps peak = $peak")
}
