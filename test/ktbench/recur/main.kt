// Deep and wide recursion: Ackermann, Takeuchi, and a tailrec gcd.
//
// Plain functions on Ints with no objects at all, so what is left is the call:
// ack(3, n) recurses about 2^(n+3) frames deep and makes millions of calls, tak
// branches three ways at every level, and both are ordinary `call`s, since
// Kotlin/Wasm emits no `return_call`. `tailrec fun gcd` is the contrast: the
// compiler turns it into a `loop`, and its Long remainder is a call to
// `Long__rem-impl`. `bintree` recurses too, but allocates on the way; this
// measures the frame -- prologue, epilogue, spills and stack checks -- by itself.
fun ack(m: Int, n: Int): Int = when {
    m == 0 -> n + 1
    n == 0 -> ack(m - 1, 1)
    else -> ack(m - 1, ack(m, n - 1))
}

fun tak(x: Int, y: Int, z: Int): Int =
    if (y < x) tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y)) else z

tailrec fun gcd(a: Long, b: Long): Long = if (b == 0L) a else gcd(b, a % b)

fun main() {
    var s = 0L
    for (r in 0 until 6) s += ack(3, 7 + (r and 1))
    for (r in 0 until 4) s = s * 31 + tak(18 + r, 12, 6)
    var g = 0L
    for (i in 1L..1_200_000L) g += gcd(i * 2_654_435_761L % 999_983L * 12L, (i xor 0x5A5AL) * 18L)
    println("s = $s g = $g")
}
