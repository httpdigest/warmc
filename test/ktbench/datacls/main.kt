// Data classes: generated equals and hashCode over fields.
//
// Every comparison is a `ref.test` for the type followed by field reads, which
// is the shape a producer emits for structural equality and one warmc has never
// seen from a real front end.
data class Key(val a: Int, val b: Int)

fun main() {
    val keys = Array(1_000) { Key(it % 97, it % 31) }
    var eq = 0L
    var h = 0L
    for (r in 0 until 3_000) {
        for (i in 0 until keys.size) {
            val k = keys[i]
            h += k.hashCode().toLong()
            if (k == keys[(i + r) % keys.size]) eq++
        }
    }
    println("eq = $eq h = $h")
}
