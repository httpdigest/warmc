// An array of objects, updated in place.
//
// `array.get` then `struct.get`/`struct.set` per element: two engine-checked
// indirections where a linear-memory producer would emit one load. What it
// measures is whether warmc can keep the array's element type known well enough
// to skip the cast on every read.
class Item(var a: Int, var b: Int)

fun main() {
    val n = 20_000
    val xs = Array(n) { Item(it, n - it) }
    var s = 0L
    for (r in 0 until 1_000) {
        for (i in 0 until n) {
            val it0 = xs[i]
            it0.a = it0.a + it0.b
            s += it0.a.toLong()
        }
    }
    println("s = $s")
}
