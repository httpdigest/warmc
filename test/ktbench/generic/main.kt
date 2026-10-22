// Generic classes with an upper bound: one erased body serving every T.
//
// `MinHeap<T : Comparable<T>>` is a binary heap written once and used here as an
// event queue of a user class and as a heapsort of Int. Kotlin/Wasm erases T to
// Any: elements live in an `Array<Any?>`, and reading one back as T checks the
// bound by loading the object's vtable, then its special itable, and testing the
// Comparable slot. `x >= pv` loads `compareTo` from that itable and calls it with
// `call_ref`, which for Job lands in a bridge that `ref.test`s and `ref.cast`s the
// argument before calling the real method; Ints are boxed with `struct.new` to go
// in at all. None of that depends on which T the call site used, so this measures
// how much type information warmc can carry through a container that erased it.
class MinHeap<T : Comparable<T>> {
    private var a = arrayOfNulls<Any>(16)
    var size = 0
        private set

    @Suppress("UNCHECKED_CAST")
    private fun at(i: Int): T = a[i] as T

    fun push(x: T) {
        if (size == a.size) a = a.copyOf(size * 2)
        var i = size++
        while (i > 0) {
            val p = (i - 1) / 2
            val pv = at(p)
            if (x >= pv) break
            a[i] = pv
            i = p
        }
        a[i] = x
    }

    fun pop(): T {
        val top = at(0)
        size--
        val last = at(size)
        a[size] = null
        if (size > 0) {
            var i = 0
            while (true) {
                var c = 2 * i + 1
                if (c >= size) break
                if (c + 1 < size && at(c + 1) < at(c)) c++
                if (last <= at(c)) break
                a[i] = a[c]
                i = c
            }
            a[i] = last
        }
        return top
    }
}

class Job(val prio: Int, val id: Int) : Comparable<Job> {
    override fun compareTo(other: Job): Int =
        if (prio != other.prio) prio.compareTo(other.prio) else id.compareTo(other.id)
}

var seed = 0x13579BDF
fun rnd(n: Int): Int { seed = seed * 1103515245 + 12345; return (seed ushr 8) % n }

fun main() {
    // An event queue: pop the earliest job, reschedule it a random delay later.
    val jobs = MinHeap<Job>()
    for (i in 0 until 4_096) jobs.push(Job(rnd(10_000), i))
    var s = 0L
    for (step in 0 until 210_000) {
        val j = jobs.pop()
        s = s * 31 + (j.prio xor j.id)
        jobs.push(Job(j.prio + 1 + rnd(5_000), j.id))
    }
    // The same heap over boxed Ints: heapsort, checked as it drains.
    var sorted = 0L
    for (r in 0 until 14) {
        val ints = MinHeap<Int>()
        for (i in 0 until 8_192) ints.push(rnd(1_000_000))
        var prev = -1
        while (ints.size > 0) {
            val v = ints.pop()
            if (v >= prev) sorted++
            prev = v
            s += v
        }
    }
    println("s = $s sorted = $sorted")
}
