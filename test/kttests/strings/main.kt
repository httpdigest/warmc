// Strings and characters: templates with nested expressions, StringBuilder
// (append of every type, insert, delete, reverse, setCharAt, setLength),
// substring/indexOf/split/replace/trim/pad, case conversion, comparison and
// equalsIgnoreCase, parsing (toInt/toLong/toDouble and the OrNull forms,
// radix parsing), number formatting with toString(radix) and padStart, char
// arithmetic and classification, raw strings with trimIndent/trimMargin,
// UTF-16 surrogate pairs and UTF-8 round trips, and a few text algorithms
// (run-length coding, Caesar, anagram groups, word wrap, Levenshtein).
//
// A Kotlin/Wasm String is a GC struct around an i16 array, built lazily from
// literals in a data segment, and a StringBuilder grows its own char array, so
// these exercise packed array copies, char-by-char loops and lots of
// short-lived string allocation.
fun rle(s: String): String = buildString {
    var i = 0
    while (i < s.length) {
        var j = i
        while (j < s.length && s[j] == s[i]) j++
        append(j - i); append(s[i]); i = j
    }
}

fun unrle(s: String): String = buildString {
    var n = 0
    for (c in s) if (c.isDigit()) n = n * 10 + (c - '0') else { repeat(n) { append(c) }; n = 0 }
}

fun wrap(text: String, width: Int): List<String> {
    val lines = mutableListOf<String>()
    var cur = StringBuilder()
    for (w in text.split(' ').filter { it.isNotEmpty() }) {
        if (cur.isNotEmpty() && cur.length + 1 + w.length > width) { lines.add(cur.toString()); cur = StringBuilder() }
        if (cur.isNotEmpty()) cur.append(' ')
        cur.append(w)
    }
    if (cur.isNotEmpty()) lines.add(cur.toString())
    return lines
}

fun levenshtein(a: String, b: String): Int {
    var prev = IntArray(b.length + 1) { it }
    for (i in 1..a.length) {
        val cur = IntArray(b.length + 1); cur[0] = i
        for (j in 1..b.length) cur[j] = minOf(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + if (a[i - 1] == b[j - 1]) 0 else 1)
        prev = cur
    }
    return prev[b.length]
}

fun main() {
    println("== templates ==")
    val name = "Kotlin"; val n = 42; val pi = 3.14159; val list = listOf(1, 2)
    println("Hello, $name! n=$n n+1=${n + 1} len=${name.length} upper=${name.uppercase()} list=$list first=${list.first()}")
    println("nested ${"inner ${n * 2} ${if (n > 40) "big" else "small"}"} dollar=\$n brace=\${x} pi=$pi")
    println("escapes: tab[\t] quote[\"] backslash[\\] unicode[éΩ] newline-free")

    println("== StringBuilder ==")
    val sb = StringBuilder()
    sb.append("abc").append(1).append(2L).append(3.5).append(true).append('x').append(null as String?).append(-0.0f)
    println("$sb len=${sb.length}")
    sb.insert(0, "<<").insert(5, '|').append(">>")
    println("$sb")
    sb.setLength(10); println("setLength '$sb'")
    sb.reverse(); println("reverse '$sb'")
    sb.deleteAt(0).deleteRange(2, 4); println("delete '$sb'")
    sb[0] = 'Z'; println("set '$sb' indexOf c=${sb.indexOf("c")} char2=${sb[2]}")
    sb.clear(); repeat(5) { sb.append(it).append(',') }; println("repeat '${sb.removeSuffix(",")}' cap-like len=${sb.length}")
    val big = StringBuilder(); for (i in 0 until 2000) big.append(('a' + i % 26)); println("big len=${big.length} hash=${big.toString().hashCode()} tail=${big.substring(1990)}")

    println("== search / slice ==")
    val s = "the quick brown fox jumps over the lazy dog"
    println("indexOf ${s.indexOf("the")} ${s.indexOf("the", 1)} ${s.lastIndexOf("o")} ${s.indexOf('z')} ${s.indexOf("cat")} ${s.indexOfFirst { it == 'q' }}")
    println("substring ${s.substring(4, 9)} ${s.substring(40)} before=${s.substringBefore(' ')} after=${s.substringAfterLast(' ')} between=${s.substringAfter("quick ").substringBefore(" jumps")}")
    println("contains ${"fox" in s} ${s.contains("FOX", ignoreCase = true)} starts ${s.startsWith("the")} ends ${s.endsWith("dog")} ${s.startsWith("quick", 4)}")
    println("split ${s.split(" ").size} ${s.split(" ", limit = 3)} ${"a,b;;c,".split(',', ';')} ${"1--2---3".split("--")}")
    println("words ${s.split(' ').map { it.length }} lines ${"l1\nl2\r\nl3".lines()}")
    println("replace ${s.replace("the", "THE")} ${s.replace('o', '0')} ${s.replaceFirst("o", "()")}")
    println("trim [${"  pad  ".trim()}] [${"  pad  ".trimStart()}] [${"  pad  ".trimEnd()}] [${"xxhixx".trim('x')}] [${"007".trimStart('0')}]")
    println("pad [${"7".padStart(4, '0')}] [${"ab".padEnd(5, '.')}] [${"long".padStart(2)}] repeat [${"ab".repeat(4)}] [${"-".repeat(0)}]")
    println("case ${"MiXeD 123".uppercase()} ${"MiXeD 123".lowercase()} ${"hello world".replaceFirstChar { it.titlecase() }} ${"ABC".lowercase().capitalizeFirst()}")
    println("reversed ${s.reversed().take(10)} chunked ${"abcdefgh".chunked(3)} windowed ${"abcde".windowed(2)} drop ${s.drop(36)} takeLast ${s.takeLast(3)}")
    println("filter ${s.filter { it !in "aeiou " }} count ${s.count { it == 'o' }} map ${"abc".map { it + 1 }}")
    println("removePrefix ${"prefix-body".removePrefix("prefix-")} surrounding ${"[x]".removeSurrounding("[", "]")} ifBlank ${"   ".ifBlank { "blank!" }}")

    println("== compare ==")
    val ws = listOf("banana", "Apple", "cherry", "apple", "Banana", "", "b")
    println("sorted ${ws.sorted()} ignoreCase ${ws.sortedWith(String.CASE_INSENSITIVE_ORDER)} byLen ${ws.sortedBy { it.length }}")
    println("compareTo ${"a".compareTo("b")} ${"b".compareTo("a")} ${"abc".compareTo("abd")} ${"ab".compareTo("abc")} ${"B".compareTo("a")}")
    println("equals ${"Hello".equals("hello")} ${"Hello".equals("hello", ignoreCase = true)} ${"a" + "b" == "ab"} hash ${"hello".hashCode()} ${"".hashCode()} ${"Kotlin/Wasm".hashCode()}")
    val anagrams = listOf("listen", "silent", "enlist", "google", "gooegl", "cat", "act", "tac", "dog")
    println("anagrams ${anagrams.groupBy { it.toCharArray().sorted().joinToString("") }.values.filter { it.size > 1 }}")

    println("== parsing ==")
    for (t in listOf("123", "-45", "+7", "0", "2147483647", "2147483648", "abc", "", "12a", " 5")) {
        println("  '$t' toIntOrNull=${t.toIntOrNull()} toLongOrNull=${t.toLongOrNull()} toDoubleOrNull=${t.toDoubleOrNull()}")
    }
    println("radix ${"ff".toInt(16)} ${"-1010".toInt(2)} ${"zz".toInt(36)} ${"777".toLong(8)} ${"7fffffffffffffff".toLong(16)} ${"g".toIntOrNull(16)}")
    for (d in listOf("3.14", "-0.5", "1e10", "1.5E-3", "NaN", "-Infinity", ".5", "5.", "1_000")) println("  '$d' -> ${d.toDoubleOrNull()}")
    try { "x1".toInt() } catch (e: NumberFormatException) { println("NumberFormatException caught") }

    println("== number formatting ==")
    for (v in listOf(0, 1, 7, 255, 1000, -1, -255, Int.MAX_VALUE, Int.MIN_VALUE)) {
        println("  $v bin=${v.toString(2)} oct=${v.toString(8)} hex=${v.toString(16)} b36=${v.toString(36)} uhex=${v.toUInt().toString(16)} pad=${v.toString().padStart(12, '_')}")
    }
    for (v in listOf(0L, 1L shl 40, -1L, Long.MAX_VALUE, Long.MIN_VALUE)) println("  $v hex=${v.toString(16)} b36=${v.toString(36)} uhex=${v.toULong().toString(16)}")
    fun fixed(x: Double, digits: Int): String {
        var scale = 1L; repeat(digits) { scale *= 10 }
        val v = kotlin.math.round(kotlin.math.abs(x) * scale).toLong()
        val sign = if (x < 0 && v != 0L) "-" else ""
        return if (digits == 0) "$sign$v" else "$sign${v / scale}.${(v % scale).toString().padStart(digits, '0')}"
    }
    println("fixed ${fixed(3.14159, 2)} ${fixed(-2.5, 0)} ${fixed(0.0049, 2)} ${fixed(1234.5678, 3)} ${fixed(-0.001, 2)} ${fixed(99.995, 2)}")
    println("table:"); for (i in 1..5) println("  |${i.toString().padStart(3)}|${(i * i).toString().padEnd(4)}|${fixed(1.0 / i, 4).padStart(8)}|")

    println("== chars ==")
    for (c in listOf('a', 'Z', '5', ' ', '_', 'é', 'Ж', '\t')) {
        println("  '${if (c == '\t') "\\t" else c.toString()}' code=${c.code} digit=${c.isDigit()} letter=${c.isLetter()} upper=${c.isUpperCase()} lower=${c.isLowerCase()} ws=${c.isWhitespace()} up=${c.uppercaseChar()} low=${c.lowercaseChar()} next=${c + 1} prev=${c - 1}")
    }
    println("arith ${'a' + 25} ${'z' - 'a'} ${('a'..'e').toList()} ${('a'..'z' step 5).joinToString("")} ${Char(65)}${Char(0x263A)} ${'9'.digitToInt()} ${'f'.digitToInt(16)} ${5.digitToChar()} ${11.digitToChar(16)}")
    println("caesar ${"Hello, World".map { c -> if (c.isLetter()) { val base = if (c.isUpperCase()) 'A' else 'a'; base + (c - base + 3) % 26 } else c }.joinToString("")}")

    println("== raw strings ==")
    val raw = """
        |line one
        |  indented "quotes" and ${'$'}dollar
        |last
    """.trimMargin()
    println(raw)
    val ind = """
        alpha
          beta
        gamma
    """.trimIndent()
    println(ind.lines().map { it.length })

    println("== unicode ==")
    val emoji = "a😀bé€"
    println("len=${emoji.length} codes=${emoji.map { it.code.toString(16) }} surrogate=${emoji[1].isHighSurrogate()} ${emoji[2].isLowSurrogate()}")
    val bytes = emoji.encodeToByteArray()
    println("utf8 len=${bytes.size} ${bytes.map { it.toInt() and 0xff }} roundtrip=${bytes.decodeToString() == emoji}")
    println("reversed codes ${emoji.reversed().map { it.code.toString(16) }}")

    println("== algorithms ==")
    for (t in listOf("aaabccddddde", "abc", "", "zzzzzzzzzzzz", "aabbaa")) println("  rle '$t' -> '${rle(t)}' -> '${unrle(rle(t))}'")
    val text = "Kotlin is a cross-platform, statically typed, general-purpose high-level programming language with type inference."
    wrap(text, 24).forEach { println("  |${it.padEnd(24)}|") }
    for ((a, b) in listOf("kitten" to "sitting", "flaw" to "lawn", "" to "abc", "same" to "same", "intention" to "execution")) println("  lev($a,$b)=${levenshtein(a, b)}")
    val freq = text.lowercase().filter { it.isLetter() }.groupingBy { it }.eachCount().entries.sortedWith(compareBy({ -it.value }, { it.key })).take(6)
    println("  freq ${freq.map { "${it.key}${it.value}" }}")
}

fun String.capitalizeFirst() = if (isEmpty()) this else this[0].uppercaseChar() + substring(1)
