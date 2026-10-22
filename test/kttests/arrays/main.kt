// Arrays: every primitive array type (IntArray, LongArray, DoubleArray,
// FloatArray, ByteArray, ShortArray, CharArray, BooleanArray), Array<T> of
// references and of nullable references, arrays of arrays, constructors with
// initialiser lambdas, copyOf/copyOfRange/copyInto with overlapping ranges,
// fill, sort/sortDescending/sortedArray and sortWith, reverse, content
// equality/hashing/toString, out-of-bounds access caught as an exception, and
// in-place algorithms (sieve, prefix sums, rotation, matrix product, insertion
// sort, binary search, two-pointer partition).
//
// Kotlin/Wasm stores a ByteArray/ShortArray/CharArray/BooleanArray as a wasm
// GC array of packed i8/i16, so reads are array.get_s or array.get_u and
// writes truncate; LongArray/DoubleArray are i64/f64 arrays.  The values
// printed below sit on the sign and truncation boundaries of each width.
fun sieve(n: Int): BooleanArray {
    val composite = BooleanArray(n + 1)
    composite[0] = true; if (n >= 1) composite[1] = true
    var i = 2
    while (i * i <= n) { if (!composite[i]) { var j = i * i; while (j <= n) { composite[j] = true; j += i } }; i++ }
    return composite
}

fun insertionSort(a: IntArray) {
    for (i in 1 until a.size) {
        val key = a[i]; var j = i - 1
        while (j >= 0 && a[j] > key) { a[j + 1] = a[j]; j-- }
        a[j + 1] = key
    }
}

fun bsearch(a: IntArray, x: Int): Int {
    var lo = 0; var hi = a.size - 1
    while (lo <= hi) {
        val mid = (lo + hi) ushr 1
        when {
            a[mid] < x -> lo = mid + 1
            a[mid] > x -> hi = mid - 1
            else -> return mid
        }
    }
    return -(lo + 1)
}

fun rotate(a: IntArray, k: Int) {
    fun rev(from: Int, to: Int) { var i = from; var j = to - 1; while (i < j) { val t = a[i]; a[i] = a[j]; a[j] = t; i++; j-- } }
    val s = k.mod(a.size)
    rev(0, a.size); rev(0, s); rev(s, a.size)
}

fun matmul(a: Array<DoubleArray>, b: Array<DoubleArray>): Array<DoubleArray> {
    val n = a.size; val m = b[0].size; val k = b.size
    return Array(n) { i -> DoubleArray(m) { j -> var s = 0.0; for (t in 0 until k) s += a[i][t] * b[t][j]; s } }
}

fun main() {
    println("== IntArray ==")
    val ia = IntArray(10) { (it * 37 + 11) % 23 - 7 }
    println("${ia.contentToString()} size=${ia.size} sum=${ia.sum()} min=${ia.minOrNull()} max=${ia.maxOrNull()} avg=${ia.average()}")
    val sorted = ia.sortedArray()
    println("sorted ${sorted.contentToString()} orig unchanged ${ia.contentToString()}")
    for (x in listOf(-7, -3, 0, 5, 15, 16)) println("  bsearch($x)=${bsearch(sorted, x)}")
    val ins = ia.copyOf(); insertionSort(ins)
    println("insertion eq ${ins.contentEquals(sorted)} hash eq ${ins.contentHashCode() == sorted.contentHashCode()}")
    ia.sortDescending(); println("desc ${ia.contentToString()}")
    ia.reverse(); println("rev ${ia.contentToString()}")
    for (k in listOf(1, 3, -2, 10, 13)) { val r = IntArray(7) { it }; rotate(r, k); println("  rotate $k ${r.contentToString()}") }
    println("copyOf(13) ${ia.copyOf(13).contentToString()} copyOf(4) ${ia.copyOf(4).contentToString()} range ${ia.copyOfRange(2, 6).contentToString()}")
    val ov = IntArray(10) { it }
    ov.copyInto(ov, destinationOffset = 3, startIndex = 0, endIndex = 6); println("overlap fwd ${ov.contentToString()}")
    val ov2 = IntArray(10) { it }
    ov2.copyInto(ov2, destinationOffset = 0, startIndex = 4, endIndex = 10); println("overlap back ${ov2.contentToString()}")
    val f = IntArray(8); f.fill(7); f.fill(-1, 2, 5); println("fill ${f.contentToString()}")
    val pre = IntArray(10) { it + 1 }; for (i in 1 until pre.size) pre[i] += pre[i - 1]; println("prefix ${pre.contentToString()}")
    println("map ${ia.map { it * 2 }} filter ${ia.filter { it > 0 }} indexOf ${ia.indexOf(ia[3])} any ${ia.any { it == 0 }} count ${ia.count { it < 0 }}")
    println("withIndex ${ia.withIndex().filter { it.index % 3 == 0 }.map { "${it.index}:${it.value}" }}")
    println("toList ${intArrayOf(3, 1, 2).toList()} toSet ${intArrayOf(3, 1, 3, 2, 1).toSet()} asList ${intArrayOf(9, 8).asList()}")
    val comp = sieve(100)
    println("primes ${(0..100).filter { !comp[it] }} count<10000 ${sieve(10000).count { !it }}")

    println("== LongArray / DoubleArray / FloatArray ==")
    val la = LongArray(12) { if (it == 0) 1L else 1L shl (it * 6) }
    println(la.contentToString())
    la[11] = Long.MIN_VALUE; la[10] = Long.MAX_VALUE
    println("sum=${la.sum()} sorted=${la.sortedArray().take(3)} max=${la.maxOrNull()}")
    val fact = LongArray(21); fact[0] = 1; for (i in 1..20) fact[i] = fact[i - 1] * i
    println("fact ${fact.contentToString()}")
    val da = DoubleArray(8) { 1.0 / (it + 1) }
    println("${da.contentToString()} sum=${da.sum()}")
    da.sort(); println("sorted ${da.contentToString()}")
    val special = doubleArrayOf(3.5, Double.NaN, -0.0, 0.0, Double.NEGATIVE_INFINITY, -1.0, Double.POSITIVE_INFINITY, 1e-300)
    special.sort(); println("special sorted ${special.contentToString()}")
    println("special eq self ${special.contentEquals(special.copyOf())} hash ${doubleArrayOf(1.0, 2.0).contentHashCode()}")
    val fa = FloatArray(6) { it * 0.1f }
    println("${fa.contentToString()} sum=${fa.sum()} ${fa.map { it.toDouble() }.take(3)}")
    val a = arrayOf(doubleArrayOf(1.0, 2.0), doubleArrayOf(3.0, 4.0), doubleArrayOf(5.0, 6.0))
    val b = arrayOf(doubleArrayOf(0.5, -1.0, 2.0), doubleArrayOf(1.5, 0.25, -0.5))
    println("matmul ${matmul(a, b).map { it.contentToString() }} ${matmul(b, a).map { it.contentToString() }}")

    println("== ByteArray / ShortArray / CharArray / BooleanArray ==")
    val ba = ByteArray(10) { (it * 50 - 100).toByte() }
    println("bytes ${ba.contentToString()} unsigned ${ba.map { it.toInt() and 0xFF }}")
    ba[0] = (ba[0] + 200).toByte(); ba[1] = (-ba[1]).toByte(); ba[2] = (ba[2].toInt() shl 3).toByte()
    println("bytes after ${ba.contentToString()} sum=${ba.sum()} sorted=${ba.sortedArray().contentToString()}")
    val bb = "Hello, wasm! é€".encodeToByteArray()
    println("utf8 ${bb.size} ${bb.contentToString()} back=${bb.decodeToString()}")
    val sa = ShortArray(8) { (it * 12000 - 40000).toShort() }
    println("shorts ${sa.contentToString()} max=${sa.maxOrNull()} ${sa.map { it.toInt() and 0xFFFF }}")
    sa[3] = (sa[3] * 3).toShort(); sa[4] = Short.MIN_VALUE; sa[5] = (Short.MAX_VALUE + 1).toShort()
    println("shorts after ${sa.contentToString()}")
    val ca = CharArray(26) { 'a' + it }
    ca.reverse(); ca[0] = ca[0].uppercaseChar(); ca[25] = '￿'
    println("chars ${ca.concatToString(0, 25)} last=${ca[25].code} concat=${ca.concatToString(0, 5)}")
    val c2 = "kotlin wasm".toCharArray(); c2.sort()
    println("sorted chars '${c2.concatToString()}' codes ${c2.map { it.code }.take(4)}")
    val bo = BooleanArray(10) { it % 3 == 0 }
    println("bools ${bo.contentToString()} count=${bo.count { it }} all=${bo.all { it }} flip=${bo.map { !it }.count { it }}")
    for (i in bo.indices) bo[i] = bo[i] xor (i % 2 == 0)
    println("xor ${bo.contentToString()}")

    println("== Array<T> ==")
    val names = arrayOf("delta", "alpha", "echo", "charlie", "bravo")
    names.sort(); println("sorted ${names.contentToString()}")
    names.sortWith(compareBy<String> { it.length }.thenByDescending { it }); println("by len ${names.contentToString()}")
    names.sortBy { it.last() }; println("by last ${names.contentToString()}")
    val nulls = arrayOfNulls<String>(5); nulls[1] = "x"; nulls[3] = "y"
    println("nulls ${nulls.contentToString()} notNull ${nulls.filterNotNull()} count ${nulls.count { it == null }}")
    val grid = Array(4) { r -> IntArray(5) { c -> r * 5 + c } }
    println("grid ${grid.joinToString(" | ") { it.joinToString(",") }}")
    val transposed = Array(5) { c -> IntArray(4) { r -> grid[r][c] } }
    println("transposed ${transposed.map { it.toList() }}")
    println("deepToString ${arrayOf(arrayOf(1, 2), arrayOf(3)).contentDeepToString()} deepEq ${arrayOf(intArrayOf(1)).contentDeepEquals(arrayOf(intArrayOf(1)))}")
    val jag = Array(5) { IntArray(it + 1) { j -> j * it } }
    println("jagged ${jag.map { it.sum() }} total ${jag.sumOf { it.size }}")
    val objs = Array<Any?>(6) { when (it % 3) { 0 -> it; 1 -> "s$it"; else -> null } }
    println("objs ${objs.contentToString()} ints=${objs.filterIsInstance<Int>()} strs=${objs.filterIsInstance<String>()}")
    val boxed: Array<Int> = arrayOf(5, 2, 8, 1)
    val prim = boxed.toIntArray(); prim[0] = 100
    println("boxed ${boxed.contentToString()} prim ${prim.contentToString()} back ${prim.toTypedArray().contentToString()}")
    val plus = intArrayOf(1, 2) + 3 + intArrayOf(4, 5)
    println("plus ${plus.contentToString()} ${(arrayOf("a") + "b" + arrayOf("c")).contentToString()}")

    println("== bounds ==")
    val small = IntArray(3)
    for (i in listOf(-1, 0, 2, 3, 100)) {
        val r = try { small[i] = i; "ok ${small[i]}" } catch (e: IndexOutOfBoundsException) { "IOOBE" }
        println("  set/get $i -> $r")
    }
    val r2 = try { arrayOf("a")[1] } catch (e: IndexOutOfBoundsException) { "IOOBE" }
    val r3 = try { ByteArray(2)[-5].toString() } catch (e: IndexOutOfBoundsException) { "IOOBE" }
    val r4 = try { IntArray(-1).size.toString() } catch (e: Exception) { "negative size: ${e::class.simpleName}" }
    println("  $r2 $r3 $r4")
    println("  empty ${IntArray(0).contentToString()} ${emptyArray<String>().size} ${IntArray(0).sum()} ${IntArray(0).maxOrNull()}")
}
