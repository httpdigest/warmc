// Building a string, which in Kotlin/Wasm is an array of chars behind a struct.
//
// Comparable to strbuild in the Rust and AssemblyScript corpora, and the one
// kernel here whose allocation is a *growing* object rather than many small
// ones -- so it measures the copy on grow rather than the allocation rate.
fun main() {
    var h = 0L
    for (r in 0 until 300) {
        val sb = StringBuilder()
        for (i in 0 until 2_000) sb.append(i % 10)
        h += sb.toString().hashCode().toLong()
    }
    println("h = $h")
}
