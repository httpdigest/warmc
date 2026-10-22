// encoding/json in depth: marshalling structs with tags (rename, omitempty,
// "-", ",string"), embedded structs, pointers and nil pointers, maps (whose
// keys json sorts, so the output is fixed), slices, arrays, []byte as base64,
// interfaces, custom MarshalJSON / UnmarshalJSON and MarshalText /
// UnmarshalText, RawMessage, Number, HTML escaping, MarshalIndent, Indent,
// Compact and Valid; unmarshalling into structs, into any (map[string]any with
// float64 numbers), into existing values, with unknown fields rejected by a
// Decoder; a streaming Decoder over several values and its Token API; and the
// exact text of syntax, type and unsupported-value errors.
//
// The package is reflection-driven: an encoder function is cached per type and
// called indirectly, and the decoder is a byte-at-a-time state machine of
// function values.  Every result is printed.
package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strings"
	"time"
)

type Address struct {
	Street string `json:"street"`
	City   string `json:"city,omitempty"`
	Zip    *int   `json:"zip,omitempty"`
}

type Audit struct {
	CreatedBy string `json:"created_by"`
	Version   int    `json:"version"`
}

type Person struct {
	Audit
	Name     string           `json:"name"`
	Age      int              `json:"age,omitempty"`
	Email    string           `json:"-"`
	Score    float64          `json:"score"`
	ID       int64            `json:"id,string"`
	Active   bool             `json:"active"`
	Address  *Address         `json:"address,omitempty"`
	Tags     []string         `json:"tags"`
	Attrs    map[string]any   `json:"attrs,omitempty"`
	Raw      json.RawMessage  `json:"raw,omitempty"`
	Data     []byte           `json:"data,omitempty"`
	Grid     [2][2]int        `json:"grid"`
	Counts   map[int]int      `json:"counts,omitempty"`
	Nested   map[string][]int `json:"nested,omitempty"`
	Iface    any              `json:"iface"`
	internal string
}

type Color int

const (
	Red Color = iota
	Green
	Blue
)

var colorNames = []string{"red", "green", "blue"}

func (c Color) MarshalText() ([]byte, error) {
	if int(c) < len(colorNames) {
		return []byte(colorNames[c]), nil
	}
	return nil, fmt.Errorf("bad color %d", int(c))
}

func (c *Color) UnmarshalText(b []byte) error {
	for i, n := range colorNames {
		if n == string(b) {
			*c = Color(i)
			return nil
		}
	}
	return fmt.Errorf("unknown color %q", b)
}

type Temp float64

func (t Temp) MarshalJSON() ([]byte, error) {
	return []byte(fmt.Sprintf(`{"celsius":%.1f,"fahrenheit":%.1f}`, float64(t), float64(t)*9/5+32)), nil
}

func (t *Temp) UnmarshalJSON(b []byte) error {
	var v struct{ Celsius float64 }
	if err := json.Unmarshal(b, &v); err != nil {
		return err
	}
	*t = Temp(v.Celsius)
	return nil
}

type Reading struct {
	Sensor string           `json:"sensor"`
	Color  Color            `json:"color"`
	Temps  []Temp           `json:"temps"`
	ByCol  map[Color]int    `json:"by_color"`
	Dur    time.Duration    `json:"dur"`
	When   time.Time        `json:"when"`
	Opt    *Temp            `json:"opt"`
	Extra  map[string]Color `json:"extra"`
}

func show(label string, v any) {
	b, err := json.Marshal(v)
	if err != nil {
		fmt.Printf("%s: error: %v\n", label, err)
		return
	}
	fmt.Printf("%s: %s\n", label, b)
}

func main() {
	zip := 12345
	p := Person{
		Audit: Audit{"admin", 3}, Name: "Ada <Lovelace> & co", Age: 36, Email: "hidden",
		Score: 99.5, ID: 1 << 60, Active: true,
		Address: &Address{"1 Loop", "", &zip},
		Tags:    []string{"math", "engine"},
		Attrs:   map[string]any{"z": 1, "a": []any{true, nil, "x", 2.5}, "m": map[string]int{"b": 2, "a": 1}},
		Raw:     json.RawMessage(`{"pre":"computed"}`),
		Data:    []byte("binary\x00\xff"),
		Grid:    [2][2]int{{1, 2}, {3, 4}},
		Counts:  map[int]int{10: 1, 2: 2, -5: 3},
		Nested:  map[string][]int{"odd": {1, 3}, "even": {2, 4}, "none": nil},
		Iface:   Address{Street: "iface st"}, internal: "x",
	}
	show("person", p)
	show("empty person", Person{})
	out, _ := json.MarshalIndent(p.Address, ">", "  ")
	fmt.Printf("indent:\n%s\n", out)

	// Scalars and special values.
	for _, v := range []any{nil, true, 0, -1, 3.14, 1e21, 1e-7, float32(0.1), math.MaxInt64, uint64(math.MaxUint64),
		"quote\"back\\slash\nnl\ttab\u0001", "<script>&</script>", "\u2028\u2029", "héllo wörld ✓",
		[]int(nil), []int{}, map[string]int(nil), [0]int{}, struct{}{}, &zip, []byte(nil), []byte{}} {
		show(fmt.Sprintf("%T", v), v)
	}
	show("NaN", math.NaN())
	show("Inf", math.Inf(1))
	show("chan", make(chan int))
	show("func", func() {})
	show("bad color", Color(9))
	show("cyclic map", func() any { m := map[string]any{}; m["self"] = m; return m }())

	// Without HTML escaping via Encoder.
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	enc.SetIndent("", " ")
	enc.Encode(map[string]string{"html": "<a href=\"x\">&</a>"})
	fmt.Print("encoder no-escape: ", buf.String())

	// Custom marshallers.
	t := Temp(21.5)
	r := Reading{
		Sensor: "s1", Color: Blue, Temps: []Temp{-40, 0, 100},
		ByCol: map[Color]int{Blue: 3, Red: 1, Green: 2}, Dur: 1500 * time.Millisecond,
		When: time.Date(2024, 2, 29, 13, 4, 5, 123000000, time.UTC), Opt: &t,
		Extra: map[string]Color{"x": Green},
	}
	rb, err := json.Marshal(r)
	fmt.Printf("reading: %s %v\n", rb, err)
	var r2 Reading
	err = json.Unmarshal(rb, &r2)
	fmt.Printf("reading back: %v %+v opt=%v when=%s\n", err, r2.Temps, *r2.Opt, r2.When.Format(time.RFC3339Nano))
	fmt.Println("reading round-trip byCol:", r2.ByCol[Red], r2.ByCol[Green], r2.ByCol[Blue], r2.Color, r2.Dur)

	// Unmarshal into a struct, including the ,string option and embedded fields.
	in := `{"created_by":"root","version":9,"name":"Grace","age":85,"score":1e2,"id":"9007199254740993",
	        "active":true,"address":{"street":"Navy Way","zip":20500},"tags":["cobol","compilers"],
	        "data":"AQID","grid":[[5,6],[7,8]],"counts":{"1":10,"-2":20},"iface":{"k":[1,"two",null]},
	        "Email":"ignored-by-dash","unknown":123,"NAME":"case-insensitive-later-wins"}`
	var q Person
	err = json.Unmarshal([]byte(in), &q)
	fmt.Printf("unmarshal err=%v\n", err)
	fmt.Printf("  audit=%+v name=%q age=%d score=%v id=%d active=%v email=%q\n", q.Audit, q.Name, q.Age, q.Score, q.ID, q.Active, q.Email)
	fmt.Printf("  street=%q city=%q zip=%d tags=%v data=%v grid=%v counts=%v\n", q.Address.Street, q.Address.City, *q.Address.Zip, q.Tags, q.Data, q.Grid, q.Counts)
	fmt.Printf("  iface=%#v\n", q.Iface)

	// Into any: numbers become float64; with UseNumber they stay json.Number.
	doc := `{"int":12345678901234567890,"neg":-0.5,"exp":6.02e23,"arr":[1,[2,[3]]],"obj":{"b":null,"a":false}}`
	var anyv any
	json.Unmarshal([]byte(doc), &anyv)
	show("any round trip", anyv)
	m := anyv.(map[string]any)
	fmt.Printf("  int as float64: %v (%T)\n", m["int"], m["int"])
	dec := json.NewDecoder(strings.NewReader(doc))
	dec.UseNumber()
	var numv map[string]any
	dec.Decode(&numv)
	n := numv["int"].(json.Number)
	_, ierr := n.Int64()
	fmt.Printf("  UseNumber: %s int64 err=%v exp=%v\n", n, ierr, numv["exp"])

	// Unmarshal into existing values: maps merge, slices are replaced, struct fields kept.
	existing := map[string]int{"keep": 1, "over": 2}
	json.Unmarshal([]byte(`{"over":20,"new":30}`), &existing)
	show("merged map", existing)
	sl := []int{9, 9, 9, 9}
	json.Unmarshal([]byte(`[1,2]`), &sl)
	fmt.Println("replaced slice", sl, len(sl))
	arr := [4]int{9, 9, 9, 9}
	json.Unmarshal([]byte(`[1,2]`), &arr)
	fmt.Println("array zero-filled", arr)
	keepAddr := Address{Street: "old", City: "Keep"}
	json.Unmarshal([]byte(`{"street":"new"}`), &keepAddr)
	fmt.Printf("struct partially updated %+v\n", keepAddr)

	// Errors.
	bad := []string{
		`{"name": }`, `[1,2,`, `{"age":"thirty"}`, `{"age":1.5}`, `{"tags":"notarray"}`,
		`{"id":123}`, `"\x01"`, `{"a":1}x`, `nul`, `{"grid":[[1,2,3]]}`, `{"age":99999999999999999999}`,
	}
	for _, s := range bad {
		var pp Person
		err := json.Unmarshal([]byte(s), &pp)
		var se *json.SyntaxError
		var te *json.UnmarshalTypeError
		kind := "other"
		if errors.As(err, &se) {
			kind = fmt.Sprintf("syntax@%d", se.Offset)
		} else if errors.As(err, &te) {
			kind = fmt.Sprintf("type(%s->%s field=%s)", te.Value, te.Type, te.Field)
		}
		fmt.Printf("error %-28q %s: %v\n", s, kind, err)
	}
	var notPtr Person
	fmt.Println("non-pointer:", json.Unmarshal([]byte(`{}`), notPtr))
	var c Color
	fmt.Println("bad text:", json.Unmarshal([]byte(`"purple"`), &c))

	// Strict decoding.
	sd := json.NewDecoder(strings.NewReader(`{"street":"s","bogus":1}`))
	sd.DisallowUnknownFields()
	var sa Address
	fmt.Println("strict:", sd.Decode(&sa))

	// Streaming several values.
	stream := `{"street":"a"} {"street":"b","city":"B"}
	[1,2,3] "str" 42 true null`
	sdec := json.NewDecoder(strings.NewReader(stream))
	for i := 0; ; i++ {
		var v any
		if err := sdec.Decode(&v); err != nil {
			fmt.Println("stream end:", err)
			break
		}
		fmt.Printf("stream %d: %T %v offset=%d\n", i, v, v, sdec.InputOffset())
	}

	// Token API.
	tdec := json.NewDecoder(strings.NewReader(`{"list":[1,"a",{"x":null}],"t":true,"n":-1.5e3}`))
	for {
		tok, err := tdec.Token()
		if err != nil {
			fmt.Println("tokens end:", err)
			break
		}
		fmt.Printf("token %T %v more=%v\n", tok, tok, tdec.More())
	}

	// Indent / Compact / Valid / HTMLEscape.
	var ib bytes.Buffer
	json.Indent(&ib, []byte(`{"a":[1,2,{"b":{}}],"c":[]}`), "", "\t")
	fmt.Println("Indent:\n" + ib.String())
	var cb bytes.Buffer
	json.Compact(&cb, []byte("{ \"a\" : [ 1 , 2 ] ,\n \"b\" : { } }"))
	fmt.Println("Compact:", cb.String())
	var hb bytes.Buffer
	json.HTMLEscape(&hb, []byte(`{"x":"<b>&amp;</b>"}`))
	fmt.Println("HTMLEscape:", hb.String())
	for _, s := range []string{`{}`, `{"a":}`, `[1 2]`, `"\u00e9"`, `"\ud83d\ude00"`, `"\ud800"`, `1e`, ` null `, `{"a":1,}`} {
		fmt.Printf("Valid(%s) = %v\n", s, json.Valid([]byte(s)))
	}

	// Round-trip a slice of structs many times and compare.
	type rec struct {
		I int     `json:"i"`
		F float64 `json:"f"`
		S string  `json:"s"`
		B []bool  `json:"b,omitempty"`
	}
	recs := make([]rec, 20)
	for i := range recs {
		recs[i] = rec{i * i, float64(i) / 3, strings.Repeat("ab", i%4), make([]bool, i%3)}
	}
	cur := recs
	for round := 0; round < 5; round++ {
		b, _ := json.Marshal(cur)
		var next []rec
		json.Unmarshal(b, &next)
		cur = next
		if round == 4 {
			fmt.Println("rounds stable, bytes:", len(b))
			fmt.Println(string(b[:200]))
		}
	}
	fmt.Printf("last rec %+v\n", cur[19])
}
