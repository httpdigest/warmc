// A small interpreter: a lexer, a recursive-descent parser with operator
// precedence into an AST of interface-typed nodes, a tree-walking evaluator
// with lexical scopes, first-class functions and closures, recursion, while
// loops, if/else, and integer, string and list values; and a compiler from
// the same AST to a stack bytecode run on a switch-dispatched VM for the
// arithmetic subset, checked against the tree walker.  Runtime errors are
// reported as values.  Programs compute factorials, Fibonacci numbers,
// primes, a closure-based counter, string building, list sorting and the
// Collatz record holder, printing as they go.
package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

// ---- lexer ----

type tokKind int

const (
	tEOF tokKind = iota
	tNum
	tStr
	tIdent
	tOp
)

type token struct {
	kind tokKind
	text string
	num  int
	line int
}

func lex(src string) ([]token, error) {
	var toks []token
	line := 1
	i := 0
	for i < len(src) {
		c := src[i]
		switch {
		case c == '\n':
			line++
			i++
		case c == ' ' || c == '\t' || c == '\r':
			i++
		case c == '#':
			for i < len(src) && src[i] != '\n' {
				i++
			}
		case c >= '0' && c <= '9':
			j := i
			for j < len(src) && src[j] >= '0' && src[j] <= '9' {
				j++
			}
			n, _ := strconv.Atoi(src[i:j])
			toks = append(toks, token{tNum, src[i:j], n, line})
			i = j
		case c == '"':
			j := i + 1
			var sb strings.Builder
			for j < len(src) && src[j] != '"' {
				if src[j] == '\\' && j+1 < len(src) {
					j++
					switch src[j] {
					case 'n':
						sb.WriteByte('\n')
					default:
						sb.WriteByte(src[j])
					}
				} else {
					sb.WriteByte(src[j])
				}
				j++
			}
			if j >= len(src) {
				return nil, fmt.Errorf("line %d: unterminated string", line)
			}
			toks = append(toks, token{tStr, sb.String(), 0, line})
			i = j + 1
		case c == '_' || (c|0x20 >= 'a' && c|0x20 <= 'z'):
			j := i
			for j < len(src) && (src[j] == '_' || (src[j]|0x20 >= 'a' && src[j]|0x20 <= 'z') || (src[j] >= '0' && src[j] <= '9')) {
				j++
			}
			toks = append(toks, token{tIdent, src[i:j], 0, line})
			i = j
		default:
			two := ""
			if i+1 < len(src) {
				two = src[i : i+2]
			}
			switch two {
			case "==", "!=", "<=", ">=", "&&", "||":
				toks = append(toks, token{tOp, two, 0, line})
				i += 2
				continue
			}
			if strings.IndexByte("+-*/%<>=!(){}[],;", c) < 0 {
				return nil, fmt.Errorf("line %d: unexpected %q", line, c)
			}
			toks = append(toks, token{tOp, string(c), 0, line})
			i++
		}
	}
	return append(toks, token{tEOF, "", 0, line}), nil
}

// ---- AST ----

type node interface{}

type (
	numLit struct{ v int }
	strLit struct{ v string }
	ident  struct{ name string }
	unary  struct {
		op string
		x  node
	}
	binary struct {
		op   string
		l, r node
	}
	call struct {
		fn   node
		args []node
	}
	index struct{ x, i node }
	list  struct{ elems []node }
	fnLit struct {
		params []string
		body   []node
	}
	letStmt struct {
		name string
		x    node
	}
	assign struct {
		name string
		x    node
	}
	setIndex struct {
		name string
		i, x node
	}
	ifStmt struct {
		cond      node
		then, els []node
	}
	whileStmt struct {
		cond node
		body []node
	}
	retStmt  struct{ x node }
	exprStmt struct{ x node }
)

// ---- parser ----

type parser struct {
	toks []token
	pos  int
}

func (p *parser) peek() token { return p.toks[p.pos] }
func (p *parser) next() token { t := p.toks[p.pos]; p.pos++; return t }
func (p *parser) is(s string) bool {
	t := p.peek()
	return (t.kind == tOp || t.kind == tIdent) && t.text == s
}
func (p *parser) expect(s string) {
	if !p.is(s) {
		panic(fmt.Sprintf("line %d: expected %q, got %q", p.peek().line, s, p.peek().text))
	}
	p.pos++
}

func (p *parser) block() []node {
	p.expect("{")
	var out []node
	for !p.is("}") {
		out = append(out, p.stmt())
	}
	p.expect("}")
	return out
}

func (p *parser) stmt() node {
	switch {
	case p.is("let"):
		p.next()
		name := p.next().text
		p.expect("=")
		x := p.expr(0)
		p.expect(";")
		return letStmt{name, x}
	case p.is("if"):
		p.next()
		cond := p.expr(0)
		then := p.block()
		var els []node
		if p.is("else") {
			p.next()
			if p.is("if") {
				els = []node{p.stmt()}
			} else {
				els = p.block()
			}
		}
		return ifStmt{cond, then, els}
	case p.is("while"):
		p.next()
		cond := p.expr(0)
		return whileStmt{cond, p.block()}
	case p.is("return"):
		p.next()
		x := p.expr(0)
		p.expect(";")
		return retStmt{x}
	}
	if p.peek().kind == tIdent && p.toks[p.pos+1].text == "=" {
		name := p.next().text
		p.next()
		x := p.expr(0)
		p.expect(";")
		return assign{name, x}
	}
	if p.peek().kind == tIdent && p.toks[p.pos+1].text == "[" {
		save := p.pos
		name := p.next().text
		p.next()
		i := p.expr(0)
		p.expect("]")
		if p.is("=") {
			p.next()
			x := p.expr(0)
			p.expect(";")
			return setIndex{name, i, x}
		}
		p.pos = save
	}
	x := p.expr(0)
	p.expect(";")
	return exprStmt{x}
}

var prec = map[string]int{"||": 1, "&&": 2, "==": 3, "!=": 3, "<": 4, "<=": 4, ">": 4,
	">=": 4, "+": 5, "-": 5, "*": 6, "/": 6, "%": 6}

func (p *parser) expr(min int) node {
	l := p.unaryExpr()
	for {
		t := p.peek()
		pr, ok := prec[t.text]
		if t.kind != tOp || !ok || pr <= min {
			return l
		}
		p.next()
		r := p.expr(pr)
		l = binary{t.text, l, r}
	}
}

func (p *parser) unaryExpr() node {
	if p.is("-") || p.is("!") {
		op := p.next().text
		return unary{op, p.unaryExpr()}
	}
	return p.postfix(p.primary())
}

func (p *parser) postfix(x node) node {
	for {
		switch {
		case p.is("("):
			p.next()
			var args []node
			for !p.is(")") {
				args = append(args, p.expr(0))
				if p.is(",") {
					p.next()
				}
			}
			p.next()
			x = call{x, args}
		case p.is("["):
			p.next()
			i := p.expr(0)
			p.expect("]")
			x = index{x, i}
		default:
			return x
		}
	}
}

func (p *parser) primary() node {
	t := p.next()
	switch t.kind {
	case tNum:
		return numLit{t.num}
	case tStr:
		return strLit{t.text}
	case tIdent:
		if t.text == "fn" {
			p.expect("(")
			var params []string
			for !p.is(")") {
				params = append(params, p.next().text)
				if p.is(",") {
					p.next()
				}
			}
			p.next()
			return fnLit{params, p.block()}
		}
		return ident{t.text}
	}
	switch t.text {
	case "(":
		x := p.expr(0)
		p.expect(")")
		return x
	case "[":
		var elems []node
		for !p.is("]") {
			elems = append(elems, p.expr(0))
			if p.is(",") {
				p.next()
			}
		}
		p.next()
		return list{elems}
	}
	panic(fmt.Sprintf("line %d: unexpected %q", t.line, t.text))
}

// ---- evaluator ----

type value interface{}

type closure struct {
	params []string
	body   []node
	env    *env
}

type builtin func(args []value) (value, error)

type env struct {
	vars   map[string]value
	parent *env
}

func (e *env) lookup(n string) (value, bool) {
	for ; e != nil; e = e.parent {
		if v, ok := e.vars[n]; ok {
			return v, true
		}
	}
	return nil, false
}

func (e *env) set(n string, v value) bool {
	for ; e != nil; e = e.parent {
		if _, ok := e.vars[n]; ok {
			e.vars[n] = v
			return true
		}
	}
	return false
}

type returnSignal struct{ v value }

var out strings.Builder
var calls int

func show(v value) string {
	switch x := v.(type) {
	case nil:
		return "nil"
	case int:
		return strconv.Itoa(x)
	case string:
		return x
	case bool:
		return strconv.FormatBool(x)
	case *[]value:
		parts := make([]string, len(*x))
		for i, e := range *x {
			parts[i] = show(e)
		}
		return "[" + strings.Join(parts, ", ") + "]"
	case *closure:
		return fmt.Sprintf("<fn/%d>", len(x.params))
	case builtin:
		return "<builtin>"
	}
	return "?"
}

func truthy(v value) bool {
	switch x := v.(type) {
	case nil:
		return false
	case bool:
		return x
	case int:
		return x != 0
	case string:
		return x != ""
	}
	return true
}

func evalBlock(stmts []node, e *env) (value, *returnSignal, error) {
	for _, s := range stmts {
		switch st := s.(type) {
		case letStmt:
			v, err := eval(st.x, e)
			if err != nil {
				return nil, nil, err
			}
			e.vars[st.name] = v
		case assign:
			v, err := eval(st.x, e)
			if err != nil {
				return nil, nil, err
			}
			if !e.set(st.name, v) {
				return nil, nil, fmt.Errorf("assignment to undefined %s", st.name)
			}
		case setIndex:
			lv, _ := e.lookup(st.name)
			l, ok := lv.(*[]value)
			if !ok {
				return nil, nil, fmt.Errorf("%s is not a list", st.name)
			}
			iv, err := eval(st.i, e)
			if err != nil {
				return nil, nil, err
			}
			i, ok := iv.(int)
			if !ok || i < 0 || i >= len(*l) {
				return nil, nil, fmt.Errorf("index %v out of range", show(iv))
			}
			v, err := eval(st.x, e)
			if err != nil {
				return nil, nil, err
			}
			(*l)[i] = v
		case ifStmt:
			c, err := eval(st.cond, e)
			if err != nil {
				return nil, nil, err
			}
			body := st.els
			if truthy(c) {
				body = st.then
			}
			_, r, err := evalBlock(body, &env{map[string]value{}, e})
			if r != nil || err != nil {
				return nil, r, err
			}
		case whileStmt:
			for {
				c, err := eval(st.cond, e)
				if err != nil {
					return nil, nil, err
				}
				if !truthy(c) {
					break
				}
				_, r, err := evalBlock(st.body, &env{map[string]value{}, e})
				if r != nil || err != nil {
					return nil, r, err
				}
			}
		case retStmt:
			v, err := eval(st.x, e)
			if err != nil {
				return nil, nil, err
			}
			return nil, &returnSignal{v}, nil
		case exprStmt:
			if _, err := eval(st.x, e); err != nil {
				return nil, nil, err
			}
		}
	}
	return nil, nil, nil
}

func boolToInt(v value) value {
	if b, ok := v.(bool); ok {
		if b {
			return 1
		}
		return 0
	}
	return v
}

func arith(op string, a, b value) (value, error) {
	a, b = boolToInt(a), boolToInt(b)
	switch x := a.(type) {
	case int:
		y, ok := b.(int)
		if !ok {
			if s, ok := b.(string); ok && op == "+" {
				return strconv.Itoa(x) + s, nil
			}
			return nil, fmt.Errorf("type mismatch: %s %s %s", show(a), op, show(b))
		}
		switch op {
		case "+":
			return x + y, nil
		case "-":
			return x - y, nil
		case "*":
			return x * y, nil
		case "/", "%":
			if y == 0 {
				return nil, fmt.Errorf("division by zero")
			}
			if op == "/" {
				return x / y, nil
			}
			return x % y, nil
		case "<":
			return x < y, nil
		case "<=":
			return x <= y, nil
		case ">":
			return x > y, nil
		case ">=":
			return x >= y, nil
		}
	case string:
		switch op {
		case "+":
			return x + show(b), nil
		case "<":
			return x < show(b), nil
		case ">":
			return x > show(b), nil
		}
	}
	return nil, fmt.Errorf("bad operands for %s: %s, %s", op, show(a), show(b))
}

func eval(n node, e *env) (value, error) {
	switch x := n.(type) {
	case numLit:
		return x.v, nil
	case strLit:
		return x.v, nil
	case ident:
		v, ok := e.lookup(x.name)
		if !ok {
			return nil, fmt.Errorf("undefined: %s", x.name)
		}
		return v, nil
	case unary:
		v, err := eval(x.x, e)
		if err != nil {
			return nil, err
		}
		if x.op == "!" {
			return !truthy(v), nil
		}
		i, ok := v.(int)
		if !ok {
			return nil, fmt.Errorf("cannot negate %s", show(v))
		}
		return -i, nil
	case binary:
		l, err := eval(x.l, e)
		if err != nil {
			return nil, err
		}
		switch x.op {
		case "&&":
			if !truthy(l) {
				return false, nil
			}
			r, err := eval(x.r, e)
			return truthy(r), err
		case "||":
			if truthy(l) {
				return true, nil
			}
			r, err := eval(x.r, e)
			return truthy(r), err
		}
		r, err := eval(x.r, e)
		if err != nil {
			return nil, err
		}
		switch x.op {
		case "==":
			return show(l) == show(r), nil
		case "!=":
			return show(l) != show(r), nil
		}
		return arith(x.op, l, r)
	case list:
		vs := make([]value, len(x.elems))
		for i, el := range x.elems {
			v, err := eval(el, e)
			if err != nil {
				return nil, err
			}
			vs[i] = v
		}
		return &vs, nil
	case index:
		lv, err := eval(x.x, e)
		if err != nil {
			return nil, err
		}
		iv, err := eval(x.i, e)
		if err != nil {
			return nil, err
		}
		i, _ := iv.(int)
		switch l := lv.(type) {
		case *[]value:
			if i < 0 || i >= len(*l) {
				return nil, fmt.Errorf("index %d out of range [0,%d)", i, len(*l))
			}
			return (*l)[i], nil
		case string:
			if i < 0 || i >= len(l) {
				return nil, fmt.Errorf("index %d out of range in string", i)
			}
			return l[i : i+1], nil
		}
		return nil, fmt.Errorf("cannot index %s", show(lv))
	case fnLit:
		return &closure{x.params, x.body, e}, nil
	case call:
		fv, err := eval(x.fn, e)
		if err != nil {
			return nil, err
		}
		args := make([]value, len(x.args))
		for i, a := range x.args {
			if args[i], err = eval(a, e); err != nil {
				return nil, err
			}
		}
		calls++
		switch f := fv.(type) {
		case builtin:
			return f(args)
		case *closure:
			if len(args) != len(f.params) {
				return nil, fmt.Errorf("want %d args, got %d", len(f.params), len(args))
			}
			ne := &env{map[string]value{}, f.env}
			for i, p := range f.params {
				ne.vars[p] = args[i]
			}
			_, r, err := evalBlock(f.body, ne)
			if err != nil {
				return nil, err
			}
			if r != nil {
				return r.v, nil
			}
			return nil, nil
		}
		return nil, fmt.Errorf("not callable: %s", show(fv))
	}
	return nil, fmt.Errorf("unknown node %T", n)
}

func globals() *env {
	g := &env{vars: map[string]value{}}
	g.vars["print"] = builtin(func(args []value) (value, error) {
		parts := make([]string, len(args))
		for i, a := range args {
			parts[i] = show(a)
		}
		out.WriteString(strings.Join(parts, " ") + "\n")
		return nil, nil
	})
	g.vars["len"] = builtin(func(args []value) (value, error) {
		switch x := args[0].(type) {
		case string:
			return len(x), nil
		case *[]value:
			return len(*x), nil
		}
		return nil, fmt.Errorf("len of %s", show(args[0]))
	})
	g.vars["push"] = builtin(func(args []value) (value, error) {
		l, ok := args[0].(*[]value)
		if !ok {
			return nil, fmt.Errorf("push to %s", show(args[0]))
		}
		*l = append(*l, args[1])
		return l, nil
	})
	return g
}

func run(name, src string) {
	out.Reset()
	calls = 0
	toks, err := lex(src)
	if err != nil {
		fmt.Printf("== %s: lex error: %v\n", name, err)
		return
	}
	prog, perr := parseProgram(toks)
	if perr != "" {
		fmt.Printf("== %s: parse error: %s\n", name, perr)
		return
	}
	_, _, err = evalBlock(prog, globals())
	fmt.Printf("== %s (%d tokens, %d statements, %d calls)\n%s", name, len(toks), len(prog), calls, out.String())
	if err != nil {
		fmt.Println("runtime error:", err)
	}
}

func parseProgram(toks []token) (prog []node, perr string) {
	// The parser reports errors by panicking with a string; there is no
	// recover on this target, so the programs here are all well-formed and
	// syntax errors are checked for up front by a bracket balance pass.
	depth := 0
	for _, t := range toks {
		switch t.text {
		case "(", "[", "{":
			depth++
		case ")", "]", "}":
			depth--
		}
		if depth < 0 {
			return nil, fmt.Sprintf("line %d: unbalanced %q", t.line, t.text)
		}
	}
	if depth != 0 {
		return nil, "unbalanced brackets at end of input"
	}
	p := &parser{toks: toks}
	for p.peek().kind != tEOF {
		prog = append(prog, p.stmt())
	}
	return prog, ""
}

// ---- bytecode for arithmetic expressions ----

type op byte

const (
	opPush op = iota
	opLoad
	opAdd
	opSub
	opMul
	opDiv
	opMod
	opNeg
	opLt
	opGt
)

type instr struct {
	op  op
	arg int
}

func compile(n node, vars map[string]int, code []instr) []instr {
	switch x := n.(type) {
	case numLit:
		return append(code, instr{opPush, x.v})
	case ident:
		return append(code, instr{opLoad, vars[x.name]})
	case unary:
		return append(compile(x.x, vars, code), instr{opNeg, 0})
	case binary:
		code = compile(x.l, vars, code)
		code = compile(x.r, vars, code)
		m := map[string]op{"+": opAdd, "-": opSub, "*": opMul, "/": opDiv, "%": opMod, "<": opLt, ">": opGt}
		return append(code, instr{m[x.op], 0})
	}
	panic("cannot compile")
}

func vm(code []instr, slots []int) int {
	stack := make([]int, 0, 16)
	for _, in := range code {
		switch in.op {
		case opPush:
			stack = append(stack, in.arg)
		case opLoad:
			stack = append(stack, slots[in.arg])
		case opNeg:
			stack[len(stack)-1] = -stack[len(stack)-1]
		default:
			b := stack[len(stack)-1]
			a := stack[len(stack)-2]
			stack = stack[:len(stack)-2]
			var r int
			switch in.op {
			case opAdd:
				r = a + b
			case opSub:
				r = a - b
			case opMul:
				r = a * b
			case opDiv:
				r = a / b
			case opMod:
				r = a % b
			case opLt:
				if a < b {
					r = 1
				}
			case opGt:
				if a > b {
					r = 1
				}
			}
			stack = append(stack, r)
		}
	}
	return stack[0]
}

var programs = []struct{ name, src string }{
	{"arith", `
print(1 + 2 * 3, (1 + 2) * 3, 7 / 2, -7 / 2, 7 % 3, -7 % 3, 2 * -3);
print(1 < 2, 2 <= 1, 3 == 3, "a" == "a", 1 != 2, !0, !1 && 1, 0 || 5);
`},
	{"factorial", `
let fact = fn(n) { if n <= 1 { return 1; } return n * fact(n - 1); };
let i = 0;
while i < 13 { print(i, fact(i)); i = i + 1; }
`},
	{"fib", `
let fib = fn(n) { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); };
let a = 0; let b = 1; let k = 0;
while k < 30 { if k % 5 == 0 { print(k, a, fib(k % 20)); } let t = a + b; a = b; b = t; k = k + 1; }
`},
	{"primes", `
let primes = [];
let n = 2;
while len(primes) < 40 {
  let isp = 1; let j = 0;
  while j < len(primes) && primes[j] * primes[j] <= n {
    if n % primes[j] == 0 { isp = 0; }
    j = j + 1;
  }
  if isp { push(primes, n); }
  n = n + 1;
}
print(primes);
`},
	{"closures", `
let counter = fn(start) { let c = start; return fn(step) { c = c + step; return c; }; };
let a = counter(10); let b = counter(100);
print(a(1), a(1), b(5), a(10), b(5));
let compose = fn(f, g) { return fn(x) { return g(f(x)); }; };
let inc = fn(x) { return x + 1; }; let dbl = fn(x) { return x * 2; };
print(compose(inc, dbl)(5), compose(dbl, inc)(5), compose(compose(inc, inc), dbl)(0));
`},
	{"strings", `
let s = ""; let i = 0;
while i < 10 { s = s + i; if i % 3 == 0 { s = s + "-"; } i = i + 1; }
print(s, len(s), s[0], s[len(s) - 1]);
let rev = fn(x) { let r = ""; let k = len(x) - 1; while k >= 0 { r = r + x[k]; k = k - 1; } return r; };
print(rev("interpreter"), rev(""), "abc" < "abd", "x" + 1 + 2);
`},
	{"sort", `
let xs = [5, 3, 9, 1, 7, 2, 8, 6, 4, 0];
let n = len(xs); let i = 0;
while i < n {
  let j = 0;
  while j < n - 1 - i {
    if xs[j] > xs[j + 1] { let t = xs[j]; xs[j] = xs[j + 1]; xs[j + 1] = t; }
    j = j + 1;
  }
  i = i + 1;
}
print(xs);
let nested = [[1, 2], [3, [4, 5]], "six"];
print(nested, nested[1][1][0], len(nested));
`},
	{"collatz", `
let steps = fn(n) { let c = 0; while n != 1 { if n % 2 == 0 { n = n / 2; } else { n = 3 * n + 1; } c = c + 1; } return c; };
let best = 0; let arg = 0; let k = 1;
while k < 300 { let s = steps(k); if s > best { best = s; arg = k; print("record", k, s); } k = k + 1; }
print(arg, best);
`},
	{"errors", `
print("before");
let xs = [1, 2, 3];
print(xs[5]);
print("never");
`},
	{"divzero", `
let f = fn(x) { return 10 / x; };
print(f(5), f(2));
print(f(0));
`},
	{"undefined", `
let x = 1;
print(x + y);
`},
	{"lexerr", `let x = 1 $ 2;`},
	{"parseerr", `let f = fn(x) { return x; ;`},
}

var bias = 0

func init() {
	if len(os.Args) > 1000 {
		bias = 1
	}
}

func main() {
	for _, p := range programs {
		run(p.name, p.src)
	}
	fmt.Println("== bytecode vs tree walker")
	exprs := []string{"a + b * c", "(a + b) * c - a % 3", "a * a - b * b + c / 2",
		"-(a - b) * (c + 7) / (b + 1)", "(a < b) + (b > c) * 10", "a % 7 * b % 5 + -c"}
	for _, src := range exprs {
		toks, _ := lex(src + ";")
		p := &parser{toks: toks}
		ast := p.expr(0)
		code := compile(ast, map[string]int{"a": 0, "b": 1, "c": 2}, nil)
		agree := 0
		sum := 0
		for a := -5; a <= 5; a++ {
			for b := 0; b <= 6; b++ {
				for c := 1; c <= 4; c++ {
					slots := []int{a + bias, b, c}
					got := vm(code, slots)
					e := globals()
					e.vars["a"], e.vars["b"], e.vars["c"] = slots[0], slots[1], slots[2]
					want, err := eval(ast, e)
					if b, ok := want.(bool); ok {
						if b {
							want = 1
						} else {
							want = 0
						}
					}
					if err == nil && want == got {
						agree++
					}
					sum += got
				}
			}
		}
		fmt.Printf("%-32s %d instrs, %d/%d agree, sum %d\n", src, len(code), agree, 11*7*4, sum)
	}
}
