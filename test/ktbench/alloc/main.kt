// Object churn: the shape wasm GC exists for.
//
// Each iteration allocates a Point that dies immediately, so this is a
// nursery-allocation and minor-collection benchmark rather than a compute one.
// `struct.new` per iteration is what warmc's inline allocation fast path and
// its generational write barrier are for.
class Point(val x: Int, val y: Int)

fun main() {
    var s = 0L
    for (i in 0 until 20_000_000) {
        val p = Point(i, i + 1)
        s += (p.x + p.y).toLong()
    }
    println("s = $s")
}
