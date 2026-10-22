// Classes and inheritance: primary and secondary constructors, the order in
// which property initialisers and init blocks run down a class chain,
// open/abstract classes, interfaces with default methods and properties,
// overriding with super calls (including super<Iface> to pick between two
// defaults), protected members, custom getters/setters with backing fields,
// lateinit, and virtual dispatch through base-class and interface references.
//
// Kotlin/Wasm lowers a class to a GC struct whose first field is its vtable,
// an override to a call_ref through a vtable slot, an interface call to an
// itable lookup, and a super call to a direct call.  Every step prints, so a
// wrong slot, a field read at the wrong index or an init block run out of
// order shows up as a diff rather than as a wrong checksum.
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.roundToLong
import kotlin.math.sqrt

fun r3(x: Double): String {
    val v = (x * 1000).roundToLong()
    val a = abs(v)
    return (if (v < 0) "-" else "") + (a / 1000) + "." + (a % 1000).toString().padStart(3, '0')
}

interface Named {
    val name: String
    val title: String get() = "<" + name + ">"
    fun greet(): String = "hello from $name"
    fun tag(): String = "N"
}

interface Measured {
    fun size(): Int
    fun tag(): String = "M"
    fun bigger(other: Measured): Boolean = size() > other.size()
}

abstract class Shape(override val name: String) : Named, Measured {
    init { println("  Shape.init name=$name") }
    abstract fun area(): Double
    open fun perimeter(): Double = 0.0
    open fun describe(): String = "$name area=${r3(area())} perim=${r3(perimeter())}"
    override fun size(): Int = area().toInt()
    override fun tag(): String = super<Named>.tag() + super<Measured>.tag() + "S"
}

open class Rect(val w: Double, val h: Double, n: String = "rect") : Shape(n) {
    init { println("  Rect.init ${w}x$h") }
    override fun area() = w * h
    override fun perimeter() = 2 * (w + h)
    override fun describe() = "Rect[" + super.describe() + "]"
}

class Square(side: Double) : Rect(side, side, "square") {
    init { println("  Square.init") }
    override fun describe() = "Square{" + super.describe() + "}"
    override fun greet() = super.greet().uppercase()
    override fun tag() = super.tag() + "Q"
    override val title: String get() = "[" + super.title + "]"
}

class Circle(val r: Double) : Shape("circle") {
    override fun area() = PI * r * r
    override fun perimeter() = 2 * PI * r
}

class Tri(val a: Double, val b: Double, val c: Double) : Shape("tri") {
    override fun area(): Double {
        val s = (a + b + c) / 2
        return sqrt(s * (s - a) * (s - b) * (s - c))
    }
    override fun perimeter() = a + b + c
    override fun greet() = "tri says: " + super.greet()
}

// Initialisation order, including an open member read from the base
// constructor before the derived class has set its field.
fun trace(what: String, v: Int): Int { println("  init $what = $v"); return v }

open class Base(val x: Int) {
    val a = trace("Base.a", x)
    init { trace("Base.init1 sees v", v) }
    val b = trace("Base.b", a * 2)
    init { trace("Base.init2", b + 1) }
    open val v: Int get() = 1
    fun show() = "Base(x=$x a=$a b=$b v=$v)"
}

open class Mid(x: Int) : Base(x + 1) {
    val c = trace("Mid.c", x * 10)
    init { trace("Mid.init", c + a + b) }
    override val v: Int get() = c
}

class Leaf(x: Int, val extra: Int) : Mid(x * 2) {
    val d = trace("Leaf.d", extra + c)
    override val v: Int get() = d + super.v
}

class Point3 {
    val x: Int
    val y: Int
    val z: Int
    constructor(x: Int, y: Int, z: Int) {
        this.x = x; this.y = y; this.z = z
        println("  Point3(3) $x $y $z")
    }
    constructor(x: Int, y: Int) : this(x, y, -1) { println("  Point3(2)") }
    constructor(v: Int) : this(v, v * v) { println("  Point3(1)") }
    fun sum() = x + y + z
}

class Counter(start: Int) {
    var count = start
        private set
    var label: String = "c"
        set(value) { field = value.trim().lowercase(); changes++ }
    var changes = 0
    val doubled get() = count * 2
    var clamped: Int = 0
        set(value) { field = value.coerceIn(-10, 10) }
        get() = field * 3
    fun inc(by: Int = 1): Counter { count += by; return this }
}

class Holder {
    lateinit var s: String
    fun ready() = this::s.isInitialized
    fun read(): String = try { s } catch (e: RuntimeException) { "uninit:" + e.message }
}

open class Account(initial: Long) {
    protected var balance = initial
    private var ops = 0
    open fun deposit(x: Long) { balance += x; ops++ }
    open fun withdraw(x: Long): Boolean {
        if (x > balance) return false
        balance -= x; ops++; return true
    }
    fun report() = "${this::class.simpleName} balance=$balance ops=$ops"
}

class Savings(initial: Long, val ratePct: Int) : Account(initial) {
    override fun deposit(x: Long) { super.deposit(x); balance += x * ratePct / 100 }
    fun accrue() { balance += balance * ratePct / 100 }
}

class Checking(initial: Long, val overdraft: Long) : Account(initial) {
    override fun withdraw(x: Long): Boolean {
        if (x > balance + overdraft) return false
        balance -= x
        return true
    }
}

abstract class Animal {
    abstract val sound: String
    abstract val legs: Int
    open fun speak() = "${this::class.simpleName} says $sound on $legs legs"
}
class Dog : Animal() { override val sound = "woof"; override val legs = 4 }
class Bird : Animal() {
    override val sound get() = "tweet"
    override val legs = 2
    override fun speak() = super.speak() + " and flies"
}
open class Cat : Animal() { override val sound = "meow"; override val legs = 4 }
class Lion : Cat() { override val sound = "ROAR"; override fun speak() = super.speak().replace("Lion", "the lion") }

fun main() {
    println("== shapes ==")
    val shapes = listOf<Shape>(Rect(3.0, 4.0), Square(2.5), Circle(1.5), Tri(3.0, 4.0, 5.0), Rect(0.5, 9.0, "thin"))
    for (s in shapes) {
        println(s.describe())
        println("  greet=" + s.greet() + " title=" + s.title + " tag=" + s.tag() + " size=" + s.size())
    }
    val named: List<Named> = shapes
    println(named.joinToString { it.name })
    val measured: List<Measured> = shapes
    for (i in measured.indices) for (j in measured.indices)
        if (i < j) println("  bigger($i,$j)=${measured[i].bigger(measured[j])} bigger($j,$i)=${measured[j].bigger(measured[i])}")
    println("by area: " + shapes.sortedBy { it.area() }.joinToString { it.name })
    println("total perimeter " + r3(shapes.sumOf { it.perimeter() }))

    println("== init order ==")
    val b = Base(3)
    println(b.show())
    val m = Mid(4)
    println(m.show())
    val l = Leaf(5, 7)
    println(l.show() + " d=" + l.d + " c=" + l.c)
    val asBase: Base = l
    println("virtual v via Base: " + asBase.v)

    println("== secondary constructors ==")
    println(Point3(1, 2, 3).sum())
    println(Point3(4, 5).sum())
    println(Point3(6).sum())

    println("== getters/setters ==")
    val c = Counter(10)
    c.inc().inc(5).inc(-2)
    c.label = "  HeLLo "
    c.label = "World"
    println("count=${c.count} doubled=${c.doubled} label=${c.label} changes=${c.changes}")
    for (v in listOf(-50, -3, 0, 4, 99)) { c.clamped = v; println("  clamped($v)=${c.clamped}") }

    println("== lateinit ==")
    val h = Holder()
    println("ready=${h.ready()} read=${h.read()}")
    h.s = "set!"
    println("ready=${h.ready()} read=${h.read()}")

    println("== accounts ==")
    val accts = listOf(Account(100), Savings(1000, 5), Checking(50, 200))
    for ((i, a) in accts.withIndex()) {
        a.deposit(100L * (i + 1))
        println("  w300=" + a.withdraw(300) + " w5000=" + a.withdraw(5000))
        if (a is Savings) { a.accrue(); a.accrue() }
        println("  " + a.report())
    }

    println("== animals ==")
    val zoo = listOf(Dog(), Bird(), Cat(), Lion())
    zoo.forEach { println("  " + it.speak()) }
    println("legs=" + zoo.sumOf { it.legs })
    println("cats=" + zoo.count { it is Cat } + " lions=" + zoo.count { it is Lion })

    println("== identity ==")
    val p = Dog(); val q = Dog()
    println("p==q ${p == q} p===p ${p === p} p==p ${p.equals(p)}")
    val r1 = Rect(1.0, 2.0)
    val r2: Any = r1
    println("same=${r1 === r2} isShape=${r2 is Shape} isNamed=${r2 is Named} isCircle=${r2 is Circle}")
}
