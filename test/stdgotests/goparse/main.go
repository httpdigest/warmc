// The Go toolchain's own front end as a library: go/scanner tokenising a
// source file (with positions and automatic semicolons), go/parser building
// an AST with comments, go/ast walking it (Inspect counting node kinds, a
// Visitor collecting identifiers, filtering exports), go/printer and
// go/format pretty-printing a deliberately mangled file, go/constant doing
// exact arbitrary-precision constant arithmetic, go/token positions, and
// go/types type-checking a self-contained package (no imports needed) to
// report the type of every expression, method sets, constant values,
// assignability, and the exact type-checker error messages for a broken one.
//
// This is a very large amount of ordinary Go code -- big switch statements,
// maps keyed by pointers, deep recursion over trees, interfaces everywhere --
// which is exactly what TinyGo-compiled corpora never contain.
package main

import (
	"bytes"
	"fmt"
	"go/ast"
	"go/constant"
	"go/format"
	"go/parser"
	"go/printer"
	"go/scanner"
	"go/token"
	"go/types"
	"sort"
	"strings"
)

const src = `// Package shapes is a test input.
package shapes

import "fmt"

// Shape is anything with an area.
type Shape interface {
	Area() float64
	Perimeter() float64
}

// Rect is a rectangle.
type Rect struct {
	W, H float64 // dimensions
	name string
}

func (r Rect) Area() float64      { return r.W * r.H }
func (r Rect) Perimeter() float64 { return 2 * (r.W + r.H) }
func (r *Rect) Scale(f float64)   { r.W *= f; r.H *= f }

const (
	Big   = 1 << 100
	Small = Big >> 99
	Pi    = 3.14159265358979323846264338327950288419716939937510582097494459
	Name  = "shapes" + "!"
)

var registry = map[string]Shape{}

func Register(name string, s Shape) error {
	if _, ok := registry[name]; ok {
		return fmt.Errorf("duplicate %q", name)
	}
	registry[name] = s
	return nil
}

func Total(shapes ...Shape) (area, perim float64) {
	for i, s := range shapes {
		_ = i
		area += s.Area()
		perim += s.Perimeter()
	}
	return
}

func generic[T ~int | ~float64](xs []T) (sum T) {
	for _, x := range xs {
		sum += x
	}
	return sum
}
`

const typecheckSrc = `package calc

type Number interface{ ~int | ~float64 }

type Vec struct{ X, Y float64 }

func (v Vec) Add(o Vec) Vec    { return Vec{v.X + o.X, v.Y + o.Y} }
func (v *Vec) Scale(k float64) { v.X *= k; v.Y *= k }

type Celsius float64

const Limit = 1 << 10
const Ratio = Limit / 3.0

func Sum[T Number](xs ...T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

func use() {
	v := Vec{1, 2}
	w := v.Add(Vec{3, 4})
	p := &w
	p.Scale(2)
	n := Sum(1, 2, 3)
	f := Sum(1.5, 2)
	c := Celsius(36.6)
	var arr [Limit / 256]byte
	m := map[string][]Vec{"a": {v, w}}
	ch := make(chan<- int, len(arr))
	fn := func(a int, b ...string) (bool, error) { return a > 0, nil }
	_, _, _, _, _, _ = n, f, c, m, ch, fn
}
`

const brokenSrc = `package broken

func f() int {
	var x int = "string"
	y := undefinedName + 1
	var z float64
	z = x
	return "wrong"
}

func g() { f(1) }
`

const mangled = `package   main
import ("fmt";"strings")
func   main( ) {
x:=[]int{1,2,
3}
for i,v:=range x{if v>1{fmt.Println(i,v)}else{fmt.Println(strings.Repeat("-",v))}}
type T struct{A int;B string}
var t=T{A:1,B:"x"}
_=t
}`

type identCollector struct {
	idents map[string]int
	depth  int
	max    int
}

func (c *identCollector) Visit(n ast.Node) ast.Visitor {
	if n == nil {
		c.depth--
		return nil
	}
	c.depth++
	c.max = max(c.max, c.depth)
	if id, ok := n.(*ast.Ident); ok {
		c.idents[id.Name]++
	}
	return c
}

func main() {
	fset := token.NewFileSet()

	// Scanner.
	var s scanner.Scanner
	file := fset.AddFile("shapes.go", fset.Base(), len(src))
	var errs []string
	s.Init(file, []byte(src), func(pos token.Position, msg string) { errs = append(errs, msg) }, scanner.ScanComments)
	counts := map[token.Token]int{}
	ntok := 0
	for {
		pos, tok, lit := s.Scan()
		if tok == token.EOF {
			break
		}
		ntok++
		counts[tok]++
		if ntok <= 40 {
			fmt.Printf("tok %-12s %-10s %q\n", fset.Position(pos), tok, lit)
		}
	}
	var toks []string
	for t, n := range counts {
		toks = append(toks, fmt.Sprintf("%s:%d", t, n))
	}
	sort.Strings(toks)
	fmt.Println("tokens", ntok, "errors", errs)
	fmt.Println("token counts", strings.Join(toks, " "))

	// Parser + AST.
	f, err := parser.ParseFile(fset, "shapes.go", src, parser.ParseComments|parser.AllErrors)
	fmt.Println("parse err:", err)
	fmt.Println("package", f.Name.Name, "imports", len(f.Imports), "decls", len(f.Decls), "comments", len(f.Comments))
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *ast.FuncDecl:
			recv := ""
			if d.Recv != nil {
				var b bytes.Buffer
				printer.Fprint(&b, fset, d.Recv.List[0].Type)
				recv = "(" + b.String() + ") "
			}
			fmt.Printf("func %s%s params=%d results=%d tparams=%v line=%d doc=%q\n", recv, d.Name.Name, d.Type.Params.NumFields(), d.Type.Results.NumFields(), d.Type.TypeParams != nil, fset.Position(d.Pos()).Line, d.Doc.Text())
		case *ast.GenDecl:
			fmt.Printf("gendecl %s specs=%d line=%d doc=%q\n", d.Tok, len(d.Specs), fset.Position(d.Pos()).Line, strings.TrimSpace(d.Doc.Text()))
		}
	}
	kinds := map[string]int{}
	ast.Inspect(f, func(n ast.Node) bool {
		if n != nil {
			kinds[fmt.Sprintf("%T", n)]++
		}
		return true
	})
	var ks []string
	for k := range kinds {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	for _, k := range ks {
		fmt.Printf("node %-22s %d\n", k, kinds[k])
	}
	ic := &identCollector{idents: map[string]int{}}
	ast.Walk(ic, f)
	var top []string
	for k, v := range ic.idents {
		if v >= 3 {
			top = append(top, fmt.Sprintf("%s=%d", k, v))
		}
	}
	sort.Strings(top)
	fmt.Println("frequent identifiers", top, "max depth", ic.max)
	exp := ast.FileExports(f)
	fmt.Println("has exports:", exp, "decls after filter", len(f.Decls))

	// go/format of a mangled file.
	formatted, err := format.Source([]byte(mangled))
	fmt.Printf("format err=%v\n%s", err, formatted)
	_, err = format.Source([]byte("package x\nfunc {"))
	fmt.Println("format broken:", err)

	// Printer config on the parsed (filtered) file.
	var pb bytes.Buffer
	cfg := printer.Config{Mode: printer.UseSpaces | printer.TabIndent, Tabwidth: 4}
	fn := f.Decls[len(f.Decls)-1]
	cfg.Fprint(&pb, fset, fn)
	fmt.Println("printed decl:\n" + pb.String())

	// Parse expressions.
	for _, e := range []string{"a + b*c", "f(x)[i].y", "func(x int) int { return x }(3)", "[]map[string]*T{}", "a +", "x.(type)"} {
		expr, err := parser.ParseExpr(e)
		if err != nil {
			fmt.Printf("ParseExpr(%q): %v\n", e, err)
			continue
		}
		var b bytes.Buffer
		printer.Fprint(&b, token.NewFileSet(), expr)
		fmt.Printf("ParseExpr(%q) = %T %s\n", e, expr, b.String())
	}

	// go/constant.
	big := constant.Shift(constant.MakeInt64(1), token.SHL, 200)
	third := constant.BinaryOp(constant.MakeInt64(1), token.QUO, constant.MakeInt64(3))
	fmt.Println("2^200 =", big.ExactString())
	fmt.Println("1/3 exact =", third.ExactString(), "kind", third.Kind(), "float approx", third.String())
	sum := constant.BinaryOp(third, token.ADD, constant.MakeFromLiteral("0.6666666666666666666666666666666666666666666666666666666666667", token.FLOAT, 0))
	fmt.Println("1/3 + 0.666... =", sum.String())
	pi := constant.MakeFromLiteral("3.14159265358979323846264338327950288", token.FLOAT, 0)
	fmt.Println("pi*2^100 =", constant.BinaryOp(pi, token.MUL, constant.Shift(constant.MakeInt64(1), token.SHL, 100)).String())
	fmt.Println("compare", constant.Compare(big, token.GTR, constant.MakeUint64(1<<63)), "sign", constant.Sign(constant.MakeInt64(-5)))
	str := constant.BinaryOp(constant.MakeString("go"), token.ADD, constant.MakeString("pher"))
	fmt.Println("string const", str, constant.StringVal(str))
	cx := constant.BinaryOp(constant.MakeImag(constant.MakeInt64(2)), token.MUL, constant.MakeImag(constant.MakeInt64(3)))
	fmt.Println("2i*3i =", cx, "int div", constant.BinaryOp(constant.MakeInt64(-7), token.QUO_ASSIGN, constant.MakeInt64(2)), "rem", constant.BinaryOp(constant.MakeInt64(-7), token.REM, constant.MakeInt64(2)))
	if v, exact := constant.Int64Val(constant.MakeInt64(42)); exact {
		fmt.Println("Int64Val", v)
	}
	fl, exact := constant.Float64Val(third)
	fmt.Println("Float64Val(1/3)", fl, exact)

	// go/types.
	tf, err := parser.ParseFile(fset, "calc.go", typecheckSrc, 0)
	if err != nil {
		fmt.Println("parse calc:", err)
		return
	}
	info := &types.Info{
		Types:      map[ast.Expr]types.TypeAndValue{},
		Defs:       map[*ast.Ident]types.Object{},
		Uses:       map[*ast.Ident]types.Object{},
		Instances:  map[*ast.Ident]types.Instance{},
		Selections: map[*ast.SelectorExpr]*types.Selection{},
	}
	conf := types.Config{}
	pkg, err := conf.Check("calc", fset, []*ast.File{tf}, info)
	fmt.Println("typecheck err:", err)
	scope := pkg.Scope()
	for _, name := range scope.Names() {
		obj := scope.Lookup(name)
		line := fmt.Sprintf("object %-8s %-10T %s", name, obj, obj.Type())
		if c, ok := obj.(*types.Const); ok {
			line += " = " + c.Val().ExactString()
		}
		fmt.Println(line)
	}
	vec := scope.Lookup("Vec").Type()
	for _, t := range []types.Type{vec, types.NewPointer(vec)} {
		ms := types.NewMethodSet(t)
		var names []string
		for i := 0; i < ms.Len(); i++ {
			names = append(names, ms.At(i).Obj().Name())
		}
		fmt.Println("method set of", t, names)
	}
	var exprs []string
	for e, tv := range info.Types {
		pos := fset.Position(e.Pos())
		if pos.Line < 22 {
			continue
		}
		var b bytes.Buffer
		printer.Fprint(&b, fset, e)
		desc := tv.Type.String()
		if tv.Value != nil {
			desc += " = " + tv.Value.String()
		}
		mode := "value"
		switch {
		case tv.IsType():
			mode = "type"
		case tv.IsBuiltin():
			mode = "builtin"
		case tv.IsVoid():
			mode = "void"
		case tv.Addressable():
			mode = "addressable"
		}
		exprs = append(exprs, fmt.Sprintf("%3d:%02d %-28s %-11s %s", pos.Line, pos.Column, b.String(), mode, desc))
	}
	sort.Strings(exprs)
	for _, e := range exprs {
		fmt.Println("expr", e)
	}
	var insts []string
	for id, inst := range info.Instances {
		insts = append(insts, fmt.Sprintf("%s at %d:%d -> %v %s", id.Name, fset.Position(id.Pos()).Line, fset.Position(id.Pos()).Column, inst.TypeArgs.At(0), inst.Type))
	}
	sort.Strings(insts)
	for _, s := range insts {
		fmt.Println("instance", s)
	}
	var sels []string
	for se, sel := range info.Selections {
		sels = append(sels, fmt.Sprintf("%d:%02d %s.%s kind=%d indirect=%v type=%s", fset.Position(se.Pos()).Line, fset.Position(se.Pos()).Column, types.ExprString(se.X), se.Sel.Name, sel.Kind(), sel.Indirect(), sel.Type()))
	}
	sort.Strings(sels)
	for _, s := range sels {
		fmt.Println("selection", s)
	}
	fl64 := types.Typ[types.Float64]
	celsius := scope.Lookup("Celsius").Type()
	fmt.Println("assignable Celsius->float64", types.AssignableTo(celsius, fl64), "convertible", types.ConvertibleTo(celsius, fl64), "identical", types.Identical(celsius.Underlying(), fl64))
	sizes := types.SizesFor("gc", "wasm")
	fmt.Println("wasm sizes: Vec", sizes.Sizeof(vec), "align", sizes.Alignof(vec), "*Vec", sizes.Sizeof(types.NewPointer(vec)), "string", sizes.Sizeof(types.Typ[types.String]))

	// Broken package: every error, in order.
	bf, _ := parser.ParseFile(fset, "broken.go", brokenSrc, 0)
	var terrs []string
	conf2 := types.Config{Error: func(err error) { terrs = append(terrs, err.Error()) }}
	conf2.Check("broken", fset, []*ast.File{bf}, nil)
	for _, e := range terrs {
		fmt.Println("type error:", e)
	}
	_, perr := parser.ParseFile(fset, "bad.go", "package p\nfunc f( {\n x := \n}\n", parser.AllErrors)
	if el, ok := perr.(scanner.ErrorList); ok {
		for _, e := range el {
			fmt.Println("parse error:", e)
		}
	}
}
