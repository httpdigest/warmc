// Enum classes: constructor properties, methods, per-constant bodies that
// override an abstract member, enums implementing interfaces, a companion
// object with lookups, `entries`/`values()`/`valueOf`/`enumValueOf`, `name`
// and `ordinal`, natural ordering, exhaustive `when` over an enum without
// `else`, and enums as map keys and set members.
//
// Each constant is a singleton struct created by the enum's static
// initialiser; a constant with a body is an anonymous subclass with its own
// vtable.  The tests below run a small stack machine and a traffic-light
// simulation whose every step prints, so a wrong vtable or ordinal shows.
interface Pretty { fun pretty(): String }

enum class Color(val rgb: Int) : Pretty {
    RED(0xFF0000), GREEN(0x00FF00), BLUE(0x0000FF), WHITE(0xFFFFFF), BLACK(0);

    val r get() = (rgb shr 16) and 0xFF
    val g get() = (rgb shr 8) and 0xFF
    val b get() = rgb and 0xFF
    fun hex() = "#" + rgb.toString(16).padStart(6, '0')
    override fun pretty() = name.lowercase().replaceFirstChar { it.uppercase() } + "(" + hex() + ")"
    fun complement(): Color = when (this) {
        RED -> GREEN; GREEN -> BLUE; BLUE -> RED; WHITE -> BLACK; BLACK -> WHITE
    }

    companion object {
        fun fromRgb(v: Int): Color? = entries.firstOrNull { it.rgb == v }
        val brightest get() = entries.maxByOrNull { it.r + it.g + it.b }
    }
}

enum class Op(val sym: String, val prec: Int) {
    ADD("+", 1) { override fun apply(a: Long, b: Long) = a + b },
    SUB("-", 1) { override fun apply(a: Long, b: Long) = a - b },
    MUL("*", 2) { override fun apply(a: Long, b: Long) = a * b },
    DIV("/", 2) {
        override fun apply(a: Long, b: Long) = if (b == 0L) 0L else a / b
        override fun describe() = "safe " + super.describe()
    },
    MOD("%", 2) { override fun apply(a: Long, b: Long) = if (b == 0L) 0L else a % b },
    POW("^", 3) {
        override fun apply(a: Long, b: Long): Long { var r = 1L; repeat(b.toInt().coerceIn(0, 62)) { r *= a }; return r }
    };

    abstract fun apply(a: Long, b: Long): Long
    open fun describe() = "$name '$sym' prec=$prec ord=$ordinal"
}

enum class Dir(val dx: Int, val dy: Int) {
    N(0, -1), E(1, 0), S(0, 1), W(-1, 0);
    fun turnRight() = entries[(ordinal + 1) % entries.size]
    fun turnLeft() = entries[(ordinal + 3) % entries.size]
    fun opposite() = entries[(ordinal + 2) % 4]
}

enum class Light(val seconds: Int) {
    RED(4) { override fun next() = GREEN },
    GREEN(3) { override fun next() = YELLOW },
    YELLOW(1) { override fun next() = RED };
    abstract fun next(): Light
}

enum class Planet(val massE24: Double, val radiusKm: Double) {
    MERCURY(0.330, 2439.7), VENUS(4.87, 6051.8), EARTH(5.97, 6371.0), MARS(0.642, 3389.5),
    JUPITER(1898.0, 69911.0), SATURN(568.0, 58232.0), URANUS(86.8, 25362.0), NEPTUNE(102.0, 24622.0);
    fun density(): Double = massE24 * 1e24 / (4.0 / 3.0 * kotlin.math.PI * (radiusKm * 1000).let { it * it * it })
}

enum class Empty

inline fun <reified E : Enum<E>> names(): String = enumValues<E>().joinToString("|") { it.name }
inline fun <reified E : Enum<E>> parseOr(s: String, d: E): E = try { enumValueOf<E>(s) } catch (e: IllegalArgumentException) { d }

fun rpn(tokens: List<String>): Long {
    val st = ArrayDeque<Long>()
    for (t in tokens) {
        val op = Op.entries.firstOrNull { it.sym == t }
        if (op == null) st.addLast(t.toLong())
        else { val b = st.removeLast(); val a = st.removeLast(); st.addLast(op.apply(a, b)) }
    }
    return st.last()
}

fun main() {
    println("== Color ==")
    for (c in Color.entries) println("  ${c.ordinal} ${c.name} ${c.pretty()} r=${c.r} g=${c.g} b=${c.b} comp=${c.complement()} toString=$c")
    println("values=${Color.values().size} valueOf=${Color.valueOf("BLUE").hex()}")
    println("fromRgb ${Color.fromRgb(0x00FF00)} ${Color.fromRgb(123)} brightest=${Color.brightest}")
    println("compare RED<BLUE ${Color.RED < Color.BLUE} WHITE.compareTo(GREEN)=${Color.WHITE.compareTo(Color.GREEN)}")
    println("sorted desc " + Color.entries.sortedDescending())
    println("by name " + Color.entries.sortedBy { it.name })
    val pretties: List<Pretty> = Color.entries
    println(pretties.joinToString { it.pretty() })
    try { Color.valueOf("PURPLE") } catch (e: IllegalArgumentException) { println("valueOf PURPLE failed") }
    println(names<Color>() + " / " + names<Dir>() + " / [" + names<Empty>() + "]")
    println("parseOr ${parseOr("E", Dir.N)} ${parseOr("Q", Dir.W)} ${parseOr("GREEN", Light.RED)}")
    println("same instance ${Color.valueOf("RED") === Color.RED} eq ${Color.entries[0] == Color.RED}")

    println("== Op ==")
    for (op in Op.entries) {
        println("  " + op.describe() + " " + listOf(17L to 5L, -17L to 5L, 2L to 10L, 7L to 0L).map { (a, b) -> op.apply(a, b) })
    }
    val progs = listOf("3 4 + 2 *", "5 1 2 + 4 * + 3 -", "2 3 ^ 2 ^", "100 7 % 3 /", "2 62 ^ 3 *", "1 0 /")
    for (p in progs) println("  rpn '$p' = ${rpn(p.split(" "))}")
    val byPrec = Op.entries.groupBy { it.prec }
    for (k in byPrec.keys.sorted()) println("  prec $k: ${byPrec[k]}")

    println("== Dir walk ==")
    var d = Dir.N; var x = 0; var y = 0
    val visited = LinkedHashMap<Dir, Int>()
    for (i in 1..24) {
        d = when {
            i % 5 == 0 -> d.opposite()
            i % 3 == 0 -> d.turnLeft()
            else -> d.turnRight()
        }
        x += d.dx * i; y += d.dy * i
        visited[d] = (visited[d] ?: 0) + 1
        println("  step $i dir=$d pos=($x,$y)")
    }
    println("  visits " + visited)
    val enumSet = Dir.entries.filter { it.dx != 0 }.toSet()
    println("  horizontal $enumSet contains E=${Dir.E in enumSet} N=${Dir.N in enumSet}")

    println("== Light ==")
    var l = Light.RED; var t = 0
    val time = IntArray(3)
    while (t < 40) {
        time[l.ordinal] += l.seconds
        t += l.seconds
        println("  t=$t ${l.name}->${l.next()}")
        l = l.next()
    }
    println("  time per light " + Light.entries.zip(time.toList()))

    println("== Planet ==")
    for (p in Planet.entries) {
        val dens = p.density()
        println("  ${p.name.padEnd(8)} density=${(dens * 10).toLong() / 10.0} rocky=${dens > 3000}")
    }
    println("  heaviest=${Planet.entries.maxByOrNull { it.massE24 }} smallest=${Planet.entries.minByOrNull { it.radiusKm }}")
    val ranks = Planet.entries.sortedByDescending { it.density() }.mapIndexed { i, p -> "${i + 1}:${p.name.take(3)}" }
    println("  " + ranks)
}
