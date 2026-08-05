// Interface dispatch through a wasm GC vtable.
//
// The fourth answer to the same source in this suite: TinyGo compares type ids,
// Go loads from an itab, Rust uses a static vtable in linear memory, and
// Kotlin/Wasm emits `ref.cast` plus `call_ref` on a function reference the
// engine type-checks. The types alternate so the branch cannot be predicted.
interface Shape { fun area(): Double; fun sides(): Int }
class Circle(val r: Double) : Shape {
    override fun area() = 3.14159265358979 * r * r
    override fun sides() = 0
}
class Rect(val w: Double, val h: Double) : Shape {
    override fun area() = w * h
    override fun sides() = 4
}
class Tri(val b: Double, val h: Double) : Shape {
    override fun area() = 0.5 * b * h
    override fun sides() = 3
}

fun main() {
    val v = ArrayList<Shape>()
    for (i in 0 until 300) {
        val x = (i % 7).toDouble() + 1.0
        when (i % 3) {
            0 -> v.add(Circle(x))
            1 -> v.add(Rect(x, x + 1))
            else -> v.add(Tri(x, x + 2))
        }
    }
    var a = 0.0
    var n = 0L
    for (r in 0 until 20_000) for (s in v) { a += s.area(); n += s.sides() }
    // The float as bits: a decimal rendering would round away the one-ulp
    // disagreement worth catching.
    println("a = ${(a * 1e6).toLong()} n = $n")
}
