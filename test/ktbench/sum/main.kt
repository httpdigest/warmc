// A scalar reduction, and the corpus's floor for everything else.
//
// Every other kernel here allocates; this one does not, so it says what
// Kotlin/Wasm costs before the collector is involved at all.
fun main() {
    var s = 0L
    for (i in 0 until 200_000_000) s += i.toLong()
    println("s = $s")
}
