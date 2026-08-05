// A linked list built and traversed: allocation with a long-lived chain.
//
// The complement to `alloc`, where everything dies at once. Here the whole list
// survives the loop that builds it, so it is promoted rather than collected,
// and the traversal is a pointer chase through engine-owned objects.
class Cell(val v: Int, val next: Cell?)

fun main() {
    var s = 0L
    for (r in 0 until 40) {
        var head: Cell? = null
        for (i in 0 until 200_000) head = Cell(i, head)
        var c = head
        while (c != null) { s += c.v.toLong(); c = c.next }
    }
    println("s = $s")
}
