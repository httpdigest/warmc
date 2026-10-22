// text/template and text/tabwriter rendering.
//
// text/template: field and method access, pipelines into printf/len/index/
// slice/html/js/urlquery, if/else if/else, range over slices, maps (sorted by
// key), channels-free ints (range 5) with $i/$v and break/continue, with/else,
// variables and their scoping, define/template/block, custom FuncMap
// functions with error returns, comparison functions (eq with several
// arguments, lt, le, and/or short-circuit, not), whitespace trimming {{- -}},
// nested templates passing dot, missingkey options, Clone and redefinition,
// and the execution errors' exact text.
//
// text/tabwriter: alignment with padding characters, AlignRight, Debug
// column separators, tab terminated vs not, DiscardEmptyColumns, and flushing
// partial tables.
//
// Template execution is a reflect-driven tree walk; every function call goes
// through reflect.Value.Call.
package main

import (
	"errors"
	"fmt"
	"os"
	"strings"
	"text/tabwriter"
	"text/template"
)

type Item struct {
	Name  string
	Price float64
	Qty   int
	Tags  []string
}

func (i Item) Total() float64 { return i.Price * float64(i.Qty) }

func (i Item) HasTag(t string) bool {
	for _, x := range i.Tags {
		if x == t {
			return true
		}
	}
	return false
}

type Order struct {
	ID       int
	Customer *Customer
	Items    []Item
	Meta     map[string]string
	Notes    []string
	Discount float64
}

type Customer struct {
	Name  string
	Email string
	VIP   bool
}

func (o *Order) Sum() float64 {
	s := 0.0
	for _, it := range o.Items {
		s += it.Total()
	}
	return s * (1 - o.Discount)
}

func run(name, text string, data any, funcs template.FuncMap) {
	t := template.New(name)
	if funcs != nil {
		t = t.Funcs(funcs)
	}
	t, err := t.Parse(text)
	if err != nil {
		fmt.Printf("--- %s: parse error: %v\n", name, err)
		return
	}
	var sb strings.Builder
	err = t.Execute(&sb, data)
	fmt.Printf("--- %s:\n%s", name, sb.String())
	if err != nil {
		fmt.Printf("\n[exec error: %v]", err)
	}
	fmt.Println()
}

func main() {
	order := &Order{
		ID:       1042,
		Customer: &Customer{"Ada <Lovelace>", "ada@example.com", true},
		Items: []Item{
			{"Widget", 2.50, 4, []string{"metal", "small"}},
			{"Gadget & Co", 10, 1, nil},
			{"Doohickey", 0.333, 3, []string{"plastic"}},
		},
		Meta:     map[string]string{"zone": "eu", "channel": "web", "agent": "x/1.0"},
		Notes:    []string{"fragile", "", "gift"},
		Discount: 0.1,
	}
	funcs := template.FuncMap{
		"upper": strings.ToUpper,
		"join":  strings.Join,
		"add":   func(a, b int) int { return a + b },
		"mul":   func(a float64, b int) float64 { return a * float64(b) },
		"safeDiv": func(a, b int) (int, error) {
			if b == 0 {
				return 0, errors.New("division by zero")
			}
			return a / b, nil
		},
		"repeat": strings.Repeat,
		"seq": func(n int) []int {
			s := make([]int, n)
			for i := range s {
				s[i] = i
			}
			return s
		},
	}

	run("fields", `Order #{{.ID}} for {{.Customer.Name}} <{{.Customer.Email}}>{{if .Customer.VIP}} (VIP){{end}}
Items: {{len .Items}}, first: {{(index .Items 0).Name}}, total {{printf "%.2f" .Sum}}
`, order, nil)

	run("range", `{{range $i, $it := .Items}}{{$i}}: {{printf "%-12s" $it.Name}} {{$it.Qty}} x {{printf "%6.3f" $it.Price}} = {{printf "%7.3f" $it.Total}}{{if $it.Tags}} tags={{join $it.Tags ","}}{{else}} (no tags){{end}}
{{end}}`, order, funcs)

	run("map-sorted", `{{range $k, $v := .Meta}}{{$k}}={{$v}};{{end}}
{{range .Meta}}[{{.}}]{{end}}`, order, nil)

	run("else-if", `{{range .}}{{if lt . 0}}neg{{else if eq . 0}}zero{{else if le . 10}}small{{else}}big{{end}} {{end}}`, []int{-5, 0, 3, 10, 11, 1000}, nil)

	run("range-else-break", `{{range .Notes}}{{if not .}}{{continue}}{{end}}note:{{.}} {{else}}no notes{{end}}
{{range $i, $n := .Notes}}{{if eq $i 1}}{{break}}{{end}}{{$i}}={{$n}} {{end}}
{{range .Missing}}x{{else}}empty range else{{end}}`, map[string]any{"Notes": order.Notes, "Missing": []int{}}, nil)

	run("range-int", `{{range $i := 5}}{{$i}}{{end}} {{range 3}}*{{end}}`, nil, nil)

	run("with", `{{with .Customer}}{{.Name}} is {{if .VIP}}vip{{else}}regular{{end}}{{end}}
{{with .Nothing}}has{{else}}nothing here{{end}}
{{with $x := 42}}x={{$x}}{{end}}`, map[string]any{"Customer": order.Customer, "Nothing": ""}, nil)

	run("variables", `{{$total := 0}}{{range .}}{{$total = add $total .}}{{end}}total={{$total}}
{{$x := "outer"}}{{range $i, $_ := .}}{{if eq $i 0}}{{$x := "inner"}}{{$x}} {{end}}{{end}}{{$x}}`, []int{1, 2, 3, 4, 5}, funcs)

	run("comparisons", `{{eq 1 1}} {{eq 1 2 3 1}} {{ne "a" "b"}} {{lt 1 2}} {{le 2 2}} {{gt 3.5 1.5}} {{ge "b" "a"}}
{{and 1 0 "x"}} {{and 1 "x"}} {{or 0 "" "first"}} {{or 0 ""}} {{not 0}} {{not "x"}}
{{and false (index . 5)}} {{or true (index . 5)}}`, []int{1}, nil)

	run("builtins", `{{len "héllo"}} {{index .M "b"}} {{index .S 1}} {{index .N 1 0}} {{slice .Str 1 3}} {{slice .S 1}}
{{html "<a href='x'>&</a>"}} {{js "it's \"quoted\" <tag>"}} {{urlquery "a b&c=d/é"}}
{{print 1 2 "x" "y" 3}} {{println "line"}}{{printf "%05.1f|%x|%q" 3.14159 255 "s"}}
{{call .F 3 4}} {{.F2 | printf "%s!"}}`, map[string]any{
		"M": map[string]int{"a": 1, "b": 2}, "S": []string{"x", "y", "z"}, "N": [][]int{{1}, {2, 3}},
		"Str": "abcdef", "F": func(a, b int) int { return a * b }, "F2": "piped",
	}, nil)

	run("pipelines", `{{.Name | upper | printf "%q"}} {{"abc" | repeat 3 | len}} {{3 | add 4 | mul 1.5}}
{{range seq 4}}{{. | add 10}},{{end}}`, Item{Name: "pipe"}, template.FuncMap{
		"upper": strings.ToUpper, "repeat": func(n int, s string) string { return strings.Repeat(s, n) },
		"add": func(a, b int) int { return a + b }, "mul": func(f float64, n int) float64 { return f * float64(n) },
		"seq": funcs["seq"],
	})

	run("trim", `start
{{- range . }}
    item {{ . -}}
{{ end }}
end {{- " trimmed" }}   {{ "x" }}`, []string{"a", "b"}, nil)

	run("methods", `{{range .Items}}{{.Name}}: hasTag(metal)={{.HasTag "metal"}} total={{.Total | printf "%.3f"}}
{{end}}sum={{printf "%.4f" .Sum}}`, order, nil)

	// define / template / block.
	base := template.Must(template.New("page").Funcs(funcs).Parse(`{{define "header"}}== {{.}} =={{end}}` +
		`{{define "row"}}| {{printf "%-10s" .Name}} | {{printf "%3d" .Qty}} |{{end}}` +
		`{{template "header" "Report"}}
{{range .Items}}{{template "row" .}}
{{end}}{{block "footer" .}}default footer: {{len .Items}} items{{end}}
`))
	var sb strings.Builder
	base.Execute(&sb, order)
	fmt.Print("--- define/block:\n", sb.String())
	over := template.Must(template.Must(base.Clone()).Parse(`{{define "footer"}}custom footer, sum {{printf "%.2f" .Sum}}{{end}}`))
	sb.Reset()
	over.Execute(&sb, order)
	fmt.Print("--- overridden block:\n", sb.String())
	sb.Reset()
	base.ExecuteTemplate(&sb, "row", Item{Name: "direct", Qty: 7})
	fmt.Println("--- ExecuteTemplate:", sb.String())
	var names []string
	for _, t := range base.Templates() {
		names = append(names, t.Name())
	}
	fmt.Println("defined templates:", len(names), base.DefinedTemplates() != "")
	fmt.Println("lookup header:", base.Lookup("header") != nil, "lookup nope:", base.Lookup("nope") == nil)

	// Recursive template.
	type node struct {
		Name     string
		Children []node
	}
	tree := node{"root", []node{{"a", []node{{"a1", nil}, {"a2", nil}}}, {"b", nil}, {"c", []node{{"c1", []node{{"c1x", nil}}}}}}}
	rt := template.Must(template.New("tree").Parse(`{{define "node"}}{{.Name}}{{if .Children}}({{range $i, $c := .Children}}{{if $i}} {{end}}{{template "node" $c}}{{end}}){{end}}{{end}}{{template "node" .}}`))
	sb.Reset()
	rt.Execute(&sb, tree)
	fmt.Println("--- recursive:", sb.String())

	// missingkey options.
	for _, opt := range []string{"missingkey=default", "missingkey=zero", "missingkey=error"} {
		t := template.Must(template.New("mk").Option(opt).Parse(`[{{.present}}|{{.absent}}]`))
		sb.Reset()
		err := t.Execute(&sb, map[string]int{"present": 1})
		fmt.Printf("--- %s: %q err=%v\n", opt, sb.String(), err)
	}

	// Errors.
	run("exec-error-func", `{{safeDiv 10 2}} {{safeDiv 1 0}} after`, nil, funcs)
	run("exec-error-field", `{{.Customer.Nope}}`, order, nil)
	run("exec-error-index", `{{index . 5}}`, []int{1, 2}, nil)
	run("exec-error-nilptr", `{{.Customer.Name}}`, &Order{}, nil)
	run("exec-error-call", `{{template "missing"}}`, nil, nil)
	run("parse-error-1", `{{if}}x{{end}}`, nil, nil)
	run("parse-error-2", `{{range .}}unterminated`, nil, nil)
	run("parse-error-3", `{{undefinedFunc 1}}`, nil, nil)
	run("parse-error-4", `{{.X | }}`, nil, nil)
	run("parse-error-5", `{{end}}`, nil, nil)

	// tabwriter.
	rows := [][]string{
		{"NAME", "QTY", "PRICE", "NOTE"},
		{"Widget", "4", "2.50", "metal, small"},
		{"Gadget & Co", "1", "10.00", ""},
		{"Doohickey", "3", "0.33", "plastic"},
		{"X", "1000", "123456.78", "a very long note that widens the column"},
	}
	configs := []struct {
		name            string
		minw, tabw, pad int
		padchar         byte
		flags           uint
	}{
		{"default", 0, 8, 1, ' ', 0},
		{"padded", 5, 0, 2, ' ', 0},
		{"dots", 0, 4, 1, '.', 0},
		{"alignright", 0, 0, 2, ' ', tabwriter.AlignRight},
		{"debug", 0, 0, 1, ' ', tabwriter.Debug},
		{"debug+right", 6, 0, 1, '-', tabwriter.Debug | tabwriter.AlignRight},
		{"tabs", 0, 8, 1, '\t', 0},
	}
	for _, c := range configs {
		fmt.Printf("--- tabwriter %s:\n", c.name)
		w := tabwriter.NewWriter(os.Stdout, c.minw, c.tabw, c.pad, c.padchar, c.flags)
		for _, r := range rows {
			fmt.Fprintln(w, strings.Join(r, "\t")+"\t")
		}
		w.Flush()
	}
	fmt.Println("--- tabwriter non-terminated cells and blocks:")
	w := tabwriter.NewWriter(os.Stdout, 0, 0, 1, ' ', 0)
	fmt.Fprintln(w, "a\tb\tc")
	fmt.Fprintln(w, "aaaa\tbbbbbb\tc")
	fmt.Fprintln(w, "")
	fmt.Fprintln(w, "new block\tx")
	fmt.Fprintln(w, "y\tyyyyyyyy")
	w.Flush()
	fmt.Println("--- tabwriter DiscardEmptyColumns:")
	w = tabwriter.NewWriter(os.Stdout, 0, 0, 1, '.', tabwriter.DiscardEmptyColumns|tabwriter.Debug)
	fmt.Fprintln(w, "a\t\tc\t")
	fmt.Fprintln(w, "aa\t\tcc\t")
	w.Flush()
	fmt.Println("--- tabwriter escaped and filtered:")
	w = tabwriter.NewWriter(os.Stdout, 0, 0, 1, ' ', tabwriter.StripEscape|tabwriter.FilterHTML)
	fmt.Fprintln(w, "\xffesc\taped\xff\t<b>bold</b>\tx&amp;y\t")
	fmt.Fprintln(w, "plain\t<i>i</i>\tz\t")
	w.Flush()
	fmt.Println("--- tabwriter generated table:")
	w = tabwriter.NewWriter(os.Stdout, 4, 0, 1, ' ', tabwriter.AlignRight|tabwriter.Debug)
	for i := 1; i <= 12; i++ {
		fmt.Fprintf(w, "%d\t%d\t%d\t%s\t\n", i, i*i, i*i*i, strings.Repeat("#", i))
	}
	w.Flush()
}
