// Collections: List/MutableList/Set/Map construction and mutation, and the
// standard operators over them -- map/filter/fold/reduce/scan, groupBy,
// associate/associateBy/associateWith, zip/unzip, chunked/windowed,
// sortedBy/sortedWith with compound comparators, partition, flatMap/flatten,
// distinct, take/drop(While), any/all/none/count, min/max/sumOf, set algebra,
// map views and getOrPut, groupingBy().eachCount()/fold, ArrayDeque, and
// buildList/buildMap.
//
// Kotlin/Wasm's ArrayList is a GC struct around a growable array of anyref,
// and its HashMap/LinkedHashMap are written in Kotlin over arrays of keys,
// values and hash chains, so these exercise boxing, array copies on growth,
// hash folding and lots of short-lived iterators and lambdas.  Maps are only
// ever iterated in insertion (LinkedHashMap) or sorted order.
data class Emp(val name: String, val dept: String, val salary: Int, val age: Int)

fun main() {
    val emps = listOf(
        Emp("ann", "eng", 120, 34), Emp("bob", "eng", 95, 25), Emp("cat", "ops", 70, 41),
        Emp("dan", "ops", 82, 29), Emp("eve", "sales", 60, 23), Emp("fay", "eng", 150, 45),
        Emp("gus", "sales", 75, 38), Emp("hal", "hr", 65, 52), Emp("ivy", "eng", 110, 31))

    println("== list basics ==")
    val ml = mutableListOf(5, 3, 8)
    ml.add(1); ml.add(0, 9); ml.addAll(listOf(7, 7, 2)); ml.addAll(2, listOf(-1, -2))
    println("$ml size=${ml.size} idx7=${ml.indexOf(7)} last7=${ml.lastIndexOf(7)} has8=${8 in ml}")
    ml.remove(7); ml.removeAt(0); ml[0] = 42
    println("$ml sub=${ml.subList(2, 5)} first=${ml.first()} last=${ml.last()}")
    ml.removeAll { it < 0 }; ml.retainAll { it != 2 }
    println("$ml")
    ml.sort(); println("sorted $ml"); ml.sortDescending(); println("desc $ml"); ml.reverse(); println("rev $ml")
    ml.shuffle(kotlin.random.Random(7)); println("shuffled $ml")
    println("sortedWith ${ml.sortedWith(compareBy<Int> { it % 3 }.thenByDescending { it })}")
    ml.clear(); println("cleared $ml ${ml.isEmpty()}")
    val big = MutableList(50) { it * 3 % 17 }
    println("big ${big.joinToString(",")} distinct=${big.distinct().size}")
    println("binarySearch ${(0..20 step 2).toList().let { l -> listOf(-1, 0, 7, 8, 20, 21).map { l.binarySearch(it) } }}")

    println("== transform ==")
    val xs = (1..12).toList()
    println("map ${xs.map { it * it }} mapIndexed ${xs.mapIndexed { i, v -> i * v }.takeLast(4)}")
    println("filter ${xs.filter { it % 3 == 0 }} not ${xs.filterNot { it % 3 == 0 }} indexed ${xs.filterIndexed { i, _ -> i % 4 == 0 }}")
    println("fold ${xs.fold(0) { a, b -> a * 2 + b }} foldRight ${xs.foldRight("") { v, acc -> acc + v % 10 }}")
    println("reduce ${xs.reduce { a, b -> a * b % 1000003 }} reduceRight ${xs.reduceRight { v, acc -> v - acc }}")
    println("scan ${xs.take(6).scan(1) { a, b -> a * b }} runningReduce ${xs.take(6).runningReduce { a, b -> a + b }}")
    println("sum=${xs.sum()} avg=${xs.average()} sumOf=${xs.sumOf { it.toLong() * it }} min=${xs.minOrNull()} maxOf=${xs.maxOf { (it * 7) % 11 }}")
    println("any=${xs.any { it > 11 }} all=${xs.all { it > 0 }} none=${xs.none { it == 0 }} count=${xs.count { it and 1 == 1 }}")
    println("take ${xs.take(3)} drop ${xs.drop(9)} takeWhile ${xs.takeWhile { it * it < 30 }} dropWhile ${xs.dropWhile { it < 10 }} takeLastWhile ${xs.takeLastWhile { it > 9 }}")
    println("chunked ${xs.chunked(5)} sums ${xs.chunked(5) { it.sum() }}")
    println("windowed ${xs.windowed(4, 3)} partial ${xs.windowed(5, 5, partialWindows = true)} avg ${xs.windowed(3) { it.average() }.take(4)}")
    println("zip ${xs.zip(xs.drop(1)).take(4)} zipWith ${xs.zip(xs.reversed()) { a, b -> a * b }} next ${xs.zipWithNext { a, b -> b - a }.distinct()}")
    val (evens, odds) = xs.partition { it % 2 == 0 }
    println("partition $evens $odds")
    println("flatMap ${listOf(1, 2, 3).flatMap { n -> List(n) { n * 10 + it } }} flatten ${listOf(listOf(1), emptyList(), listOf(2, 3)).flatten()}")
    println("withIndex ${listOf("a", "b", "c").withIndex().map { (i, v) -> "$i$v" }} reversed ${xs.asReversed().take(3)}")
    println("distinctBy ${listOf("apple", "avocado", "banana", "blueberry", "cherry").distinctBy { it.first() }}")
    println("joinToString ${xs.joinToString(prefix = "<", postfix = ">", separator = "|", limit = 5, truncated = "..")}")
    println("joinToString ${xs.joinToString(" ") { if (it % 2 == 0) "e$it" else "o$it" }}")

    println("== employees ==")
    println("by salary ${emps.sortedBy { it.salary }.map { it.name }}")
    println("by dept, -salary ${emps.sortedWith(compareBy<Emp> { it.dept }.thenByDescending { it.salary }).map { "${it.dept}:${it.name}" }}")
    val byDept = emps.groupBy { it.dept }
    for (d in byDept.keys.sorted()) {
        val es = byDept.getValue(d)
        println("  $d n=${es.size} total=${es.sumOf { it.salary }} avgAge=${es.map { it.age }.average()} oldest=${es.maxByOrNull { it.age }?.name} names=${es.map { it.name }}")
    }
    println("groupBy value ${emps.groupBy({ it.age / 10 * 10 }, { it.name })}")
    println("associateBy ${emps.associateBy { it.name }.getValue("eve")} associate ${emps.take(3).associate { it.name to it.salary }}")
    println("associateWith ${listOf("a", "bb", "ccc").associateWith { it.length }}")
    println("eachCount ${emps.groupingBy { it.dept }.eachCount()}")
    println("fold ${emps.groupingBy { it.dept }.fold(0) { acc, e -> acc + e.salary }}")
    println("aggregate ${emps.groupingBy { it.age >= 35 }.aggregate { _, acc: String?, e, first -> if (first) e.name else acc + "+" + e.name }}")
    println("min/max ${emps.minByOrNull { it.salary }?.name} ${emps.maxOf { it.salary }} ${emps.minOf { it.age }}")
    println("top3 ${emps.sortedByDescending { it.salary }.take(3).map { it.name }} rank of ivy ${emps.sortedByDescending { it.salary }.indexOfFirst { it.name == "ivy" }}")
    val (names, salaries) = emps.map { it.name to it.salary }.unzip()
    println("unzip $names $salaries")

    println("== sets ==")
    val a = setOf(1, 2, 3, 4, 5, 6)
    val b = (4..9).toSet()
    println("union ${a union b} intersect ${a intersect b} subtract ${a subtract b} ${b - a} plus ${a + 10} minus ${a - 3}")
    println("contains ${3 in a} ${10 in a} containsAll ${a.containsAll(listOf(1, 6))} sizes ${a.size} ${b.size}")
    val ms = linkedSetOf("q", "w", "e")
    println("add ${ms.add("w")} ${ms.add("r")} remove ${ms.remove("q")} ${ms.remove("z")} $ms")
    val hs = HashSet<Int>()
    for (i in 0 until 1000) hs.add((i * 7919) % 101)
    println("hashset size ${hs.size} sum ${hs.sum()} sorted head ${hs.sorted().take(5)}")
    val words = "the quick brown fox jumps over the lazy dog".split(" ")
    println("letters ${words.flatMap { it.toList() }.toSet().size} sorted ${words.toSet().sorted().joinToString("")}")

    println("== maps ==")
    val m = linkedMapOf("one" to 1, "two" to 2, "three" to 3)
    m["four"] = 4; m["two"] = 22; m.remove("one"); m.getOrPut("two") { 0 }; m.getOrPut("five") { 5 }
    println("$m keys=${m.keys} values=${m.values} size=${m.size}")
    println("get ${m["two"]} ${m["zero"]} getOrDefault ${m["zero"] ?: -1} getOrElse ${m.getOrElse("x") { 99 }}")
    println("mapValues ${m.mapValues { it.value * 2 }} mapKeys ${m.mapKeys { it.key.uppercase() }}")
    println("filterKeys ${m.filterKeys { it.length > 3 }} filterValues ${m.filterValues { it % 2 == 1 }} filter ${m.filter { (k, v) -> k.length == v }}")
    println("entries ${m.entries.map { "${it.key}=${it.value}" }} maxBy ${m.maxByOrNull { it.value }?.key} toList ${m.toList().take(2)}")
    val counts = LinkedHashMap<Char, Int>()
    for (c in "mississippi river") counts[c] = counts.getOrPut(c) { 0 } + 1
    println("counts $counts")
    for ((k, v) in counts.entries.sortedWith(compareByDescending<Map.Entry<Char, Int>> { it.value }.thenBy { it.key })) print("$k$v ")
    println()
    val inc = mutableMapOf<String, MutableList<Int>>()
    for (i in 1..10) inc.getOrPut(if (i % 3 == 0) "fizz" else "plain") { mutableListOf() }.add(i)
    println("getOrPut $inc")
    val iter = m.entries.iterator()
    while (iter.hasNext()) { val e = iter.next(); if (e.value > 4) iter.remove() else e.setValue(e.value + 100) }
    println("after iterator edit $m")
    val hm = HashMap<Int, String>()
    for (i in 0 until 300) hm[i * 37 % 1000] = "v$i"
    for (i in 0 until 300 step 3) hm.remove(i * 37 % 1000)
    println("hashmap size ${hm.size} keysum ${hm.keys.sum()} sample ${hm.keys.sorted().take(6).map { hm[it] }}")
    println("plus ${mapOf(1 to "a") + mapOf(2 to "b") + (1 to "z")} minus ${mapOf(1 to "a", 2 to "b") - 1}")

    println("== deque / builders ==")
    val dq = ArrayDeque<Int>()
    for (i in 1..5) { if (i % 2 == 0) dq.addFirst(i) else dq.addLast(i) }
    println("deque $dq first=${dq.first()} last=${dq.last()}")
    println("removeFirst ${dq.removeFirst()} removeLast ${dq.removeLast()} $dq")
    for (i in 0 until 20) { dq.addLast(i); if (i % 3 == 0) dq.removeFirst() }
    println("deque after churn $dq size=${dq.size}")
    val bl = buildList { add(1); addAll(listOf(2, 3)); for (i in 4..6) add(i * i); removeAt(0) }
    val bm = buildMap { for (w in words) put(w, (get(w) ?: 0) + w.length) }
    println("buildList $bl buildMap $bm")
    val bs = buildSet { addAll(words.map { it.length }) }
    println("buildSet $bs")
    println("List(5) ${List(5) { "x$it" }} emptyList ${emptyList<String>()} listOfNotNull ${listOfNotNull(1, null, 3)}")
    println("repeat ${List(3) { listOf('a', 'b') }.flatten()} generate ${(1..5).map { it.toString().repeat(it) }}")
    println("equality ${listOf(1, 2) == mutableListOf(1, 2)} ${setOf(1, 2) == setOf(2, 1)} ${mapOf(1 to 2) == hashMapOf(1 to 2)} ${listOf(1, 2).hashCode()} ${setOf("a", "b").hashCode()}")
}
