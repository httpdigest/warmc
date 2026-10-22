// Two-dimensional primitive arrays: Array<DoubleArray> matrix multiply and
// Array<IntArray> Floyd-Warshall.
//
// A DoubleArray is a struct wrapping a wasm `(array (mut f64))`, and its `get` is a
// call that range-checks against `array.len` before the `array.get` that checks
// again. An Array<DoubleArray> is an array of Any, so each row fetched from it
// is an `Array.get` and a `ref.test`/`ref.cast` back to DoubleArray. The kernels
// hoist rows out of their inner loops by hand, as careful Kotlin does, leaving
// the inner loop a few primitive-array accesses: the case where removing the
// redundant bounds checks and keeping the storage pointer in a register is the
// whole game. `sortints` has the one-dimensional IntArray; this adds the
// indirection, and doubles.
fun matmul(a: Array<DoubleArray>, b: Array<DoubleArray>, c: Array<DoubleArray>) {
    val n = a.size
    for (i in 0 until n) {
        val ci = c[i]
        ci.fill(0.0)
        val ai = a[i]
        for (k in 0 until n) {
            val aik = ai[k]
            val bk = b[k]
            for (j in 0 until n) ci[j] += aik * bk[j]
        }
    }
}

// All-pairs shortest paths over an Array<IntArray>.
fun floyd(d: Array<IntArray>) {
    val n = d.size
    for (k in 0 until n) {
        val dk = d[k]
        for (i in 0 until n) {
            val di = d[i]
            val dik = di[k]
            for (j in 0 until n) {
                val t = dik + dk[j]
                if (t < di[j]) di[j] = t
            }
        }
    }
}

fun main() {
    val n = 120
    var x = 42
    fun rnd(m: Int): Int { x = x * 1103515245 + 12345; return (x ushr 8) % m }
    val a = Array(n) { DoubleArray(n) { (rnd(2001) - 1000) / 1000.0 } }
    val b = Array(n) { DoubleArray(n) { (rnd(2001) - 1000) / 1000.0 } }
    val c = Array(n) { DoubleArray(n) }
    var tr = 0.0
    for (r in 0 until 24) {
        matmul(a, b, c)
        for (i in 0 until n) tr += c[i][(i + r) % n]
        val t = a[r]; a[r] = a[n - 1 - r]; a[n - 1 - r] = t
    }
    val m = 220
    val d = Array(m) { i -> IntArray(m) { j -> if (i == j) 0 else 1 + rnd(1000) } }
    floyd(d)
    var ds = 0L
    for (row in d) for (v in row) ds += v
    println("tr = ${tr.toBits()} ds = $ds")
}
