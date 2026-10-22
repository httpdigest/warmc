// Method expressions and method values, stored and called through.  A small
// stack machine keeps its opcodes in a table of method expressions,
// `(*vm).push` and the like.  TinyGo gives each one a `$thunk` wrapper that
// accepts the extra context argument every func value is called with, so the
// dispatch loop is a load of a table index from a global array and a
// `call_indirect` to the thunk, which calls or inlines the method.  Method
// values bound to a value receiver (`s.apply`) and to a pointer receiver
// (`m.push`) are kept in a map and a slice; each is a `$bound` thunk that
// takes its receiver from the context, again reached by `call_indirect`.
//
// closures measures function values that close over local variables.  This
// is the other way Go programs build dispatch tables, and the one where the
// callee's receiver arrives through the context rather than as an argument.
package main

type vm struct {
	acc   int
	stack [32]int
	sp    int
	pc    int
}

func (m *vm) push(arg int) { m.stack[m.sp] = m.acc; m.sp = (m.sp + 1) & 31; m.acc = arg }
func (m *vm) add(arg int)  { m.sp = (m.sp - 1) & 31; m.acc += m.stack[m.sp] + arg }
func (m *vm) mul(arg int)  { m.acc = m.acc * (arg | 1) & 0xffffff }
func (m *vm) xor(arg int)  { m.acc ^= arg << 3 }
func (m *vm) jmp(arg int) {
	if m.acc&1 != 0 {
		m.pc += arg & 3
	}
}

// An op table of method expressions: each entry takes the receiver as its
// first argument.
var ops = [...]func(*vm, int){(*vm).push, (*vm).add, (*vm).mul, (*vm).xor, (*vm).jmp}

type insn struct{ op, arg int }

//go:noinline
func run(m *vm, prog []insn) int {
	m.pc = 0
	for m.pc < len(prog) {
		in := prog[m.pc]
		m.pc++
		ops[in.op](m, in.arg)
	}
	return m.acc
}

// A value type with value-receiver methods, taken as method values bound to
// different receivers and stored in a map and a slice.
type scaler struct{ k, off int }

func (s scaler) apply(x int) int { return x*s.k + s.off }
func (s scaler) invert(x int) int {
	if s.k == 0 {
		return x
	}
	return x/s.k - s.off
}

//go:noinline
func chain(fs []func(int) int, x int) int {
	for _, f := range fs {
		x = f(x) & 0xfffff
	}
	return x
}

func main() {
	const n = 512
	prog := make([]insn, n)
	x := uint32(31337)
	for i := range prog {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		prog[i] = insn{int(x % uint32(len(ops))), int(x>>8) & 255}
	}
	byName := map[string]func(int) int{}
	names := []string{"double", "triple", "shift", "neg", "half"}
	for i, nm := range names {
		s := scaler{i + 2, i*3 - 4}
		if i&1 == 0 {
			byName[nm] = s.apply
		} else {
			byName[nm] = s.invert
		}
	}
	// Bound methods over a pointer receiver too.
	var m vm
	pushes := []func(int){m.push, m.xor}

	fs := make([]func(int) int, 0, 64)
	for i := 0; i < 64; i++ {
		fs = append(fs, byName[names[(i*7)%len(names)]])
	}
	total := 0
	for r := 0; r < 55000; r++ {
		prog[r&(n-1)].arg = r & 255
		m.acc = r
		total += run(&m, prog)
		for i := 0; i < 16; i++ {
			pushes[i&1](i)
		}
		total += chain(fs, r) + m.acc
	}
	println("total =", total)
}
