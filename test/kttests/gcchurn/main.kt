// Allocation churn for the garbage collector: binary trees built, checked and
// dropped at increasing depth while one long-lived tree stays reachable;
// linked lists built, reversed and dropped; maps whose values are replaced
// over and over; objects with parent/child cycles; closures and strings made
// in loops; ArrayLists grown past many reallocations; and a ring of
// long-lived records whose fields are rewritten with fresh objects (old-to-
// young pointers) and verified against an independent checksum at the end.
//
// Kotlin/Wasm allocates every object as a wasm GC struct and never frees one
// itself, so all of this is the engine's collector's work.  The checks print
// as they go: a collector that frees something still reachable, moves an
// object without fixing a reference, or misses a root in a frame shows as a
// wrong checksum or a trap, and the long-lived structures are re-walked after
// every phase.
class TreeNode(val left: TreeNode?, val right: TreeNode?, val v: Int) {
    fun check(): Long = v.toLong() * 3 + (left?.check() ?: 1) * 5 - (right?.check() ?: 2)
    fun count(): Int = 1 + (left?.count() ?: 0) + (right?.count() ?: 0)
}

fun bottomUp(depth: Int, v: Int): TreeNode =
    if (depth == 0) TreeNode(null, null, v) else TreeNode(bottomUp(depth - 1, 2 * v - 1), bottomUp(depth - 1, 2 * v), v)

class Cell(val value: Int, var next: Cell?)

fun buildList(n: Int, seed: Int): Cell? {
    var head: Cell? = null
    for (i in 0 until n) head = Cell((i * seed) % 1009, head)
    return head
}
fun reverse(h: Cell?): Cell? {
    var prev: Cell? = null; var cur = h
    while (cur != null) { val nx = cur.next; cur.next = prev; prev = cur; cur = nx }
    return prev
}
fun sumList(h: Cell?): Long { var s = 0L; var c = h; var i = 0; while (c != null) { s += c.value.toLong() * (i++ % 7 + 1); c = c.next }; return s }

class Parent(val name: String) {
    val children = mutableListOf<Child>()
    fun add(n: Int): Child { val c = Child(this, n); children.add(c); return c }
}
class Child(val parent: Parent, val n: Int) { var sibling: Child? = null }

data class Record(var id: Int, var payload: IntArray, var label: String, var link: Record?)

fun checksum(rs: Array<Record>): Long {
    var s = 0L
    for (r in rs) {
        s = s * 31 + r.id
        for (x in r.payload) s = s * 17 + x
        s = s * 13 + r.label.hashCode()
        s = s * 7 + (r.link?.id ?: -1)
    }
    return s
}

fun main() {
    println("== trees ==")
    val longLived = bottomUp(12, 1)
    val llCheck = longLived.check()
    println("long-lived depth 12 nodes=${longLived.count()} check=$llCheck")
    for (d in 4..14 step 2) {
        val iterations = 1 shl (14 - d)
        var check = 0L
        for (i in 1..iterations) check += bottomUp(d, i).check() + bottomUp(d, -i).check()
        println("  ${iterations * 2} trees of depth $d check=$check long-lived still ${longLived.check() == llCheck}")
    }

    println("== linked lists ==")
    var keep: Cell? = buildList(1000, 3)
    val keepSum = sumList(keep)
    for (round in 1..12) {
        val l = buildList(5000 + round * 100, round)
        val s1 = sumList(l)
        val r = reverse(l)
        val s2 = sumList(r)
        if (round % 3 == 0) keep = reverse(reverse(keep))
        println("  round $round sum=$s1 reversed=$s2 keep ok=${sumList(keep) == keepSum}")
    }

    println("== maps ==")
    val m = LinkedHashMap<String, List<Int>>()
    for (round in 0 until 30) {
        for (k in 0 until 200) {
            val key = "k" + (k * 7 + round) % 250
            m[key] = List(k % 13 + 1) { it * round + k }
        }
        if (round % 5 == 4) {
            val iter = m.keys.iterator(); var n = 0
            while (iter.hasNext()) { iter.next(); if (n++ % 3 == 0) iter.remove() }
            println("  round $round size=${m.size} total=${m.values.sumOf { v -> v.sum().toLong() }} first=${m.keys.first()}")
        }
    }

    println("== cycles ==")
    var survivors = 0L
    val kept = mutableListOf<Parent>()
    for (i in 0 until 2000) {
        val p = Parent("p$i")
        var prev: Child? = null
        for (j in 0 until 10) { val c = p.add(i + j); c.sibling = prev; prev = c }
        p.children.first().sibling = p.children.last()   // close a ring
        if (i % 100 == 0) kept.add(p)
        survivors += p.children.sumOf { it.parent.children.size + (it.sibling?.n ?: 0) }
    }
    println("  survivors=$survivors kept=${kept.size} keptCheck=${kept.sumOf { p -> p.children.sumOf { it.n + it.parent.name.length } }}")
    var walk = kept[3].children[0]
    val seen = StringBuilder()
    repeat(12) { seen.append(walk.n).append(','); walk = walk.sibling ?: walk }
    println("  ring walk $seen")

    println("== closures / strings ==")
    val fns = ArrayList<(Int) -> Int>()
    for (i in 0 until 3000) { val k = i % 17; val box = IntArray(1) { i }; fns.add { x -> x * k + box[0] } }
    var acc = 0L
    for (round in 0 until 5) for (f in fns) acc += f(round)
    println("  closures acc=$acc")
    var strHash = 0
    var longest = ""
    for (i in 0 until 20000) {
        val s = "item-" + i.toString(36) + "-" + (i * i % 997)
        strHash = strHash * 31 + s.hashCode()
        if (s.length >= longest.length) longest = s
    }
    println("  strings hash=$strHash longest=$longest")
    val sb = StringBuilder()
    for (i in 0 until 3000) { sb.append(i % 10); if (sb.length > 500) sb.setLength(100) }
    println("  builder len=${sb.length} tail=${sb.takeLast(10)}")

    println("== growth ==")
    val lists = ArrayList<ArrayList<Long>>()
    for (i in 0 until 40) {
        val l = ArrayList<Long>()
        for (j in 0 until 2000) l.add(i.toLong() * j)
        lists.add(l)
        if (lists.size > 8) lists.removeAt(0)
    }
    println("  retained=${lists.size} sums=${lists.map { it.sum() % 100000 }}")
    val boxes = ArrayList<Any>()
    for (i in 0 until 30000) boxes.add(if (i % 3 == 0) i else if (i % 3 == 1) i.toLong() * 3 else "s$i")
    println("  boxes ints=${boxes.count { it is Int }} longs=${boxes.filterIsInstance<Long>().sum()} strs=${boxes.filterIsInstance<String>().sumOf { it.length }}")

    println("== old-to-young ==")
    val ring = Array(64) { Record(it, IntArray(8) { j -> it * j }, "r$it", null) }
    for (i in ring.indices) ring[i].link = ring[(i * 5 + 1) % ring.size]
    val rng = kotlin.random.Random(99)
    var expected = 0L
    for (round in 0 until 400) {
        val r = ring[rng.nextInt(ring.size)]
        r.payload = IntArray(8) { round * 8 + it }
        r.label = "round$round/" + r.id
        if (round % 7 == 0) r.link = ring[rng.nextInt(ring.size)]
        if (round % 50 == 49) {
            expected = checksum(ring)
            var garbage = 0
            repeat(2000) { garbage += Record(it, IntArray(16), "g", null).payload.size }
            println("  round $round checksum=$expected again=${checksum(ring) == expected} garbage=$garbage")
        }
    }
    println("  final ${checksum(ring) == expected} long-lived tree ${longLived.check() == llCheck} kept list ${sumList(keep) == keepSum}")
}
