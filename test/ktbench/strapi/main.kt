// String APIs -- split, substring, trim, indexOf, padStart, uppercase -- and Char
// classification, parsing a generated CSV.
//
// A Kotlin/Wasm String is a struct holding its length and an `(array (mut i16))`
// of UTF-16 code units, plus a `leftIfInSum` link that keeps a concatenation a
// lazy rope until something reads its characters. Each line here goes through
// split, which returns a List<String>, so every field fetch is a `call_ref` to
// `get` through the List itable; then substring, toInt, trim, padStart and
// uppercase, all stdlib calls; then a per-character pass of isDigit, isUpperCase
// and `in "aeiou"`. `strbuild` measures building one string; this measures taking
// many apart, which is most of what string-handling code does.
val NAMES = arrayOf("alpha", "Bravo", "charlie", "Delta", "echo", "Foxtrot", "golf", "Hotel")

fun makeText(n: Int): String {
    val sb = StringBuilder()
    var x = 7
    for (i in 0 until n) {
        x = x * 1103515245 + 12345
        val v = (x ushr 9) % 100_000
        sb.append("id").append(i).append(", ").append(NAMES[(x ushr 4) and 7])
            .append(i % 13).append(',').append(v).append(',')
            .append(if (x and 0x100 == 0) "yes" else "no").append('\n')
    }
    return sb.toString()
}

fun main() {
    val text = makeText(4_000)
    var h = 0L
    for (r in 0 until 14) {
        for (line in text.split('\n')) {
            if (line.isEmpty()) continue
            val f = line.split(',')
            val id = f[0].substring(2).toInt()
            val name = f[1].trim()
            val v = f[2].toInt()
            val yes = f[3] == "yes"
            val padded = v.toString().padStart(8, '0')
            var vowels = 0
            var digits = 0
            var upper = 0
            for (ch in name) {
                if (ch in "aeiou") vowels++
                if (ch.isDigit()) digits++
                if (ch.isUpperCase()) upper++
            }
            val up = name.uppercase()
            val at = line.indexOf(',', 4) + line.lastIndexOf('e')
            h = h * 31 + id + v + vowels + digits * 3 + upper * 5 + at +
                padded.hashCode() + up.hashCode() + (if (yes) 1 else 0) + r
        }
    }
    println("h = $h")
}
