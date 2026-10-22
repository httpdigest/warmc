// text/template: one template parsed once and executed 4,500 times over an
// order struct, with an if, a range with an else, a nested range, pipelines
// into printf, len, and a method call.
//
// The heaviest use of reflect in this corpus.  Execution walks the parse tree,
// and every {{.Field}} looks the name up at run time, first as a method and
// then as a field.  Every function the template calls -- printf, len and the
// Total method -- goes through reflect.Value.Call, which lays out an argument
// frame and hands it to runtime.reflectcall.  On wasm reflectcall picks one of
// 27 frame sizes with a chain of compares, and the runtime.callN it picks
// memory.copy's the frame onto the stack and makes a call_indirect.  So the
// cost is interpretation and reflection rather than any loop; json is the
// other module of that kind, and this one is larger.
package main

import (
	"fmt"
	"strings"
	"text/template"
)

type line struct {
	SKU   string
	Qty   int
	Price float64
	Tags  []string
}

type order struct {
	ID       int
	Customer string
	VIP      bool
	Lines    []line
}

func (o order) Total() float64 {
	t := 0.0
	for _, l := range o.Lines {
		t += float64(l.Qty) * l.Price
	}
	return t
}

const src = `Order #{{.ID}} for {{.Customer}}{{if .VIP}} (VIP){{end}}
{{range $i, $l := .Lines}}{{$i}}. {{$l.SKU | printf "%-8s"}} x{{$l.Qty}} @ {{printf "%.2f" $l.Price}}{{range .Tags}} [{{.}}]{{end}}
{{else}}(empty)
{{end}}total {{printf "%.2f" .Total}} lines {{len .Lines}}
`

func main() {
	tmpl := template.Must(template.New("order").Parse(src))
	names := []string{"ada", "brian", "chen", "dora"}
	tags := []string{"fragile", "gift", "bulk", "eco"}
	var sb strings.Builder
	var acc uint64
	for r := 0; r < 4500; r++ {
		o := order{ID: r, Customer: names[r&3], VIP: r%3 == 0}
		for i := 0; i < r%6; i++ {
			o.Lines = append(o.Lines, line{SKU: fmt.Sprint("sku", r*7+i),
				Qty: i + 1, Price: float64(r%50+i) * 1.25, Tags: tags[:i%4]})
		}
		sb.Reset()
		if err := tmpl.Execute(&sb, o); err != nil {
			panic(err)
		}
		s := sb.String()
		acc = acc*31 + uint64(len(s))
		for i := 0; i < len(s); i += 7 {
			acc += uint64(s[i])
		}
	}
	fmt.Println("acc =", acc)
}
