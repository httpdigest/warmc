// hello.kt — a class and a list, so the module really does use wasm GC
class Point(val x: Int, val y: Int)

fun main() {
    val pts = (1..5).map { Point(it, it * it) }
    var s = 0
    for (p in pts) s += p.x + p.y
    println("points = ${pts.size}, sum = $s")
}

