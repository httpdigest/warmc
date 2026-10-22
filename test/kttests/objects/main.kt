// Objects and nesting: `object` singletons with mutable state and lazy first
// use, companion objects (factories, constants, a companion implementing an
// interface, private constructors reached only through the companion), nested
// classes versus inner classes that capture their outer instance (and
// this@Outer), anonymous objects implementing interfaces and capturing locals,
// local classes inside functions, and objects extending abstract classes.
//
// An inner class is a struct holding a reference to its outer struct and an
// anonymous object is a struct holding its captured locals, so every capture
// is a field access through a chain of GC references; the prints check each
// link of the chain.
interface Shape { fun area(): Int; fun name(): String = "shape" }

object Registry {
    private val items = mutableListOf<String>()
    var lookups = 0
    init { println("  Registry initialised") }
    fun register(s: String): Int { items.add(s); return items.size - 1 }
    fun get(i: Int): String { lookups++; return items.getOrElse(i) { "?" } }
    val size get() = items.size
    override fun toString() = "Registry$items"
}

object IdGen {
    private var next = 100
    fun next() = next++
}

class User private constructor(val id: Int, val name: String) {
    companion object Factory {
        const val MAX_NAME = 8
        private var created = 0
        fun create(name: String): User { created++; return User(IdGen.next(), name.take(MAX_NAME)) }
        fun createdCount() = created
    }
    override fun toString() = "User#$id($name)"
}

interface Parser<T> { fun parse(s: String): T }

class Temp(val celsius: Double) {
    companion object : Parser<Temp> {
        override fun parse(s: String): Temp {
            val v = s.dropLast(1).toDouble()
            return if (s.endsWith("F")) Temp((v - 32) * 5 / 9) else Temp(v)
        }
        val FREEZING = Temp(0.0)
    }
    override fun toString() = "Temp(${(celsius * 100).toLong() / 100.0}C)"
}

fun <T> parseAll(p: Parser<T>, xs: List<String>): List<T> = xs.map { p.parse(it) }

class Outer(val label: String) {
    private var counter = 0
    val inners = mutableListOf<Inner>()

    class Nested(val v: Int) { fun twice() = v * 2 }

    inner class Inner(val k: Int) {
        val id = ++counter
        fun describe() = "Inner(k=$k id=$id of $label counter=$counter)"
        fun bump() { counter += k }
        inner class Deeper(val z: Int) {
            fun path() = "$label/$k/$z sum=${counter + k + z} outer=${this@Outer.label} inner=${this@Inner.k}"
        }
    }

    fun make(k: Int): Inner { val i = Inner(k); inners.add(i); return i }
    fun total() = counter
}

abstract class Greeter(val greeting: String) {
    abstract fun target(): String
    fun greet() = "$greeting, ${target()}!"
}

object World : Greeter("Hello") { override fun target() = "world" }
object Moon : Greeter("Goodnight") {
    var calls = 0
    override fun target(): String { calls++; return "moon #$calls" }
}

fun counterObject(start: Int): Iterator<Int> {
    var n = start
    return object : Iterator<Int> {
        override fun hasNext() = n < start + 5
        override fun next(): Int { val v = n; n += 1; return v * v }
    }
}

fun shapes(scale: Int): List<Shape> {
    class Sq(val s: Int) : Shape {
        override fun area() = s * s * scale
        override fun name() = "sq$s"
    }
    var made = 0
    val list = mutableListOf<Shape>()
    for (i in 1..4) {
        list.add(Sq(i))
        list.add(object : Shape {
            val id = ++made
            override fun area() = i * scale + id
        })
    }
    println("  made $made anon shapes")
    return list
}

fun main() {
    println("== before first use ==")
    println("  main started")
    val a = Registry.register("alpha")
    val b = Registry.register("beta")
    Registry.register("gamma")
    println("  $a $b ${Registry.get(1)} ${Registry.get(7)} size=${Registry.size} lookups=${Registry.lookups}")
    println("  $Registry")
    println("  same object ${Registry === Registry}")

    println("== companion ==")
    val users = listOf("ann", "bartholomew", "cy").map { User.create(it) }
    println("  $users created=${User.createdCount()} max=${User.MAX_NAME}")
    println("  more ${User.Factory.create("dee")} ${User.createdCount()} next id ${IdGen.next()}")
    println("  temps ${parseAll(Temp, listOf("100C", "212F", "-40F", "37.5C"))} freezing=${Temp.FREEZING}")
    val p: Parser<Temp> = Temp.Companion
    println("  via iface ${p.parse("98.6F")}")

    println("== nested / inner ==")
    println("  nested ${Outer.Nested(21).twice()}")
    val o = Outer("O1")
    val i1 = o.make(3)
    val i2 = o.make(5)
    println("  ${i1.describe()} | ${i2.describe()}")
    i1.bump(); i2.bump(); i2.bump()
    println("  after bumps ${i1.describe()} total=${o.total()}")
    val d = i2.Deeper(7)
    println("  ${d.path()}")
    val o2 = Outer("O2")
    val j = o2.Inner(11)
    println("  ${j.describe()} ${j.Deeper(1).path()} o1 total still ${o.total()}")
    for (k in 1..5) o.make(k).bump()
    println("  inners=${o.inners.size} total=${o.total()} ids=${o.inners.map { it.id }}")

    println("== object inheritance ==")
    val greeters: List<Greeter> = listOf(World, Moon, Moon, World, Moon)
    greeters.forEach { println("  " + it.greet()) }
    println("  moon calls ${Moon.calls}")

    println("== anonymous objects ==")
    val it1 = counterObject(3)
    val got = mutableListOf<Int>()
    while (it1.hasNext()) got.add(it1.next())
    println("  $got")
    for (s in shapes(2)) println("  ${s.name()} area=${s.area()}")
    println("  total ${shapes(3).sumOf { it.area() }}")

    var clicks = 0
    val listeners = mutableListOf<() -> Unit>()
    val handler = object {
        var local = 0
        fun click(n: Int) { local += n; clicks += n * 10 }
    }
    for (n in 1..4) listeners.add { handler.click(n) }
    listeners.forEach { it() }
    listeners.reversed().forEach { it() }
    println("  local=${handler.local} clicks=$clicks")

    val cmp = object : Comparator<String> {
        override fun compare(a: String, b: String): Int =
            if (a.length != b.length) a.length - b.length else b.compareTo(a)
    }
    println("  " + listOf("pear", "fig", "banana", "kiwi", "apple", "date", "plum").sortedWith(cmp))
}
