// Enum classes with properties, a companion object and an object declaration,
// driving a bytecode interpreter.
//
// `when (op)` over an enum compiles to a `struct.get` of the entry's ordinal and a
// `br_table`, where `sealed` gets a chain of `ref.test`s. Each step also reads two
// constructor properties of the entry (`op.pushes - op.pops`) and updates the
// counters in `object Stats`, a singleton the module keeps in a global and
// reaches with `global.get` and `struct.get`/`struct.set`. The program is an
// Array<Op>, so every fetch is an `Array.get` and a `ref.test`/`ref.cast` back to
// Op. Interpreter dispatch is where an indirect branch costs the most, and this
// one comes from a real front end rather than a hand-written switch.
enum class Op(val pops: Int, val pushes: Int) {
    PUSH(0, 1), LOAD(0, 1), STORE(1, 0), ADD(2, 1), SUB(2, 1), MUL(2, 1),
    AND(2, 1), XOR(2, 1), DUP(1, 2), SWAP(2, 2), JNZ(1, 0), HALT(0, 0);

    companion object {
        fun decode(code: Int): Op = entries[code]
    }
}

object Stats {
    var steps = 0L
    var maxDepth = 0
}

fun run(code: Array<Op>, arg: IntArray, locals: IntArray): Int {
    val stack = IntArray(16)
    var sp = 0
    var pc = 0
    var depth = 0
    while (true) {
        val op = code[pc]
        val a = arg[pc]
        pc++
        Stats.steps++
        depth += op.pushes - op.pops
        if (depth > Stats.maxDepth) Stats.maxDepth = depth
        when (op) {
            Op.PUSH -> stack[sp++] = a
            Op.LOAD -> stack[sp++] = locals[a]
            Op.STORE -> locals[a] = stack[--sp]
            Op.ADD -> { sp--; stack[sp - 1] += stack[sp] }
            Op.SUB -> { sp--; stack[sp - 1] -= stack[sp] }
            Op.MUL -> { sp--; stack[sp - 1] *= stack[sp] }
            Op.AND -> { sp--; stack[sp - 1] = stack[sp - 1] and stack[sp] }
            Op.XOR -> { sp--; stack[sp - 1] = stack[sp - 1] xor stack[sp] }
            Op.DUP -> { stack[sp] = stack[sp - 1]; sp++ }
            Op.SWAP -> { val t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t }
            Op.JNZ -> if (stack[--sp] != 0) pc = a
            Op.HALT -> return stack[0]
        }
    }
}

fun main() {
    // for (i = n; i != 0; i--) acc = ((acc * 31 + i) & 0xFFFFF) ^ (i & 7)
    val prog = intArrayOf(
        1, 1,  0, 31,  5, 0,  1, 0,  3, 0,  0, 0xFFFFF,  6, 0,
        1, 0,  0, 7,  6, 0,  7, 0,  2, 1,
        1, 0,  0, 1,  4, 0,  8, 0,  2, 0,  10, 0,
        1, 1,  11, 0)
    val code = Array(prog.size / 2) { Op.decode(prog[2 * it]) }
    val arg = IntArray(prog.size / 2) { prog[2 * it + 1] }
    var s = 0L
    for (r in 0 until 12) {
        val locals = intArrayOf(100_000 + r, r)
        s = s * 31 + run(code, arg, locals)
    }
    println("s = $s steps = ${Stats.steps} depth = ${Stats.maxDepth}")
}
