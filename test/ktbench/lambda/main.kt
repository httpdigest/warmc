// Closures, which become objects with a captured field and a function ref.
//
// A lambda that escapes is a `struct.new` holding the capture plus a `ref.func`,
// and calling it is a `call_ref`. rsbench/closures showed that the indirect call
// was not what made that kernel slow; this is the same question asked of a
// producer whose closures the engine type-checks.
fun apply(n: Int, f: (Int) -> Int): Long {
    var s = 0L
    for (i in 0 until n) s += f(i).toLong()
    return s
}

fun main() {
    var s = 0L
    for (r in 0 until 200) {
        val k = r and 7
        s += apply(20_000) { it * 2 + k }
        s += apply(20_000) { (it xor k) and 1023 }
    }
    println("s = $s")
}
