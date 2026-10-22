// Control flow shapes a compiler has to lower: dense integer switches (which
// may become jump tables), sparse ones (binary search), switches on strings
// (by length, then value), on runes and on types, fallthrough chains,
// switch with init statements and no tag, goto forward and backward
// (including a goto-based state machine), labeled break and continue out of
// nested for/switch/select, loops with multiple exits, short-circuit && and
// || with side effects in their operands, and a bytecode interpreter whose
// main loop is one big switch.
//
// Go's wasm back end emits all of this as nested blocks with br/br_if/
// br_table inside one loop per function, plus the resume-point dispatch at
// the top of every function, so the branch structure warmc sees is unusual.
package main

import (
	"fmt"
	"strings"
)

//go:noinline
func dense(x int) string {
	switch x {
	case 0:
		return "zero"
	case 1:
		return "one"
	case 2:
		return "two"
	case 3:
		return "three"
	case 4:
		return "four"
	case 5:
		return "five"
	case 6:
		return "six"
	case 7:
		return "seven"
	case 8:
		return "eight"
	case 9:
		return "nine"
	case 10:
		return "ten"
	case 11:
		return "eleven"
	case 12, 13, 14:
		return "teen-ish"
	case 15:
		return "fifteen"
	}
	return "other"
}

//go:noinline
func sparse(x int64) int {
	switch x {
	case -1000000:
		return 1
	case -77:
		return 2
	case 0:
		return 3
	case 13:
		return 4
	case 1024:
		return 5
	case 65535:
		return 6
	case 1 << 40:
		return 7
	case 9223372036854775807:
		return 8
	}
	return 0
}

//go:noinline
func keyword(s string) int {
	switch s {
	case "break", "case", "chan", "const", "continue":
		return 1
	case "default", "defer", "else", "fallthrough", "for":
		return 2
	case "func", "go", "goto", "if", "import":
		return 3
	case "interface", "map", "package", "range", "return":
		return 4
	case "select", "struct", "switch", "type", "var":
		return 5
	case "":
		return -1
	}
	return 0
}

//go:noinline
func classifyRune(r rune) string {
	switch {
	case r >= 'a' && r <= 'z':
		return "lower"
	case r >= 'A' && r <= 'Z':
		return "upper"
	case r >= '0' && r <= '9':
		return "digit"
	}
	switch r {
	case ' ', '\t', '\n':
		return "space"
	case '(', ')', '[', ']', '{', '}':
		return "bracket"
	case 'é', 'ß', '日':
		return "non-ascii-known"
	}
	return "other"
}

//go:noinline
func fall(x int) []string {
	var out []string
	switch {
	case x > 100:
		out = append(out, ">100")
		fallthrough
	case x > 10:
		out = append(out, ">10")
		fallthrough
	case x > 1:
		out = append(out, ">1")
		if x == 50 {
			break
		}
		fallthrough
	case x > 0:
		out = append(out, ">0")
	default:
		out = append(out, "<=0")
	}
	return out
}

//go:noinline
func typeName(v any) string {
	switch x := v.(type) {
	case nil:
		return "nil"
	case int, int8, int16:
		return fmt.Sprintf("small-int %v", x)
	case int64:
		return fmt.Sprintf("int64 %d", x)
	case string:
		if len(x) > 3 {
			return "long string"
		}
		return "short string"
	case []byte:
		return fmt.Sprintf("bytes len %d", len(x))
	case error:
		return "error " + x.Error()
	case fmt.Stringer:
		return "stringer"
	case func() int:
		return fmt.Sprint("func ", x())
	}
	return fmt.Sprintf("unknown %T", v)
}

// goto-based state machine: recognise numbers like -12.5e+3.
//
//go:noinline
func isNumber(s string) bool {
	i := 0
	if i < len(s) && (s[i] == '+' || s[i] == '-') {
		i++
	}
	digits := 0
intPart:
	if i < len(s) && s[i] >= '0' && s[i] <= '9' {
		i++
		digits++
		goto intPart
	}
	if i < len(s) && s[i] == '.' {
		i++
		goto fracPart
	}
	goto exponent
fracPart:
	if i < len(s) && s[i] >= '0' && s[i] <= '9' {
		i++
		digits++
		goto fracPart
	}
exponent:
	if digits == 0 {
		return false
	}
	if i < len(s) && (s[i] == 'e' || s[i] == 'E') {
		i++
		if i < len(s) && (s[i] == '+' || s[i] == '-') {
			i++
		}
		ed := 0
		for i < len(s) && s[i] >= '0' && s[i] <= '9' {
			i++
			ed++
		}
		if ed == 0 {
			return false
		}
	}
	return i == len(s)
}

var calls []string

func side(name string, v bool) bool {
	calls = append(calls, name)
	return v
}

// A tiny bytecode VM.
const (
	opPush = iota
	opAdd
	opSub
	opMul
	opDup
	opSwap
	opJz
	opJmp
	opDec
	opPrint
	opHalt
	opOver
	opDrop
)

//go:noinline
func vm(code []int) (out []int, steps int) {
	var st []int
	pc := 0
	for {
		steps++
		switch code[pc] {
		case opPush:
			st = append(st, code[pc+1])
			pc += 2
		case opAdd:
			st[len(st)-2] += st[len(st)-1]
			st = st[:len(st)-1]
			pc++
		case opSub:
			st[len(st)-2] -= st[len(st)-1]
			st = st[:len(st)-1]
			pc++
		case opMul:
			st[len(st)-2] *= st[len(st)-1]
			st = st[:len(st)-1]
			pc++
		case opDup:
			st = append(st, st[len(st)-1])
			pc++
		case opSwap:
			st[len(st)-1], st[len(st)-2] = st[len(st)-2], st[len(st)-1]
			pc++
		case opOver:
			st = append(st, st[len(st)-2])
			pc++
		case opDrop:
			st = st[:len(st)-1]
			pc++
		case opJz:
			v := st[len(st)-1]
			st = st[:len(st)-1]
			if v == 0 {
				pc = code[pc+1]
			} else {
				pc += 2
			}
		case opJmp:
			pc = code[pc+1]
		case opDec:
			st[len(st)-1]--
			pc++
		case opPrint:
			out = append(out, st[len(st)-1])
			pc++
		case opHalt:
			return out, steps
		default:
			panic(fmt.Sprintf("bad opcode %d at %d", code[pc], pc))
		}
	}
}

func main() {
	var ds []string
	for i := -2; i <= 17; i++ {
		ds = append(ds, dense(i))
	}
	fmt.Println("dense", strings.Join(ds, ","))
	var sp []int
	for _, v := range []int64{-1000000, -77, -76, 0, 13, 14, 1024, 65535, 65536, 1 << 40, 1<<40 + 1, 9223372036854775807, -9223372036854775808} {
		sp = append(sp, sparse(v))
	}
	fmt.Println("sparse", sp)
	var kw []int
	for _, w := range strings.Fields("break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var Break breaks x func_ go2 caseX") {
		kw = append(kw, keyword(w))
	}
	kw = append(kw, keyword(""))
	fmt.Println("keywords", kw)
	var rc []string
	for _, r := range "aZ5 \t([日é?ß" {
		rc = append(rc, classifyRune(r))
	}
	fmt.Println("runes", rc)
	for _, x := range []int{500, 50, 20, 5, 1, 0, -3} {
		fmt.Println("fallthrough", x, fall(x))
	}
	for _, v := range []any{nil, 3, int8(-1), int64(1 << 40), "abc", "abcdef", []byte("xy"), fmt.Errorf("e"), func() int { return 9 }, 2.5, struct{}{}} {
		fmt.Println("type switch:", typeName(v))
	}
	for _, s := range []string{"0", "-12", "+3.25", ".5", "5.", "1e10", "1E-7", "-1.5e+30", "", "-", ".", "1e", "1e+", "12a", "1.2.3", "--1", "e5"} {
		fmt.Printf("isNumber(%q) = %v\n", s, isNumber(s))
	}

	// Labeled break/continue out of nested constructs.
	var found []string
outer:
	for i := 0; i < 10; i++ {
		for j := 0; j < 10; j++ {
			switch {
			case j > i:
				continue outer
			case i*j == 42:
				found = append(found, fmt.Sprintf("%dx%d", i, j))
				break outer
			case (i+j)%7 == 0 && i > 0:
				found = append(found, fmt.Sprintf("%d+%d", i, j))
				continue outer
			}
		}
	}
	fmt.Println("labeled", found)
	ch := make(chan int, 10)
	for i := 0; i < 10; i++ {
		ch <- i
	}
	close(ch)
	got := 0
loop:
	for {
		select {
		case v, ok := <-ch:
			if !ok {
				break loop
			}
			if v == 7 {
				break loop
			}
			got += v
		}
	}
	fmt.Println("break out of select", got, len(ch))

	// Backward goto loop.
	n, acc := 0, 1
again:
	if n < 20 {
		acc = acc*3 + n
		n++
		goto again
	}
	fmt.Println("goto loop", n, acc)

	// Short-circuit evaluation order.
	calls = nil
	r1 := side("a", false) && side("b", true) || side("c", true) && side("d", false) || side("e", true)
	fmt.Println("short-circuit", r1, calls)
	calls = nil
	r2 := !(side("p", true) || side("q", true)) && side("r", true)
	fmt.Println("short-circuit 2", r2, calls)

	// Loop with multiple exits and a for-range over a string with continue.
	vowels, consonants, stopAt := 0, 0, -1
	for i, r := range "the quick brown fox jumps over the lazy dog" {
		switch r {
		case ' ':
			continue
		case 'a', 'e', 'i', 'o', 'u':
			vowels++
		case 'z':
			stopAt = i
		default:
			consonants++
		}
		if stopAt >= 0 {
			break
		}
	}
	fmt.Println("multi-exit loop", vowels, consonants, stopAt)

	// Bytecode VM: factorial of 10 and a countdown printing every value.
	fact := []int{
		opPush, 1, // 0
		opPush, 10, // 2
		opDup,    // 4
		opJz, 17, // 5
		opSwap,   // 7: n acc
		opOver,   // 8: n acc n
		opMul,    // 9: n acc*n
		opSwap,   // 10: acc' n
		opDec,    // 11
		opJmp, 4, // 12
		opHalt,  // 14 (unused)
		opHalt,  // 15 (unused)
		opHalt,  // 16 (unused)
		opDrop,  // 17: acc
		opPrint, // 18
		opHalt,  // 19
	}
	out, steps := vm(fact)
	fmt.Println("vm factorial", out, "steps", steps)
	countdown := []int{opPush, 12, opPrint, opDec, opDup, opJz, 9, opJmp, 2, opHalt}
	out, steps = vm(countdown)
	fmt.Println("vm countdown", out, "steps", steps)
	func() {
		defer func() { fmt.Println("vm bad opcode:", recover()) }()
		vm([]int{opPush, 1, 99})
	}()

	// A switch inside a hot loop: histogram of digit classes of i*i.
	hist := make([]int, 6)
	for i := 0; i < 20000; i++ {
		switch d := (i * i / 7) % 10; d {
		case 0:
			hist[0]++
		case 1, 3, 5:
			hist[1]++
		case 2, 4:
			hist[2]++
		case 6:
			hist[3]++
		case 7, 8:
			hist[4]++
		default:
			hist[5]++
		}
	}
	fmt.Println("histogram", hist)
}
