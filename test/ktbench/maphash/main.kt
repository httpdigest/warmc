// A hash map, which is where a managed language spends its allocation budget.
//
// Boxing is the cost being measured: a HashMap<Int, Int> in Kotlin/Wasm holds
// *references*, so every key and value is an object, and every lookup is a cast
// and a field read rather than a compare. gobench/maphash and rsbench/mapint
// are the unboxed answers to the same question.
fun main() {
    var s = 0L
    for (r in 0 until 40) {
        val m = HashMap<Int, Int>()
        for (i in 0 until 20_000) m[i * 7 + r] = i
        for (i in 0 until 20_000) s += (m[i * 7 + r] ?: 0).toLong()
    }
    println("s = $s")
}
