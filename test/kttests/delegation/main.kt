// Delegation: `by lazy` (evaluated once, on first read, in dependency order),
// Delegates.observable/vetoable/notNull, hand-written ReadOnlyProperty and
// ReadWriteProperty delegates that log every get and set with the property's
// name, properties delegated to a Map, delegating to another property
// (`by this::other`), `provideDelegate`, local delegated properties, and
// interface delegation with `by` (a decorator that overrides some members of
// the delegated interface and forwards the rest).
//
// A delegated property is a hidden field holding the delegate object plus
// accessors that call getValue/setValue on it with a KProperty reference, so
// these are virtual calls through generic interfaces, and lazy initialisers
// are closures run behind a "not yet initialised" sentinel test.
import kotlin.properties.Delegates
import kotlin.properties.PropertyDelegateProvider
import kotlin.properties.ReadOnlyProperty
import kotlin.properties.ReadWriteProperty
import kotlin.reflect.KProperty

val events = mutableListOf<String>()
fun log(s: String) { events.add(s); println("  [$s]") }

class Logged<T>(private var value: T) : ReadWriteProperty<Any?, T> {
    var reads = 0
    var writes = 0
    override fun getValue(thisRef: Any?, property: KProperty<*>): T { reads++; log("get ${property.name}=$value"); return value }
    override fun setValue(thisRef: Any?, property: KProperty<*>, value: T) { writes++; log("set ${property.name}: ${this.value} -> $value"); this.value = value }
}

class Clamped(private val lo: Int, private val hi: Int, init: Int) {
    private var v = init.coerceIn(lo, hi)
    operator fun getValue(thisRef: Any?, p: KProperty<*>) = v
    operator fun setValue(thisRef: Any?, p: KProperty<*>, nv: Int) { v = nv.coerceIn(lo, hi) }
}

fun constant(x: Int) = ReadOnlyProperty<Any?, Int> { _, p -> x + p.name.length }

class Upper : ReadWriteProperty<Any?, String> {
    private var s = ""
    override fun getValue(thisRef: Any?, property: KProperty<*>) = s
    override fun setValue(thisRef: Any?, property: KProperty<*>, value: String) { s = value.uppercase() }
}

class Registered(private val registry: MutableList<String>) : PropertyDelegateProvider<Any?, ReadOnlyProperty<Any?, String>> {
    override fun provideDelegate(thisRef: Any?, property: KProperty<*>): ReadOnlyProperty<Any?, String> {
        registry.add(property.name)
        val id = registry.size
        return ReadOnlyProperty { _, p -> "${p.name}#$id" }
    }
}

class Settings(registry: MutableList<String>) {
    var volume by Clamped(0, 10, 5)
    var title by Upper()
    val answer by constant(40)
    val longerName by constant(0)
    val first by Registered(registry)
    val second by Registered(registry)
    var counter: Int by Logged(0)
    var name: String by Delegates.observable("init") { p, old, new -> log("observed ${p.name} '$old' -> '$new'") }
    var even: Int by Delegates.vetoable(0) { _, _, new -> new % 2 == 0 }
    var late: String by Delegates.notNull()
    var alias by this::volume
}

class Lazies {
    var computeCount = 0
    val a: Int by lazy { computeCount++; log("compute a"); 6 }
    val b: Int by lazy { computeCount++; log("compute b from a"); a * 7 }
    val c: String by lazy { computeCount++; log("compute c from a,b"); "a=$a b=$b" }
    val big: List<Int> by lazy { computeCount++; log("compute big"); (1..20).map { it * it } }
}

class Record(map: Map<String, Any?>) {
    val name: String by map
    val age: Int by map
    val tags: List<String> by map
}

class MutableRecord(val map: MutableMap<String, Any?>) {
    var score: Int by map
    var label: String by map
}

interface Store {
    fun put(k: String, v: Int)
    fun get(k: String): Int?
    fun keys(): List<String>
    fun describe(): String = "Store(${keys().size})"
}

class MapStore : Store {
    private val m = LinkedHashMap<String, Int>()
    override fun put(k: String, v: Int) { m[k] = v }
    override fun get(k: String) = m[k]
    override fun keys() = m.keys.toList()
}

class CountingStore(private val inner: Store) : Store by inner {
    var puts = 0
    var gets = 0
    override fun put(k: String, v: Int) { puts++; inner.put(k, v * 10) }
    override fun get(k: String): Int? { gets++; return inner.get(k) }
    override fun describe() = "Counting(" + inner.describe() + " puts=$puts gets=$gets)"
}

class PrefixStore(private val prefix: String, inner: Store) : Store by inner {
    override fun keys() = listOf(prefix) + get(prefix).toString().let { listOf(it) }
}

class LoggingList<T>(private val inner: MutableList<T> = ArrayList()) : MutableList<T> by inner {
    var adds = 0
    override fun add(element: T): Boolean { adds++; log("add $element"); return inner.add(element) }
}

fun main() {
    println("== Settings ==")
    val registry = mutableListOf<String>()
    val s = Settings(registry)
    println("registry after construction $registry")
    println("volume=${s.volume} answer=${s.answer} longerName=${s.longerName} first=${s.first} second=${s.second}")
    for (v in listOf(3, -5, 99, 7)) { s.volume = v; println("  volume($v) -> ${s.volume} alias=${s.alias}") }
    s.alias = 1
    println("alias set -> volume=${s.volume}")
    s.title = "hello world"
    println("title=${s.title}")
    s.counter = 1
    s.counter += 10
    s.counter = s.counter * 2
    println("counter=${s.counter}")
    s.name = "alpha"
    s.name = "beta"
    s.name = s.name + "!"
    for (e in listOf(2, 3, 8, 11, 0, -4)) { s.even = e; println("  even <- $e : ${s.even}") }
    try { println(s.late) } catch (e: IllegalStateException) { println("late: ${e.message}") }
    s.late = "now set"
    println("late=${s.late}")

    println("== lazy ==")
    val lz = Lazies()
    println("nothing computed: ${lz.computeCount}")
    println("c=${lz.c} count=${lz.computeCount}")
    println("c again=${lz.c} b=${lz.b} a=${lz.a} count=${lz.computeCount}")
    println("big sum=${lz.big.sum()} again=${lz.big.size} count=${lz.computeCount}")
    val local by lazy { log("local lazy"); 123 }
    println("before local")
    println("local=${local + local}")
    var localObserved by Delegates.observable(0) { _, o, n -> log("local $o->$n") }
    localObserved = 5; localObserved++
    println("localObserved=$localObserved")

    println("== map delegation ==")
    val r = Record(mapOf("name" to "Ann", "age" to 41, "tags" to listOf("x", "y")))
    println("${r.name} ${r.age + 1} ${r.tags}")
    val backing = linkedMapOf<String, Any?>("score" to 10, "label" to "tag1")
    val mr = MutableRecord(backing)
    mr.score += 5
    mr.label = mr.label.reversed()
    println("${mr.score} ${mr.label} $backing")
    backing["score"] = 99
    println("after backing change ${mr.score}")

    println("== interface delegation ==")
    val cs = CountingStore(MapStore())
    for ((i, k) in listOf("a", "b", "c", "a").withIndex()) cs.put(k, i)
    println("${cs.get("a")} ${cs.get("b")} ${cs.get("zz")} keys=${cs.keys()}")
    println(cs.describe())
    val ps = PrefixStore("a", cs)
    println("prefix keys=${ps.keys()} desc=${ps.describe()} get=${ps.get("c")}")
    println(cs.describe())
    val ll = LoggingList<Int>()
    ll.add(3); ll.add(1); ll.add(2)
    ll.addAll(listOf(9, 8))   // forwarded, not counted
    ll.sort()
    println("list=${ll.toList()} adds=${ll.adds} size=${ll.size} contains9=${9 in ll} idx=${ll.indexOf(8)}")
    ll.removeAt(0); ll[0] = 100
    println("list=${ll.toList()}")

    println("== event log ==")
    println("${events.size} events, first=${events.first()} last=${events.last()}")
    println(events.groupingBy { it.substringBefore(' ') }.eachCount())
}
