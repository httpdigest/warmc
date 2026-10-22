// ArrayDeque as a FIFO and as a monotonic stack: breadth-first search on a grid,
// then a sliding-window maximum.
//
// kotlin.collections.ArrayDeque is a circular buffer over an `Array<Any?>`, so an
// ArrayDeque<Int> boxes every index it is handed (a `struct.new` of kotlin.Int at
// each addLast) and unboxes it with a `ref.cast` on the way out, and
// `isNotEmpty()` is a `call_ref` through the Collection itable. Around it is
// ordinary primitive-array work -- BooleanArray walls, IntArray distances -- each
// access a call to a get or set. `maphash` measures boxing into a hash table; this
// is the same tax on a structure whose own work is trivial, so it is mostly the
// tax.
const val W = 256
const val H = 256

var seed = 0x7F4A7C15
fun rnd(n: Int): Int { seed = seed * 1103515245 + 12345; return (seed ushr 8) % n }

fun bfs(wall: BooleanArray, dist: IntArray, start: Int): Long {
    dist.fill(-1)
    val q = ArrayDeque<Int>()
    dist[start] = 0
    q.addLast(start)
    var sum = 0L
    while (q.isNotEmpty()) {
        val c = q.removeFirst()
        val d = dist[c]
        sum += d
        val x = c % W
        val y = c / W
        if (x > 0 && !wall[c - 1] && dist[c - 1] < 0) { dist[c - 1] = d + 1; q.addLast(c - 1) }
        if (x < W - 1 && !wall[c + 1] && dist[c + 1] < 0) { dist[c + 1] = d + 1; q.addLast(c + 1) }
        if (y > 0 && !wall[c - W] && dist[c - W] < 0) { dist[c - W] = d + 1; q.addLast(c - W) }
        if (y < H - 1 && !wall[c + W] && dist[c + W] < 0) { dist[c + W] = d + 1; q.addLast(c + W) }
    }
    return sum
}

// Sliding-window maximum with a monotonic deque of indices.
fun windowMax(xs: IntArray, k: Int): Long {
    val q = ArrayDeque<Int>()
    var s = 0L
    for (i in xs.indices) {
        while (q.isNotEmpty() && xs[q.last()] <= xs[i]) q.removeLast()
        q.addLast(i)
        if (q.first() <= i - k) q.removeFirst()
        if (i >= k - 1) s += xs[q.first()]
    }
    return s
}

fun main() {
    val wall = BooleanArray(W * H) { rnd(100) < 28 }
    val dist = IntArray(W * H)
    var s = 0L
    for (r in 0 until 18) {
        val start = (r * 7919) % (W * H)
        wall[start] = false
        s = s * 31 + bfs(wall, dist, start)
    }
    val xs = IntArray(300_000) { rnd(1_000_000) }
    for (k in intArrayOf(3, 17, 64, 255, 1000)) s = s * 31 + windowMax(xs, k)
    println("s = $s")
}
