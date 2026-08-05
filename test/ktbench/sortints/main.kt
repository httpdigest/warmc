// Sorting an IntArray: a primitive array, so no per-element objects.
//
// The point of having it beside `arrobj` is the contrast -- this is the one
// array shape Kotlin/Wasm can lay out as flat i32s, so it is the closest this
// corpus comes to what the C and Rust corpora do all the time.
fun main() {
    var s = 0L
    for (r in 0 until 200) {
        val n = 20_000
        val xs = IntArray(n)
        var x = 12345 + r
        for (i in 0 until n) { x = x * 1103515245 + 12345; xs[i] = x ushr 8 }
        xs.sort()
        for (i in 0 until n step 512) s += xs[i].toLong()
    }
    println("s = $s")
}
