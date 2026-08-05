// Build and walk a binary tree, the classic collector benchmark.
//
// The same shape as gobench/bintree and rsbench/bintree, so the three rows are
// comparable -- except that here the nodes are wasm GC structs the engine owns,
// not linear-memory records the producer lays out itself.
class Node(val left: Node?, val right: Node?, val value: Int)

fun build(depth: Int, v: Int): Node? {
    if (depth == 0) return null
    return Node(build(depth - 1, v * 2), build(depth - 1, v * 2 + 1), v)
}

fun walk(n: Node?): Long {
    if (n == null) return 0L
    return n.value.toLong() + walk(n.left) + walk(n.right)
}

fun main() {
    var s = 0L
    for (r in 0 until 60) s += walk(build(16, r))
    println("s = $s")
}
