// 64-bit integer and bit operations, through stdlib extension functions.
//
// splitmix64 written as an extension function on Long, then the stdlib's bit
// tools over its output. countOneBits and countTrailingZeroBits are emitted
// inline as `i64.popcnt` and `i64.ctz`; countLeadingZeroBits is a call to a
// helper around `i64.clz`. rotateLeft and rotateRight never become `i64.rotl`:
// one is a call and the other is inlined, both as a shift left, a shift right by
// 64 - n and an or, so the rotate is an idiom warmc has to recognise rather than
// an instruction it is handed. The ULong remainder is `i64.rem_u` between calls
// to the value class's wrapper functions, and the high half of a 64x64 product
// is built from 32-bit halves. Every value here is an i64, which the object
// kernels in this corpus barely touch.

// splitmix64's finaliser, as an extension function on Long.
fun Long.mix(): Long {
    var z = this
    z = (z xor (z ushr 30)) * -4658895280553007687L
    z = (z xor (z ushr 27)) * -7723592293110705685L
    return z xor (z ushr 31)
}

val Long.parity: Int get() = countOneBits() and 1

// The high 64 bits of an unsigned 64x64 product, from 32-bit halves.
fun mulHi(a: Long, b: Long): Long {
    val a0 = a and 0xFFFFFFFFL
    val a1 = a ushr 32
    val b0 = b and 0xFFFFFFFFL
    val b1 = b ushr 32
    val t = a1 * b0 + ((a0 * b0) ushr 32)
    val w1 = (t and 0xFFFFFFFFL) + a0 * b1
    return a1 * b1 + (t ushr 32) + (w1 ushr 32)
}

fun main() {
    var state = 0x0123456789ABCDEFL
    var acc = 0L
    var pop = 0L
    var lz = 0L
    var tz = 0L
    var par = 0
    var hi = 0L
    var um = 0UL
    for (i in 0 until 20_000_000) {
        state += -7046029254386353131L
        val z = state.mix()
        acc = acc.rotateLeft(13) xor z
        acc = acc.rotateRight((z and 63).toInt()) + mulHi(z, acc)
        pop += z.countOneBits()
        lz += z.countLeadingZeroBits()
        tz += (z or Long.MIN_VALUE).countTrailingZeroBits()
        par += z.parity
        hi = hi xor z.takeHighestOneBit() xor z.takeLowestOneBit()
        um += z.toULong() % 1_000_003UL
    }
    println("acc = $acc pop = $pop lz = $lz tz = $tz par = $par hi = $hi um = $um")
}
