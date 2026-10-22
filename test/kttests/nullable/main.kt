// Nullable types without `!!`: safe calls and chains of them, the elvis
// operator (with defaults, with `return`/`continue`/`throw` on the right),
// `as?` safe casts, smart casts after null checks on locals and `val`
// properties, the scope functions let/run/also/apply/with/takeIf/takeUnless,
// nullable primitives (boxed Int?/Long?/Double?/Char?/Boolean?), and the
// null-aware collection helpers (filterNotNull, mapNotNull, firstOrNull,
// getOrNull, orEmpty, isNullOrEmpty/Blank, singleOrNull, maxOrNull).
//
// A nullable reference in Kotlin/Wasm is a nullable wasm ref and a nullable
// primitive is a boxed struct or null, so a safe call is a ref.is_null branch
// and every Int? read is an unboxing struct.get behind a null test.
class Address(val street: String?, val city: String?, val zip: Int?)
class Company(val name: String, val address: Address?)
class Employee(val name: String, val company: Company?, val boss: Employee?, val salary: Int?)

fun cityOf(e: Employee?): String = e?.company?.address?.city ?: "unknown city"
fun bossChain(e: Employee?): String {
    val names = mutableListOf<String>()
    var cur = e
    while (cur != null) { names.add(cur.name); cur = cur.boss }
    return names.joinToString(" -> ").ifEmpty { "nobody" }
}
fun zipLen(e: Employee?): Int = e?.company?.address?.zip?.toString()?.length ?: -1

fun parsePositive(s: String?): Int? {
    val t = s?.trim() ?: return null
    val n = t.toIntOrNull() ?: return null
    return n.takeIf { it > 0 }
}

fun requireName(m: Map<String, String?>, k: String): String =
    m[k] ?: throw IllegalStateException("missing $k")

fun lengthOrZero(x: Any?): Int = (x as? String)?.length ?: (x as? List<*>)?.size ?: 0

fun sumNullable(xs: List<Int?>): Int {
    var s = 0
    for (x in xs) {
        val v = x ?: continue
        s += v
    }
    return s
}

fun describe(x: Int?): String = when (x) {
    null -> "none"
    in Int.MIN_VALUE..-1 -> "neg $x"
    0 -> "zero"
    else -> "pos ${x + 1}"
}

class Node(val v: Int, val next: Node?)

fun build(vararg xs: Int): Node? {
    var head: Node? = null
    for (i in xs.indices.reversed()) head = Node(xs[i], head)
    return head
}
fun Node?.len(): Int = if (this == null) 0 else 1 + next.len()
fun Node?.sum(): Int = this?.let { it.v + it.next.sum() } ?: 0
fun Node?.render(): String = this?.let { "${it.v}" + (it.next?.let { n -> "," + n.render() } ?: "") } ?: "[]"
fun Node?.nth(i: Int): Int? = if (this == null) null else if (i == 0) v else next.nth(i - 1)

class Config {
    var name: String? = null
    var retries: Int? = null
    var verbose: Boolean? = null
    override fun toString() = "Config(name=$name, retries=$retries, verbose=$verbose)"
}

fun main() {
    println("== chains ==")
    val ceo = Employee("Zed", Company("Acme", Address("1 Main", "Springfield", 12345)), null, 500)
    val mgr = Employee("Yve", Company("Acme", Address(null, null, null)), ceo, null)
    val dev = Employee("Xan", Company("Startup", null), mgr, 90)
    val free = Employee("Wil", null, null, 40)
    val people: List<Employee?> = listOf(ceo, mgr, dev, free, null)
    for (p in people) {
        println("  ${p?.name ?: "<null>"} city=${cityOf(p)} chain=${bossChain(p)} zipLen=${zipLen(p)}" +
            " street=${p?.company?.address?.street?.uppercase()} company=${p?.company?.name?.length}" +
            " salary=${p?.salary?.times(12) ?: "n/a"} bossSalary=${p?.boss?.salary}")
    }
    println("payroll=${people.sumOf { it?.salary ?: 0 }} known=${people.mapNotNull { it?.salary }}")
    println("with boss=${people.filter { it?.boss != null }.map { it?.name }}")

    println("== elvis / parse ==")
    for (s in listOf("12", " 7 ", "-3", "0", "abc", "", null, "2147483647", "2147483648")) {
        val p = parsePositive(s)
        println("  '${s}' -> $p doubled=${p?.let { it * 2 }} desc=${describe(p)} or=${p ?: -1}")
    }
    val m = mapOf("a" to "x", "b" to null)
    for (k in listOf("a", "b", "c")) {
        val r = try { requireName(m, k) } catch (e: IllegalStateException) { "error: ${e.message}" }
        println("  $k: $r containsKey=${m.containsKey(k)} get=${m[k]} getOrElse=${m.getOrElse(k) { "dflt" }}")
    }
    println("sumNullable=${sumNullable(listOf(1, null, 2, null, null, 39))}")
    for (x in listOf<Any?>("hello", listOf(1, 2, 3), 42, null, "")) println("  lengthOrZero($x)=${lengthOrZero(x)}")

    println("== safe casts ==")
    val things: List<Any?> = listOf(1, "s", 2.5, null, 'c', 10L, listOf("a"), Node(1, null))
    for (t in things) {
        val i = t as? Int
        val s = t as? String
        val n = t as? Number
        val c = t as? Comparable<*>
        println("  $i ${s?.plus("!")} ${n?.toInt()} ${c != null} ${(t as? Node)?.v}")
    }

    println("== scope functions ==")
    val cfg = Config().apply { name = "svc"; retries = 3 }
    println(cfg)
    val cfg2 = Config().also { it.verbose = true }.apply { retries = (retries ?: 0) + 5 }
    println(cfg2)
    val summary = with(cfg) { "$name/${retries ?: 0}/${verbose ?: false}" }
    println(summary)
    val r = cfg.run { name?.length?.plus(retries ?: 0) }
    println("run=$r")
    for (v in listOf(3, 8, 15, -2)) {
        println("  $v takeIf even=${v.takeIf { it % 2 == 0 }} takeUnless neg=${v.takeUnless { it < 0 }}" +
            " let=${v.takeIf { it > 5 }?.let { "big $it" } ?: "small"}")
    }
    var calls = 0
    val nameLen = cfg.name?.let { calls++; it.length } ?: run { calls += 100; 0 }
    val noLen = Config().name?.let { calls++; it.length } ?: run { calls += 100; 0 }
    println("nameLen=$nameLen noLen=$noLen calls=$calls")

    println("== nullable primitives ==")
    val ints: List<Int?> = listOf(5, null, -7, 0, null, Int.MAX_VALUE)
    println("${ints.filterNotNull()} max=${ints.filterNotNull().maxOrNull()} nulls=${ints.count { it == null }}")
    println("sorted=${ints.sortedWith(nullsFirst(naturalOrder()))} last=${ints.sortedWith(nullsLast(reverseOrder()))}")
    val longs: Array<Long?> = arrayOf(1L, null, Long.MIN_VALUE)
    println(longs.map { it?.let { l -> l / 2 } })
    val doubles = listOf<Double?>(1.5, null, -0.0, Double.NaN)
    println(doubles.map { it?.plus(1.0) })
    val chars = "a1b2".map { c -> if (c.isDigit()) null else c }
    println("$chars ${chars.filterNotNull().joinToString("")}")
    val bools = listOf<Boolean?>(true, null, false)
    println(bools.map { it == true })
    var a: Int? = 41
    var b: Int? = 41
    println("eq=${a == b} plus=${a?.plus(1)}")
    b = null
    println("eq=${a == b} cmp=${compareValues(a, b)} ${compareValues(b, a)} ${compareValues(b, null as Int?)}")
    a = null
    println("both null eq=${a == b}")

    println("== collections ==")
    val words: List<String?> = listOf("apple", null, "", "  ", "kiwi", null)
    for (w in words) println("  '$w' empty=${w.isNullOrEmpty()} blank=${w.isNullOrBlank()} orEmpty='${w.orEmpty()}' len=${w?.length}")
    println("first=${words.firstOrNull()} firstNull=${words.firstOrNull { it == null }} lastNonNull=${words.lastOrNull { it != null }}")
    println("getOrNull 1=${words.getOrNull(1)} 10=${words.getOrNull(10)} single=${listOf(1).singleOrNull()} ${listOf(1, 2).singleOrNull()}")
    println("emptyMax=${emptyList<Int>().maxOrNull()} find=${(1..10).find { it * it > 50 }} none=${(1..3).find { it > 5 }}")
    val nullableList: List<Int>? = null
    println("orEmpty=${nullableList.orEmpty()} size=${nullableList?.size} sum=${nullableList?.sum() ?: 0}")

    println("== linked list ==")
    val lists = listOf(build(), build(1), build(1, 2, 3), build(5, -5, 10, 20))
    for (l in lists) println("  ${l.render()} len=${l.len()} sum=${l.sum()} nth2=${l.nth(2)} head=${l?.v} second=${l?.next?.v}")
}
