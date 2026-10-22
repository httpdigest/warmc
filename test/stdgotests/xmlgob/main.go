// The other two reflection-driven codecs, encoding/xml and encoding/gob.
//
// xml: Marshal and MarshalIndent with attributes, chardata, innerxml,
// comments, omitempty, nested element paths (a>b>c), namespaces, custom
// MarshalXML; Unmarshal back including any-attr and unknown elements; the
// token-level Decoder walking StartElement, EndElement, CharData, Comment and
// ProcInst tokens with entity decoding; EscapeText; and syntax errors with
// line numbers.
//
// gob: an Encoder/Decoder pair over one buffer carrying several values of
// several types (structs with nested pointers, slices, maps, interfaces sent
// after Register, a type implementing GobEncoder), decoding into a struct with
// a different field set, and the errors for type mismatches.  gob's encoded
// bytes for maps follow map iteration order, so only decoded values and the
// lengths of map-free encodings are printed.
package main

import (
	"bytes"
	"encoding/gob"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"sort"
	"strings"
)

type Price struct {
	Currency string  `xml:"currency,attr"`
	Amount   float64 `xml:",chardata"`
}

type Book struct {
	XMLName  xml.Name `xml:"urn:books book"`
	ID       string   `xml:"id,attr"`
	Lang     string   `xml:"lang,attr,omitempty"`
	Title    string   `xml:"title"`
	Authors  []string `xml:"authors>author"`
	Price    Price    `xml:"price"`
	Note     string   `xml:",comment"`
	Summary  string   `xml:"meta>summary,omitempty"`
	InStock  bool     `xml:"in-stock"`
	Pages    *int     `xml:"pages,omitempty"`
	Internal string   `xml:"-"`
}

type Library struct {
	XMLName xml.Name   `xml:"library"`
	Name    string     `xml:"name,attr"`
	Books   []Book     `xml:"book"`
	Extra   []xml.Attr `xml:",any,attr"`
}

type Point struct{ X, Y int }

func (p Point) MarshalXML(e *xml.Encoder, start xml.StartElement) error {
	start.Attr = append(start.Attr, xml.Attr{Name: xml.Name{Local: "xy"}, Value: fmt.Sprintf("%d,%d", p.X, p.Y)})
	return e.EncodeElement("", start)
}

func (p *Point) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	for _, a := range start.Attr {
		if a.Name.Local == "xy" {
			fmt.Sscanf(a.Value, "%d,%d", &p.X, &p.Y)
		}
	}
	return d.Skip()
}

type Shape struct {
	Name   string  `xml:"name,attr"`
	Points []Point `xml:"pt"`
	Inner  string  `xml:",innerxml"`
}

// gob types.
type Inner struct {
	Label string
	Vals  []float64
}

type Outer struct {
	ID     int
	Name   string
	Ptr    *Inner
	Inners []Inner
	Lookup map[string]int
	Matrix [][]int32
	Any    any
	Bytes  []byte
	Flag   bool
	U      uint64
	C      complex128
}

type Animal interface{ Sound() string }
type Dog struct{ Name string }
type Cat struct {
	Name  string
	Lives int
}

func (d Dog) Sound() string { return d.Name + " says woof" }
func (c Cat) Sound() string { return fmt.Sprintf("%s says meow (%d lives)", c.Name, c.Lives) }

type Version struct{ Major, Minor, Patch int }

func (v Version) GobEncode() ([]byte, error) {
	return []byte(fmt.Sprintf("v%d.%d.%d", v.Major, v.Minor, v.Patch)), nil
}

func (v *Version) GobDecode(b []byte) error {
	_, err := fmt.Sscanf(string(b), "v%d.%d.%d", &v.Major, &v.Minor, &v.Patch)
	return err
}

type Narrow struct {
	Name string
	ID   int64
	New  string
}

func sortedKeys(m map[string]int) []string {
	var ks []string
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

func main() {
	pages := 312
	lib := Library{Name: "City & Co", Books: []Book{
		{ID: "b1", Lang: "en", Title: "The Go <Programming> Language", Authors: []string{"Donovan", "Kernighan"},
			Price: Price{"USD", 34.99}, Note: " classic ", Summary: "Learn Go", InStock: true, Pages: &pages, Internal: "x"},
		{ID: "b2", Title: "Wasm \"Inside\" & Out", Price: Price{"EUR", 0.5}},
	}, Extra: []xml.Attr{{Name: xml.Name{Local: "since"}, Value: "1901"}}}
	out, err := xml.MarshalIndent(lib, "", "  ")
	fmt.Println("marshal err:", err)
	fmt.Println(xml.Header + string(out))
	compact, _ := xml.Marshal(lib.Books[1])
	fmt.Println(string(compact))

	var back Library
	err = xml.Unmarshal(out, &back)
	fmt.Println("unmarshal err:", err, "books:", len(back.Books), "name:", back.Name)
	for _, b := range back.Books {
		pg := -1
		if b.Pages != nil {
			pg = *b.Pages
		}
		fmt.Printf("  book ns=%q id=%s lang=%q title=%q authors=%q price=%v %s note=%q summary=%q stock=%v pages=%d\n",
			b.XMLName.Space, b.ID, b.Lang, b.Title, b.Authors, b.Price.Amount, b.Price.Currency, b.Note, b.Summary, b.InStock, pg)
	}
	for _, a := range back.Extra {
		fmt.Printf("  extra attr %s=%s\n", a.Name.Local, a.Value)
	}

	// Custom marshalers and innerxml.
	sh := Shape{Name: "tri", Points: []Point{{0, 0}, {4, 0}, {0, 3}}}
	so, _ := xml.Marshal(sh)
	fmt.Println(string(so))
	var sb Shape
	err = xml.Unmarshal([]byte(`<Shape name="sq"><pt xy="1,2"><ignored/></pt><pt xy="3,4"/><other a="b">text</other></Shape>`), &sb)
	fmt.Printf("shape back err=%v name=%s points=%v inner=%q\n", err, sb.Name, sb.Points, sb.Inner)

	// Token stream.
	doc := `<?xml version="1.0"?>
<!-- top comment -->
<root xmlns:h="http://example.com/h" a="1 &amp; 2">
  <h:item id="x">text &lt;escaped&gt; &#65;&#x42;</h:item>
  <empty/>
  <![CDATA[raw <cdata>]]>
</root>`
	d := xml.NewDecoder(strings.NewReader(doc))
	for {
		tok, err := d.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			fmt.Println("token err", err)
			break
		}
		switch t := tok.(type) {
		case xml.StartElement:
			var as []string
			for _, a := range t.Attr {
				as = append(as, fmt.Sprintf("%s:%s=%q", a.Name.Space, a.Name.Local, a.Value))
			}
			fmt.Printf("start {%s}%s %v\n", t.Name.Space, t.Name.Local, as)
		case xml.EndElement:
			fmt.Printf("end {%s}%s\n", t.Name.Space, t.Name.Local)
		case xml.CharData:
			if s := strings.TrimSpace(string(t)); s != "" {
				fmt.Printf("chardata %q\n", s)
			}
		case xml.Comment:
			fmt.Printf("comment %q\n", string(t))
		case xml.ProcInst:
			fmt.Printf("procinst %s %q\n", t.Target, string(t.Inst))
		case xml.Directive:
			fmt.Printf("directive %q\n", string(t))
		}
	}
	var esc bytes.Buffer
	xml.EscapeText(&esc, []byte("a<b>&\"c'\n\t\x01"))
	fmt.Printf("EscapeText %q\n", esc.String())
	for _, bad := range []string{"<a><b></a>", "<a>\n\n<b x=1/></a>", "<a>&bogus;</a>", "<a>", ""} {
		var v struct{}
		err := xml.Unmarshal([]byte(bad), &v)
		var se *xml.SyntaxError
		if errors.As(err, &se) {
			fmt.Printf("xml bad %q: line %d: %s\n", bad, se.Line, se.Msg)
		} else {
			fmt.Printf("xml bad %q: %v\n", bad, err)
		}
	}

	// gob.
	gob.Register(Dog{})
	gob.Register(Cat{})
	var buf bytes.Buffer
	enc := gob.NewEncoder(&buf)
	o := Outer{ID: 7, Name: "outer", Ptr: &Inner{"p", []float64{1.5, -2}},
		Inners: []Inner{{"a", nil}, {"b", []float64{3}}}, Lookup: map[string]int{"x": 1, "y": -2, "z": 300},
		Matrix: [][]int32{{1, 2}, {}, {-3}}, Any: Cat{"Tom", 9}, Bytes: []byte{0, 255, 128},
		Flag: true, U: 1<<64 - 1, C: complex(1.5, -2.5)}
	animals := []Animal{Dog{"Rex"}, Cat{"Kit", 3}, Dog{"Fido"}}
	steps := []any{o, animals, Version{1, 22, 333}, []string{"just", "strings"}, 42, "str"}
	for _, s := range steps {
		var err error
		if a, ok := s.([]Animal); ok {
			err = enc.Encode(&a)
		} else {
			err = enc.Encode(s)
		}
		fmt.Printf("gob encode %T err=%v\n", s, err)
	}
	fmt.Println("gob stream nonempty:", buf.Len() > 0)
	dec := gob.NewDecoder(&buf)
	var o2 Outer
	fmt.Println("decode outer:", dec.Decode(&o2))
	fmt.Printf("  id=%d name=%s ptr=%+v inners=%+v matrix=%v any=%#v bytes=%v flag=%v u=%d c=%v\n",
		o2.ID, o2.Name, *o2.Ptr, o2.Inners, o2.Matrix, o2.Any, o2.Bytes, o2.Flag, o2.U, o2.C)
	for _, k := range sortedKeys(o2.Lookup) {
		fmt.Printf("  lookup %s=%d\n", k, o2.Lookup[k])
	}
	var a2 []Animal
	fmt.Println("decode animals:", dec.Decode(&a2))
	for _, a := range a2 {
		fmt.Printf("  %T: %s\n", a, a.Sound())
	}
	var v2 Version
	fmt.Println("decode version:", dec.Decode(&v2), v2)
	var ss []string
	fmt.Println("decode strings:", dec.Decode(&ss), ss)
	var wrong string
	fmt.Println("decode int into string:", dec.Decode(&wrong))
	var str string
	fmt.Println("decode next:", dec.Decode(&str), str)
	fmt.Println("decode at end:", dec.Decode(&str))

	// Decoding into a different struct: matching fields by name.
	var nb bytes.Buffer
	gob.NewEncoder(&nb).Encode(Outer{ID: 99, Name: "narrowed", Flag: true})
	var nw Narrow
	err = gob.NewDecoder(&nb).Decode(&nw)
	fmt.Printf("narrow decode err=%v %+v\n", err, nw)

	// No common fields.
	var cb bytes.Buffer
	gob.NewEncoder(&cb).Encode(Inner{"x", nil})
	var pt Point
	fmt.Println("no common fields:", gob.NewDecoder(&cb).Decode(&pt))

	// Map-free encodings have stable lengths; round trip many values.
	var lb bytes.Buffer
	le := gob.NewEncoder(&lb)
	for i := 0; i < 50; i++ {
		le.Encode(Inner{strings.Repeat("z", i%5), []float64{float64(i), float64(i) / 8}})
	}
	fmt.Println("50 inners encoded bytes:", lb.Len())
	ld := gob.NewDecoder(&lb)
	total := 0.0
	labels := 0
	for {
		var in Inner
		if err := ld.Decode(&in); err != nil {
			fmt.Println("inner stream end:", err)
			break
		}
		total += in.Vals[0] + in.Vals[1]
		labels += len(in.Label)
	}
	fmt.Println("inner totals", total, labels)
}
