// Interfaces and embedding: dynamic dispatch through itabs, struct embedding
// with promoted fields and methods (and shadowing at shallower depth),
// embedding pointers and interfaces in structs, interface embedding and
// method-set rules for value vs pointer receivers, type switches with
// multiple types per case and nil, two-value type assertions to concrete and
// interface types, comparisons of interfaces holding comparable values,
// interface values as map keys, the typed-nil-is-not-nil trap, an interface
// satisfied by a function type, sort.Interface over a wrapper, fmt.Stringer
// picked up through embedding, and a small expression evaluator built on an
// interface hierarchy.
//
// Go's interface method calls compile to a load of the function pointer out
// of the itab and a call_indirect on wasm; type switches compare type
// descriptor hashes, then pointers.  The itab cache is populated at run time
// for assertions to interface types.
package main

import (
	"errors"
	"fmt"
	"io"
	"math"
	"sort"
	"strings"
)

type Animal interface {
	Name() string
	Sound() string
}

type Walker interface{ Legs() int }

type Pet interface {
	Animal
	Walker
	Owner() string
}

type base struct {
	name  string
	owner string
}

func (b base) Name() string     { return b.name }
func (b base) Owner() string    { return b.owner }
func (b *base) Rename(n string) { b.name = n }

type Dog struct {
	base
	breed string
}

func (d Dog) Sound() string { return "woof" }
func (d Dog) Legs() int     { return 4 }

type Bird struct {
	*base
	canFly bool
}

func (b Bird) Sound() string { return "tweet" }
func (b Bird) Legs() int     { return 2 }
func (b Bird) Name() string  { return "bird:" + b.base.Name() } // shadows promoted Name

type Fish struct{ base }

func (Fish) Sound() string { return "..." }

type Logger struct {
	io.Writer
	prefix string
}

func (l Logger) Log(s string) { fmt.Fprintf(l, "%s%s\n", l.prefix, s) }

type upper struct{ sb *strings.Builder }

func (u upper) Write(p []byte) (int, error) { return u.sb.WriteString(strings.ToUpper(string(p))) }

// Function type implementing an interface.
type Transformer interface{ Transform(string) string }
type TransformFunc func(string) string

func (f TransformFunc) Transform(s string) string { return f(s) }

type chain []Transformer

func (c chain) Transform(s string) string {
	for _, t := range c {
		s = t.Transform(s)
	}
	return s
}

// Expression tree.
type Expr interface {
	Eval(env map[string]float64) (float64, error)
}
type Num float64
type Var string
type Bin struct {
	Op   byte
	L, R Expr
}
type Call struct {
	Fn   string
	Args []Expr
}

func (n Num) Eval(map[string]float64) (float64, error) { return float64(n), nil }
func (v Var) Eval(env map[string]float64) (float64, error) {
	x, ok := env[string(v)]
	if !ok {
		return 0, fmt.Errorf("undefined variable %q", string(v))
	}
	return x, nil
}
func (b Bin) Eval(env map[string]float64) (float64, error) {
	l, err := b.L.Eval(env)
	if err != nil {
		return 0, err
	}
	r, err := b.R.Eval(env)
	if err != nil {
		return 0, err
	}
	switch b.Op {
	case '+':
		return l + r, nil
	case '-':
		return l - r, nil
	case '*':
		return l * r, nil
	case '/':
		if r == 0 {
			return 0, errors.New("division by zero")
		}
		return l / r, nil
	}
	return 0, fmt.Errorf("bad op %c", b.Op)
}
func (c Call) Eval(env map[string]float64) (float64, error) {
	var args []float64
	for _, a := range c.Args {
		v, err := a.Eval(env)
		if err != nil {
			return 0, err
		}
		args = append(args, v)
	}
	switch c.Fn {
	case "sqrt":
		return math.Sqrt(args[0]), nil
	case "max":
		return math.Max(args[0], args[1]), nil
	}
	return 0, fmt.Errorf("unknown function %s", c.Fn)
}

func (n Num) String() string { return fmt.Sprint(float64(n)) }
func (v Var) String() string { return string(v) }
func (b Bin) String() string { return fmt.Sprintf("(%v %c %v)", b.L, b.Op, b.R) }
func (c Call) String() string {
	var s []string
	for _, a := range c.Args {
		s = append(s, fmt.Sprint(a))
	}
	return c.Fn + "(" + strings.Join(s, ", ") + ")"
}

type myErr struct{}

func (*myErr) Error() string { return "myErr" }

func mayFail(fail bool) error {
	var p *myErr
	if fail {
		p = &myErr{}
	}
	return p // typed nil when !fail
}

func classify(x any) string {
	switch v := x.(type) {
	case nil:
		return "nil"
	case int, int64:
		return fmt.Sprintf("integer %v", v)
	case float64:
		return fmt.Sprintf("float %g", v)
	case string:
		return "string of len " + fmt.Sprint(len(v))
	case error:
		return "error: " + v.Error()
	case fmt.Stringer:
		return "stringer: " + v.String()
	case []any:
		parts := make([]string, len(v))
		for i, e := range v {
			parts[i] = classify(e)
		}
		return "list[" + strings.Join(parts, "; ") + "]"
	case func() int:
		return fmt.Sprintf("func returning %d", v())
	case Pet:
		return "pet " + v.Name()
	case Animal:
		return "animal " + v.Name()
	default:
		return fmt.Sprintf("other %T", v)
	}
}

type byLen []string

func (b byLen) Len() int { return len(b) }
func (b byLen) Less(i, j int) bool {
	return len(b[i]) < len(b[j]) || (len(b[i]) == len(b[j]) && b[i] < b[j])
}
func (b byLen) Swap(i, j int) { b[i], b[j] = b[j], b[i] }

func main() {
	d := Dog{base{"rex", "ann"}, "lab"}
	b := Bird{&base{"tweety", "bob"}, true}
	f := Fish{base{"nemo", ""}}
	animals := []Animal{d, &d, b, f}
	for i, a := range animals {
		_, isPet := a.(Pet)
		w, isWalker := a.(Walker)
		legs := -1
		if isWalker {
			legs = w.Legs()
		}
		fmt.Printf("animal %d %T name=%s sound=%s pet=%v legs=%d\n", i, a, a.Name(), a.Sound(), isPet, legs)
	}
	// Promoted pointer method via addressable value.
	d.Rename("max")
	fmt.Println("renamed dog", d.Name(), "embedded field", d.base.name, d.owner)
	b.Rename("polly")
	fmt.Println("bird via embedded pointer", b.Name(), b.base.Name())
	// The interface copy of d holds the old value.
	fmt.Println("interface copy keeps old name:", animals[0].Name(), "pointer sees new:", animals[1].Name())

	// Method values and expressions through interfaces.
	soundOf := Animal.Sound
	nameOfDog := Dog.Name
	fmt.Println("method expr", soundOf(b), nameOfDog(d), (*Dog).Sound(&d))
	nv := animals[2].Name
	fmt.Println("method value", nv())

	// Interface embedding in struct: Logger wraps any writer.
	var sb strings.Builder
	lg := Logger{upper{&sb}, "> "}
	lg.Log("hello")
	lg.Log("world")
	fmt.Print(sb.String())
	var w io.Writer = lg
	_, isStringer := w.(fmt.Stringer)
	fmt.Println("logger is writer, stringer:", isStringer)

	// Function types implementing interfaces.
	tr := chain{
		TransformFunc(strings.TrimSpace),
		TransformFunc(strings.ToUpper),
		TransformFunc(func(s string) string { return strings.ReplaceAll(s, " ", "_") }),
		chain{TransformFunc(func(s string) string { return "[" + s + "]" })},
	}
	fmt.Println("transform", tr.Transform("  hello big world "))

	// Typed nil.
	e1 := mayFail(false)
	e2 := mayFail(true)
	var e3 error
	fmt.Println("typed nil != nil:", e1 != nil, "real error:", e2, "untyped nil == nil:", e3 == nil)
	fmt.Printf("typed nil type %T\n", e1)
	var pm *myErr
	fmt.Println("errors.As into typed nil target:", errors.As(e2, &pm), pm != nil)

	// Interface comparisons.
	var x, y any = 3, 3
	var z any = int64(3)
	var s1, s2 any = "a", "a"
	var a1, a2 any = [2]int{1, 2}, [2]int{1, 2}
	var p1, p2 any = &d, &d
	var st1, st2 any = struct{ A, B int }{1, 2}, struct{ A, B int }{1, 2}
	fmt.Println("compare", x == y, x == z, s1 == s2, a1 == a2, p1 == p2, st1 == st2, any(d) == any(d), x != nil)
	var an1 Animal = d
	var an2 Animal = d
	fmt.Println("animal iface equal", an1 == an2, an1 == Animal(Dog{}))

	// Interfaces as map keys.
	m := map[any]string{}
	keys := []any{1, int8(1), "1", 1.0, true, [2]int{1, 1}, struct{ S string }{"s"}, Num(1), Var("v"), nil}
	for i, k := range keys {
		m[k] = fmt.Sprint(i)
	}
	fmt.Println("iface map size", len(m))
	for _, k := range keys {
		fmt.Printf("  m[%T(%v)] = %s\n", k, k, m[k])
	}

	// Type switches.
	vals := []any{nil, 1, int64(2), 2.5, "hello", errors.New("boom"), Num(3), []any{1, "x", nil}, func() int { return 7 }, d, f, struct{}{}, &d, map[string]int{}}
	for _, v := range vals {
		fmt.Println("classify:", classify(v))
	}

	// sort.Interface wrapper.
	ws := byLen(strings.Fields("pear fig apple kiwi banana date plum cherry"))
	sort.Sort(ws)
	fmt.Println("byLen", ws)
	sort.Sort(sort.Reverse(ws))
	fmt.Println("byLen reversed", ws)

	// Expression evaluator.
	env := map[string]float64{"x": 3, "y": 4, "zero": 0}
	exprs := []Expr{
		Bin{'+', Num(1), Bin{'*', Num(2), Num(3)}},
		Call{"sqrt", []Expr{Bin{'+', Bin{'*', Var("x"), Var("x")}, Bin{'*', Var("y"), Var("y")}}}},
		Bin{'/', Num(1), Var("zero")},
		Bin{'-', Var("x"), Var("missing")},
		Call{"max", []Expr{Var("x"), Bin{'/', Var("y"), Num(8)}}},
		Call{"nope", nil},
		Bin{'%', Num(1), Num(2)},
	}
	for _, e := range exprs {
		v, err := e.Eval(env)
		fmt.Printf("eval %v = %v err=%v\n", e, v, err)
	}

	// Deep chain of interface calls built at run time.
	var expr Expr = Num(0)
	for i := 1; i <= 50; i++ {
		if i%2 == 0 {
			expr = Bin{'+', expr, Num(i)}
		} else {
			expr = Bin{'-', expr, Bin{'*', Num(i), Num(2)}}
		}
	}
	v, err := expr.Eval(nil)
	fmt.Println("deep expr", v, err)

	// Empty interface holding various kinds and assertions.
	var holder any = d
	if dd, ok := holder.(Dog); ok {
		fmt.Println("assert Dog ok", dd.breed)
	}
	if _, ok := holder.(*Dog); !ok {
		fmt.Println("assert *Dog fails on value")
	}
	if pet, ok := holder.(Pet); ok {
		fmt.Println("Dog is Pet with owner", pet.Owner(), "legs", pet.Legs())
	}
	if _, ok := any(f).(Pet); !ok {
		fmt.Println("Fish is not a Pet (no Legs)")
	}
	_, ok := any(b).(interface{ Rename(string) })
	fmt.Println("Bird has Rename via embedded *base:", ok)
	_, ok = any(f).(interface{ Rename(string) })
	fmt.Println("Fish value has Rename:", ok)
	_, ok = any(&f).(interface{ Rename(string) })
	fmt.Println("*Fish has Rename:", ok)
}
