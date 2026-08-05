// Throwing and catching, which no other corpus in this suite does.
//
// Kotlin compiles an exception to the wasm exception-handling proposal -- a
// `throw` and a `try_table` with a `catch` -- so this is the only kernel here
// that exercises warmc's landing pads and frame descriptions from a *producer*
// rather than from a hand-written .wat fixture.
//
// The throw is on one iteration in sixteen: often enough to measure, rare
// enough that the cost of the non-throwing path still dominates, which is the
// ratio real code has.
class Small(val v: Int) : Exception()

fun step(i: Int): Int {
    if (i and 15 == 0) throw Small(i)
    return i and 7
}

fun main() {
    var s = 0L
    var caught = 0L
    for (i in 0 until 12_000_000) {
        try { s += step(i).toLong() } catch (e: Small) { caught++ }
    }
    println("s = $s caught = $caught")
}
