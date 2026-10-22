// Integer arithmetic at every width: Int/Long overflow and wrap-around,
// negation and abs of MIN_VALUE, division and remainder of negative operands
// (truncating `/` and `%` versus flooring `floorDiv` and `mod`), MIN_VALUE / -1,
// division by zero caught as ArithmeticException, shifts by amounts at and
// beyond the width (masked), and/or/xor/inv, countOneBits/LeadingZero/
// TrailingZero, rotateLeft/Right, takeHighestOneBit/LowestOneBit, Byte and
// Short wrap and sign extension, unsigned UByte/UShort/UInt/ULong arithmetic,
// comparison, division and printing, and every narrowing/widening conversion
// between them.  Then hashing and checksum kernels built from those pieces:
// FNV-1a, a murmur-style mixer, CRC-32, xorshift, modular exponentiation,
// gcd/lcm and an integer square root.
//
// Kotlin/Wasm lowers these straight to i32/i64 wasm instructions, plus explicit
// checks where Kotlin and wasm disagree (division traps in wasm, it throws in
// Kotlin; MIN_VALUE / -1 traps in wasm).  Every edge value is printed.
fun fnv1a(s: String): Int {
    var h = 0x811c9dc5.toInt()
    for (c in s) { h = h xor (c.code and 0xff); h *= 0x01000193 }
    return h
}

fun mix64(x0: Long): Long {
    var x = x0
    x = x xor (x ushr 33); x *= -0xae502812aa7333L
    x = x xor (x ushr 33); x *= -0x3b314601e57a13adL
    return x xor (x ushr 33)
}

val crcTable = IntArray(256) { n ->
    var c = n
    repeat(8) { c = if (c and 1 != 0) (c ushr 1) xor 0xEDB88320.toInt() else c ushr 1 }
    c
}
fun crc32(bytes: ByteArray): UInt {
    var c = -1
    for (b in bytes) c = crcTable[(c xor b.toInt()) and 0xff] xor (c ushr 8)
    return c.inv().toUInt()
}

fun powMod(b0: Long, e0: Long, m: Long): Long {
    var r = 1L; var b = b0 % m; var e = e0
    while (e > 0) { if (e and 1L == 1L) r = r * b % m; b = b * b % m; e = e shr 1 }
    return r
}

tailrec fun gcd(a: Long, b: Long): Long = if (b == 0L) kotlin.math.abs(a) else gcd(b, a % b)
fun lcm(a: Long, b: Long) = a / gcd(a, b) * b

fun isqrt(n: Long): Long {
    if (n < 2) return n
    var x = n; var y = (x + 1) / 2
    while (y < x) { x = y; y = (x + n / x) / 2 }
    return x
}

fun hex(i: Int) = i.toUInt().toString(16).padStart(8, '0')
fun hex(l: Long) = l.toULong().toString(16).padStart(16, '0')

fun main() {
    println("== Int overflow ==")
    val imax = Int.MAX_VALUE; val imin = Int.MIN_VALUE
    println("${imax + 1} ${imin - 1} ${imax * 2} ${imax * imax} ${imin * -1} ${-imin} ${kotlin.math.abs(imin)} ${imin / -1} ${imin % -1}")
    var acc = 1
    for (i in 1..20) { acc *= i; if (i % 4 == 0) println("  $i! mod 2^32 = $acc ${hex(acc)}") }
    var fib = intArrayOf(0, 1)
    for (i in 0 until 60) fib = intArrayOf(fib[1], fib[0] + fib[1])
    println("fib60 wrapped ${fib[0]}")
    println("Long ${Long.MAX_VALUE + 1} ${Long.MIN_VALUE - 1} ${Long.MAX_VALUE * 3} ${-Long.MIN_VALUE} ${Long.MIN_VALUE / -1} ${Long.MIN_VALUE % -1L}")
    println("mixed ${imax.toLong() + 1} ${(imax + 1).toLong()} ${imax.toLong() * imax} ${(imax.toLong() * imax).toInt()}")

    println("== division ==")
    for (a in listOf(7, -7, 0, 13, -13, imin, imax)) {
        for (b in listOf(2, -2, 3, -3, 7)) {
            print("  $a/$b=${a / b} %=${a % b} fdiv=${a.floorDiv(b)} mod=${a.mod(b)}")
        }
        println()
    }
    for (a in listOf(-9L, 9L, Long.MIN_VALUE)) println("  L $a ${a / 4} ${a % 4} ${a.floorDiv(4L)} ${a.mod(4L)} ${a / -4} ${a.mod(-4L)}")
    for ((a, b) in listOf(1 to 0, 0 to 0, imin to 0)) {
        val r = try { (a / b).toString() } catch (e: ArithmeticException) { "ArithmeticException" }
        val r2 = try { (a % b).toString() } catch (e: ArithmeticException) { "ArithmeticException" }
        val r3 = try { (a.toLong() / 0L).toString() } catch (e: ArithmeticException) { "ArithmeticException" }
        println("  $a/$b -> $r, $r2, $r3")
    }

    println("== shifts / bits ==")
    val x = 0x12345678; val neg = -0x12345678
    for (s in listOf(0, 1, 4, 31, 32, 33, 63, -1)) {
        println("  s=$s shl=${hex(x shl s)} shr=${hex(neg shr s)} ushr=${hex(neg ushr s)} Lshl=${hex(1L shl s)} Lshr=${hex(Long.MIN_VALUE shr s)} Lushr=${hex(-1L ushr s)}")
    }
    println("and=${hex(x and neg)} or=${hex(x or neg)} xor=${hex(x xor neg)} inv=${hex(x.inv())} Linv=${hex(0L.inv())}")
    for (v in listOf(0, 1, -1, 0x80000000.toInt(), 0x00F0F000, 12345678, imax)) {
        println("  ${hex(v)} pop=${v.countOneBits()} clz=${v.countLeadingZeroBits()} ctz=${v.countTrailingZeroBits()} rol5=${hex(v.rotateLeft(5))} ror7=${hex(v.rotateRight(7))} hi=${hex(v.takeHighestOneBit())} lo=${hex(v.takeLowestOneBit())}")
    }
    for (v in listOf(0L, 1L, -1L, Long.MIN_VALUE, 0x0123456789ABCDEFL)) {
        println("  ${hex(v)} pop=${v.countOneBits()} clz=${v.countLeadingZeroBits()} ctz=${v.countTrailingZeroBits()} rol13=${hex(v.rotateLeft(13))} ror40=${hex(v.rotateRight(40))}")
    }
    var bits = 0L
    for (i in 0 until 64 step 3) bits = bits or (1L shl i)
    println("every third bit ${hex(bits)} pop=${bits.countOneBits()} reversed-ish ${hex(java_like_reverse(bits))}")

    println("== Byte / Short ==")
    for (v in listOf(0, 127, 128, 255, 256, -129, 300, -1, 1000, 32767, 32768, 65535, 65536, -32769)) {
        println("  $v toByte=${v.toByte()} toShort=${v.toShort()} toUByte=${v.toUByte()} toUShort=${v.toUShort()} toChar=${v.toChar().code}")
    }
    val b1: Byte = 100; val b2: Byte = 100
    println("byte add ${b1 + b2} (Int) ${(b1 + b2).toByte()} (Byte) mul ${(b1 * b2).toByte()} neg ${(-Byte.MIN_VALUE).toByte()} inc ${(Byte.MAX_VALUE + 1).toByte()}")
    val s1: Short = 30000
    println("short ${(s1 + s1).toShort()} ${(s1 * 3).toShort()} ${Short.MIN_VALUE.toInt() - 1} ${(Short.MIN_VALUE - 1).toShort()}")
    var bacc: Byte = 0
    for (i in 1..50) bacc = (bacc + i * 7).toByte()
    println("byte accumulate $bacc and=${(0xF0.toByte().toInt() and 0xFF)} sext=${0xF0.toByte().toInt()} ${0x80.toByte().toLong()}")

    println("== unsigned ==")
    val u = 4000000000u; val u2 = 3u
    println("UInt ${u + u} ${u * u2} ${u / u2} ${u % u2} ${u2 - u} ${UInt.MAX_VALUE + 1u} ${u > u2} ${u.toInt()} ${u.toLong()} ${u shr 4} ${u.countOneBits()}")
    val ul = 18000000000000000000uL
    println("ULong $ul ${ul / 7uL} ${ul % 7uL} ${ul + ul} ${ULong.MAX_VALUE} ${ULong.MAX_VALUE / 10uL} ${ul.toLong()} ${(-1L).toULong()} ${ul > 9223372036854775807uL}")
    println("UByte ${255.toUByte()} ${(200u.toUByte() + 100u.toUByte())} ${(200u.toUByte() + 100u.toUByte()).toUByte()} UShort ${(65535u.toUShort() + 1u)} ${(65535u.toUShort()).toShort()}")
    val us = listOf(5u, 4000000000u, 0u, 2147483648u, 2147483647u)
    println("sorted ${us.sorted()} max ${us.maxOrNull()} sum ${us.sum()} signed view ${us.map { it.toInt() }.sorted()}")
    for (d in listOf(1u, 2u, 3u, 10u, 4000000000u)) print(" ${UInt.MAX_VALUE / d}:${UInt.MAX_VALUE % d}")
    println()
    for (d in listOf(1uL, 3uL, 1000000007uL, 10000000000000000000uL)) print(" ${ULong.MAX_VALUE / d}:${ULong.MAX_VALUE % d}")
    println()
    println("compare ${0u.compareTo(UInt.MAX_VALUE)} ${(-1).compareTo(0)} ${UInt.MAX_VALUE.compareTo(0u)} ${(-1).toUInt() == UInt.MAX_VALUE}")

    println("== conversions ==")
    for (l in listOf(0L, 255L, 4294967296L + 5, -1L, Long.MIN_VALUE, Long.MAX_VALUE, 2147483648L)) {
        println("  $l toInt=${l.toInt()} toShort=${l.toShort()} toByte=${l.toByte()} toUInt=${l.toUInt()} toDouble=${l.toDouble()} toFloat=${l.toFloat()}")
    }
    for (d in listOf(0.0, -0.9, 2.5, -2.5, 1e10, -1e10, 3e19, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, 2147483647.9, -2147483648.9)) {
        println("  $d toInt=${d.toInt()} toLong=${d.toLong()} toUInt=${d.toUInt()} toULong=${d.toULong()}")
    }
    println("  chars ${'A'.code.toByte()} ${Char(0xFFFF).code.toShort()} ${(-1).toChar().code} ${65.toChar()}")

    println("== kernels ==")
    for (s in listOf("", "a", "hello", "Kotlin/Wasm", "The quick brown fox")) println("  fnv1a('$s')=${hex(fnv1a(s))} crc32=${crc32(s.encodeToByteArray()).toString(16)}")
    println("  mix64 ${(0L until 6L).map { hex(mix64(it)) }}")
    var xs = 88172645463325252L
    val outs = LongArray(6)
    for (i in 0 until 1000) { xs = xs xor (xs shl 13); xs = xs xor (xs ushr 7); xs = xs xor (xs shl 17); if (i >= 994) outs[i - 994] = xs }
    println("  xorshift ${outs.map { hex(it) }}")
    println("  powMod ${powMod(2, 1000000, 1000000007)} ${powMod(3, 1L shl 40, 998244353)} ${powMod(7, 0, 13)} fermat ${(2L..12L).map { powMod(it, 1000000006, 1000000007) }}")
    println("  gcd ${gcd(1071, 462)} ${gcd(-48, 18)} ${gcd(0, 5)} ${gcd(Long.MAX_VALUE, 3)} lcm ${(1L..20L).reduce(::lcm)} ${lcm(4294967296L, 6)}")
    println("  isqrt ${listOf(0L, 1L, 15L, 16L, 17L, 1L shl 40, Long.MAX_VALUE, 999999999999L).map(::isqrt)}")
    var collisions = 0
    val seen = HashSet<Int>()
    for (i in 0 until 5000) if (!seen.add(fnv1a("key$i") ushr 20)) collisions++
    println("  fnv buckets used ${seen.size} collisions $collisions")
}

fun java_like_reverse(v: Long): Long {
    var x = v; var r = 0L
    for (i in 0 until 64) { r = (r shl 1) or (x and 1L); x = x ushr 1 }
    return r
}
