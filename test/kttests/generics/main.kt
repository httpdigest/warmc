// Generics: generic classes and functions, upper bounds and `where` clauses,
// declaration-site variance (`out` producers, `in` consumers), use-site
// variance and star projections, reified type parameters in inline functions
// (`is T`, `T::class`, filtering by type), type checks and smart casts on
// generic values, and a generic binary search tree keyed by Comparable.
//
// Kotlin/Wasm erases type parameters to their bound, so a generic container
// holds `anyref`-like references and every read back at a concrete type is a
// ref.cast (and for Int/Long/Double an unboxing struct.get).  Reified
// functions are expanded at each call site into ref.test against a concrete
// struct type.  Wrong casts trap; wrong tests pick the wrong branch.
interface Source<out T> { fun next(): T; fun peekAll(): List<T> }
interface Sink<in T> { fun put(x: T); fun count(): Int }

class CountingSource(private var n: Int) : Source<Int> {
    override fun next(): Int = n++
    override fun peekAll() = listOf(n, n + 1, n + 2)
}

class Collector<T> : Sink<T> {
    val items = mutableListOf<T>()
    override fun put(x: T) { items.add(x) }
    override fun count() = items.size
}

fun <T> pump(from: Source<T>, to: Sink<T>, n: Int) { repeat(n) { to.put(from.next()) } }

fun sumAll(src: Source<Number>, n: Int): Double { var s = 0.0; repeat(n) { s += src.next().toDouble() }; return s }

fun fillInts(sink: Sink<Int>, xs: IntRange) { for (x in xs) sink.put(x) }

class Stack<T>(vararg init: T) : Iterable<T> {
    private val data = ArrayList<T>()
    init { for (x in init) push(x) }
    fun push(x: T) { data.add(x) }
    fun pop(): T? = if (data.isEmpty()) null else data.removeAt(data.size - 1)
    fun peek(): T? = data.lastOrNull()
    val size get() = data.size
    override fun iterator(): Iterator<T> = data.asReversed().iterator()
    fun <R> map(f: (T) -> R): Stack<R> { val s = Stack<R>(); for (x in data) s.push(f(x)); return s }
    override fun toString() = "Stack" + data
}

fun <T : Comparable<T>> maxOf3(a: T, b: T, c: T): T = maxOf(a, maxOf(b, c))

fun <T> clampAll(xs: List<T>, lo: T, hi: T): List<T> where T : Comparable<T>, T : Any =
    xs.map { if (it < lo) lo else if (it > hi) hi else it }

fun <K, V> invert(m: Map<K, V>): Map<V, List<K>> {
    val out = LinkedHashMap<V, MutableList<K>>()
    for ((k, v) in m) out.getOrPut(v) { mutableListOf() }.add(k)
    return out
}

fun <A, B, C> compose(f: (A) -> B, g: (B) -> C): (A) -> C = { g(f(it)) }

fun <T> swap(p: Pair<T, T>): Pair<T, T> = Pair(p.second, p.first)

fun copyInto(from: Array<out Any>, to: Array<in String>) {
    for (i in from.indices) to[i] = from[i].toString() + "!"
}

fun describeList(xs: List<*>): String {
    val kinds = xs.map {
        when (it) {
            null -> "null"
            is Int -> "I$it"
            is String -> "S${it.length}"
            is List<*> -> "L${it.size}"
            else -> "?"
        }
    }
    return kinds.joinToString(" ")
}

inline fun <reified T> List<Any?>.only(): List<T> = filter { it is T }.map { it as T }
inline fun <reified T> Any?.isA(): Boolean = this is T
inline fun <reified T : Any> typeName(): String = T::class.simpleName ?: "?"
inline fun <reified T> countOf(xs: Array<Any?>): Int { var c = 0; for (x in xs) if (x is T) c++; return c }
inline fun <reified T> List<Any?>.firstOfOrNull(): T? { for (x in this) if (x is T) return x; return null }

class Tree<K : Comparable<K>, V> {
    private inner class Node(val key: K, var value: V) {
        var left: Node? = null
        var right: Node? = null
    }
    private var root: Node? = null
    var size = 0
        private set

    fun put(k: K, v: V) {
        var cur: Node = root ?: run { root = Node(k, v); size++; return }
        while (true) {
            val c = k.compareTo(cur.key)
            if (c == 0) { cur.value = v; return }
            val next = if (c < 0) cur.left else cur.right
            if (next == null) {
                if (c < 0) cur.left = Node(k, v) else cur.right = Node(k, v)
                size++
                return
            }
            cur = next
        }
    }

    operator fun get(k: K): V? {
        var cur = root
        while (cur != null) {
            val c = k.compareTo(cur.key)
            if (c == 0) return cur.value
            cur = if (c < 0) cur.left else cur.right
        }
        return null
    }

    fun inorder(): List<Pair<K, V>> {
        val out = mutableListOf<Pair<K, V>>()
        fun go(n: Node?) { if (n == null) return; go(n.left); out.add(n.key to n.value); go(n.right) }
        go(root)
        return out
    }

    fun height(): Int {
        fun h(n: Node?): Int = if (n == null) 0 else 1 + maxOf(h(n.left), h(n.right))
        return h(root)
    }
}

open class Animal(val name: String) { override fun toString() = "Animal($name)" }
class Cat(name: String) : Animal(name) { override fun toString() = "Cat($name)" }

fun feedAll(xs: List<Animal>) = xs.joinToString { "fed ${it.name}" }
fun addCats(xs: MutableList<in Cat>) { xs.add(Cat("tom")); xs.add(Cat("kit")) }

fun main() {
    println("== variance ==")
    val src = CountingSource(5)
    val col = Collector<Int>()
    pump(src, col, 4)
    println("collected ${col.items} count=${col.count()} peek=${src.peekAll()}")
    val numSrc: Source<Number> = CountingSource(1)
    println("sumAll ${sumAll(numSrc, 10)}")
    val anySink = Collector<Any>()
    fillInts(anySink, 1..5)
    val intSink: Sink<Int> = anySink
    intSink.put(99)
    println("anySink ${anySink.items} ${anySink.count()}")
    val cats: List<Cat> = listOf(Cat("a"), Cat("b"))
    println(feedAll(cats))
    val animals = mutableListOf<Animal>(Animal("dog"))
    addCats(animals)
    println(animals)
    val anys = arrayOfNulls<Any>(3)
    val strs = arrayOf<Any>("x", 1, 2.5)
    val dest = Array<Any?>(3) { null }
    copyInto(strs, arrayOf("p", "q", "r").also { copyInto(arrayOf(7, 8, 9), it); println(it.toList()) })
    @Suppress("UNCHECKED_CAST")
    copyInto(strs, dest as Array<in String>)
    println("${anys.toList()} ${dest.toList()}")

    println("== Stack ==")
    val st = Stack(1, 2, 3)
    st.push(4)
    println("$st size=${st.size} peek=${st.peek()} iter=${st.toList()}")
    val sq = st.map { "n" + it * it }
    println("$sq pop=${sq.pop()} pop=${sq.pop()} $sq")
    while (st.size > 0) print("${st.pop()} ")
    println("| empty pop=${st.pop()} peek=${st.peek()}")
    val nested = Stack(Stack("a"), Stack("b", "c"))
    println("nested $nested sizes=${nested.map { it.size }}")

    println("== bounds ==")
    println("${maxOf3(3, 9, 4)} ${maxOf3("pear", "apple", "zoo")} ${maxOf3(2.5, -1.0, 2.4)} ${maxOf3('a', 'z', 'm')}")
    println(clampAll(listOf(-5, 0, 5, 10, 15), 0, 10))
    println(clampAll(listOf("a", "m", "z"), "b", "y"))
    println(invert(mapOf("one" to 1, "uno" to 1, "two" to 2, "eins" to 1, "zwei" to 2, "three" to 3)))
    val f = compose<Int, String, Int>({ it.toString(2) }, { it.count { c -> c == '1' } })
    println((0..16).map(f))
    val g = compose(compose({ x: Int -> x * 3 }, { it + 1 }), { "<$it>" })
    println((1..5).map(g))
    println("${swap(1 to 2)} ${swap("a" to "b")}")

    println("== star / reified ==")
    val mixed: List<Any?> = listOf(1, "two", null, 3, listOf(4, 5), "six", 7L, 8.0, 'c', Cat("z"), Animal("w"), 9)
    println(describeList(mixed))
    println("ints=${mixed.only<Int>()} strings=${mixed.only<String>()} longs=${mixed.only<Long>()}")
    println("animals=${mixed.only<Animal>()} cats=${mixed.only<Cat>()} lists=${mixed.only<List<*>>()}")
    println("nums=${mixed.only<Number>()} comparables=${mixed.only<Comparable<*>>().size} any=${mixed.only<Any>().size}")
    println("first String=${mixed.firstOfOrNull<String>()} Double=${mixed.firstOfOrNull<Double>()} Short=${mixed.firstOfOrNull<Short>()}")
    for (x in listOf<Any?>(1, "s", null, 2.0, Cat("c"))) {
        println("  $x int=${x.isA<Int>()} str=${x.isA<String>()} nullableStr=${x.isA<String?>()} animal=${x.isA<Animal>()} any=${x.isA<Any>()}")
    }
    println("${typeName<Int>()} ${typeName<String>()} ${typeName<Cat>()} ${typeName<Tree<Int, Int>>()} ${typeName<List<Int>>()}")
    val arr = arrayOf<Any?>(1, 2, "3", null, 4.0, 5, Cat("x"))
    println("countOf Int=${countOf<Int>(arr)} Any=${countOf<Any>(arr)} Any?=${countOf<Any?>(arr)} Animal=${countOf<Animal>(arr)}")

    println("== smart casts ==")
    fun area(x: Any): Double = when {
        x is Int && x > 0 -> x.toDouble() * x
        x is Double -> x * x
        x is String && x.length > 2 -> x.length.toDouble()
        x is Pair<*, *> && x.first is Int && x.second is Int -> (x.first as Int).toDouble() * (x.second as Int)
        else -> -1.0
    }
    for (x in listOf<Any>(3, -3, 1.5, "ab", "abcd", 2 to 7, "x" to 1, Unit)) println("  area($x)=${area(x)}")

    println("== Tree ==")
    val t = Tree<Int, String>()
    val keys = listOf(50, 30, 70, 20, 40, 60, 80, 35, 45, 65, 30, 50)
    for ((i, k) in keys.withIndex()) t.put(k, "v$i")
    println("size=${t.size} height=${t.height()}")
    println(t.inorder())
    println("get 35=${t[35]} 50=${t[50]} 99=${t[99]}")
    val ts = Tree<String, Int>()
    "the quick brown fox jumps over the lazy dog the end".split(" ").forEach { w -> ts.put(w, (ts[w] ?: 0) + 1) }
    println(ts.inorder())
    println("height=${ts.height()} the=${ts["the"]} cat=${ts["cat"]}")
}
