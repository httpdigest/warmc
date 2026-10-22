// goto, and labeled break and continue.  lex is a tokenizer written as a
// state machine whose states are labels and whose transitions are gotos;
// LLVM turns it into one wasm `loop` per self-looping state, nested inside
// the loop for `start`, so the state lives in the program counter rather
// than in a variable.  matchBrackets checks bracket nesting line by line,
// leaving an inner loop over bytes with `continue lines` and `break lines`.
// The switch on a byte in each is lowered to `br_table`.
//
// Hand-written lexers and parsers are commonly shaped like this, and it is
// control flow with almost no arithmetic: every byte is one load and a few
// compares that pick one of several back edges.
package main

const (
	tIdent = iota + 1
	tNumber
	tString
	tOp
	tComment
)

// lex is a tokenizer whose states are labels and whose transitions are
// gotos, the way a hand-written lexer is often laid out.  It returns a
// checksum of the token kinds and lengths and the number of tokens.
//
//go:noinline
func lex(src []byte) (int, int) {
	sum, count := 0, 0
	i, start := 0, 0
	var c byte

start:
	if i >= len(src) {
		return sum, count
	}
	start = i
	c = src[i]
	i++
	switch {
	case c == ' ' || c == '\t' || c == '\n':
		goto start
	case c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_':
		goto ident
	case c >= '0' && c <= '9':
		goto number
	case c == '"':
		goto str
	case c == '/' && i < len(src) && src[i] == '/':
		goto comment
	default:
		goto op
	}

ident:
	if i < len(src) {
		c = src[i]
		if c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_' {
			i++
			goto ident
		}
	}
	sum += tIdent*(i-start) + int(src[start])
	count++
	goto start

number:
	if i < len(src) {
		c = src[i]
		if c >= '0' && c <= '9' || c == '.' || c == 'x' || c >= 'a' && c <= 'f' {
			i++
			goto number
		}
	}
	sum += tNumber * (i - start)
	count++
	goto start

str:
	if i < len(src) {
		c = src[i]
		i++
		if c == '\\' {
			i++
			goto str
		}
		if c != '"' {
			goto str
		}
	}
	sum += tString * (i - start)
	count++
	goto start

comment:
	for i < len(src) && src[i] != '\n' {
		i++
	}
	sum += tComment
	count++
	goto start

op:
	// Two-character operators.
	if i < len(src) {
		switch src[i] {
		case '=', '&', '|', '+', '-', '<', '>':
			i++
		}
	}
	sum += tOp*(i-start) + int(c)
	count++
	goto start
}

// matchBrackets checks nesting with labeled break and continue: the outer
// loop walks lines, the inner loop walks a line's bytes, and a mismatch
// abandons the rest of the line.
//
//go:noinline
func matchBrackets(src []byte) (int, int) {
	good, bad := 0, 0
	var stack [64]byte
	depth := 0
	i := 0
lines:
	for i < len(src) {
		for i < len(src) {
			c := src[i]
			i++
			switch c {
			case '\n':
				continue lines
			case '(', '[', '{':
				if depth == len(stack) {
					break lines
				}
				stack[depth] = c
				depth++
			case ')', ']', '}':
				want := byte('(')
				if c == ']' {
					want = '['
				} else if c == '}' {
					want = '{'
				}
				if depth == 0 || stack[depth-1] != want {
					bad++
					depth = 0
					for i < len(src) && src[i] != '\n' {
						i++
					}
					continue lines
				}
				depth--
				good++
			}
		}
	}
	return good, bad + depth
}

var lines = []string{
	"func main() { x := 42; y := x << 3 }\n",
	"for i := 0; i < len(xs); i++ { total += xs[i] * 0x1f }\n",
	"s := \"hello, \\\"world\\\"\" // a string with escapes\n",
	"if a >= b && c != d || e <= f { return (a + b) * [c] }\n",
	"m[key] = append(m[key], value) ) // unbalanced on purpose\n",
	"x = y/z - 3.25e0 + call(arg1, arg2, \"lit\")\n",
	"var buffer_1 [16]byte; ptr := &buffer_1[0]\n",
}

func main() {
	src := make([]byte, 0, 1<<15)
	for i := 0; i < 400; i++ {
		src = append(src, lines[(i*3)%len(lines)]...)
	}
	total := 0
	for r := 0; r < 2200; r++ {
		src[(r*131)%len(src)] = "({[]})ab9\" "[r%11]
		s, c := lex(src)
		g, b := matchBrackets(src)
		total += s + c*3 + g*5 + b*7
	}
	println("total =", total)
}
