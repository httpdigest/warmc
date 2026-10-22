// The byte-level encoders: encoding/binary (fixed-width ByteOrder Put/Get and
// Append for both endiannesses, binary.Write/Read of structs and slices,
// Size, Uvarint/Varint and their Append forms over boundary values, and the
// errors for truncated input), encoding/hex (Encode, Decode, Dump, Dumper,
// invalid-byte and odd-length errors), encoding/base64 (Std, URL, Raw,
// strict decoding, streaming encoder), encoding/base32 (Std, Hex, NoPadding),
// encoding/ascii85, encoding/pem round trips with headers, and encoding/csv
// reading quoted fields, embedded newlines, lazy quotes, comments, custom
// separators and error positions, and writing with CRLF.
//
// These are loops of shifts, masks and table lookups over bytes -- the kind
// of code where a wrong sign- or zero-extension is invisible until a byte with
// its top bit set goes through it, so the inputs are full of them.
package main

import (
	"bytes"
	"encoding/ascii85"
	"encoding/base32"
	"encoding/base64"
	"encoding/binary"
	"encoding/csv"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"io"
	"math"
	"strings"
)

type header struct {
	Magic   [4]byte
	Version uint16
	Flags   uint16
	Count   int32
	Offset  int64
	Ratio   float32
	Scale   float64
	Signed  int8
	Pad     [3]uint8
}

func main() {
	// ByteOrder.
	b := make([]byte, 8)
	for _, bo := range []binary.ByteOrder{binary.BigEndian, binary.LittleEndian} {
		bo.PutUint16(b, 0xBEEF)
		fmt.Printf("%v PutUint16 % x -> %#x\n", bo, b[:2], bo.Uint16(b))
		bo.PutUint32(b, 0xDEADBEEF)
		fmt.Printf("%v PutUint32 % x -> %#x\n", bo, b[:4], bo.Uint32(b))
		bo.PutUint64(b, 0x0123456789ABCDEF)
		fmt.Printf("%v PutUint64 % x -> %#x\n", bo, b, bo.Uint64(b))
		fmt.Printf("%v as int16 %d int32 %d int64 %d\n", bo, int16(bo.Uint16(b)), int32(bo.Uint32(b[4:])), int64(bo.Uint64(b)))
	}
	ab := binary.BigEndian.AppendUint32(nil, 0x80000001)
	ab = binary.LittleEndian.AppendUint16(ab, 0xFF7F)
	ab = binary.BigEndian.AppendUint64(ab, math.Float64bits(-2.5))
	fmt.Printf("append % x\n", ab)

	// Write / Read structs.
	h := header{Magic: [4]byte{'W', 'A', 'S', 'M'}, Version: 2, Flags: 0x8001, Count: -7,
		Offset: -1 << 40, Ratio: 0.75, Scale: math.Pi, Signed: -128, Pad: [3]uint8{1, 255, 128}}
	for _, bo := range []binary.ByteOrder{binary.BigEndian, binary.LittleEndian} {
		var buf bytes.Buffer
		if err := binary.Write(&buf, bo, h); err != nil {
			fmt.Println("write err", err)
		}
		fmt.Printf("%v struct size %d bytes % x\n", bo, binary.Size(h), buf.Bytes())
		var back header
		err := binary.Read(bytes.NewReader(buf.Bytes()), bo, &back)
		fmt.Printf("  read back err=%v equal=%v %+v\n", err, back == h, back)
		var short header
		err = binary.Read(bytes.NewReader(buf.Bytes()[:10]), bo, &short)
		fmt.Println("  short read:", err, errors.Is(err, io.ErrUnexpectedEOF))
	}
	vals := []int16{-1, 0, 1, math.MinInt16, math.MaxInt16}
	var vb bytes.Buffer
	binary.Write(&vb, binary.LittleEndian, vals)
	fmt.Printf("int16 slice % x\n", vb.Bytes())
	back16 := make([]int16, len(vals))
	binary.Read(&vb, binary.LittleEndian, back16)
	fmt.Println("int16 slice back", back16)
	enc, err := binary.Append(nil, binary.BigEndian, struct {
		A uint8
		B [2]uint16
	}{9, [2]uint16{1, 0xFFFF}})
	fmt.Printf("binary.Append % x %v\n", enc, err)

	// Varints.
	ub := make([]byte, binary.MaxVarintLen64)
	for _, u := range []uint64{0, 1, 127, 128, 255, 300, 16383, 16384, 1<<32 - 1, 1 << 56, math.MaxUint64} {
		n := binary.PutUvarint(ub, u)
		got, m := binary.Uvarint(ub[:n])
		fmt.Printf("uvarint %20d -> %d bytes % x -> %d (%d)\n", u, n, ub[:n], got, m)
	}
	for _, s := range []int64{0, -1, 1, -64, 64, -65, math.MinInt64, math.MaxInt64} {
		n := binary.PutVarint(ub, s)
		got, m := binary.Varint(ub[:n])
		fmt.Printf("varint %20d -> %d bytes % x -> %d (%d)\n", s, n, ub[:n], got, m)
	}
	app := binary.AppendUvarint(nil, 1000)
	app = binary.AppendVarint(app, -1000)
	fmt.Printf("append varints % x\n", app)
	r := bytes.NewReader(app)
	u1, e1 := binary.ReadUvarint(r)
	s1, e2 := binary.ReadVarint(r)
	_, e3 := binary.ReadUvarint(r)
	fmt.Println("ReadUvarint/ReadVarint", u1, e1, s1, e2, e3)
	v, n := binary.Uvarint([]byte{0x80, 0x80})
	fmt.Println("truncated uvarint", v, n)
	v, n = binary.Uvarint([]byte{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f})
	fmt.Println("overflow uvarint", v, n)

	// Hex.
	data := []byte("Hello, \x00\x7f\x80\xfe\xff world! 0123456789abcdef")
	hs := hex.EncodeToString(data)
	fmt.Println("hex", hs)
	dec, err := hex.DecodeString(strings.ToUpper(hs))
	fmt.Println("hex decode upper", bytes.Equal(dec, data), err)
	for _, bad := range []string{"abc", "zz", "0g", "12 34"} {
		_, err := hex.DecodeString(bad)
		fmt.Printf("hex decode %q: %v\n", bad, err)
	}
	fmt.Print(hex.Dump(data))
	var dumpBuf bytes.Buffer
	d := hex.Dumper(&dumpBuf)
	d.Write([]byte("split "))
	d.Write([]byte("across writes, more than sixteen bytes"))
	d.Close()
	fmt.Print(dumpBuf.String())
	fmt.Println("AppendEncode", string(hex.AppendEncode([]byte("x="), []byte{1, 0xab})))

	// Base64.
	inputs := [][]byte{{}, {0}, {0, 0}, {0, 0, 0}, {0xff}, {0xfb, 0xff}, {0xfb, 0xff, 0xbf}, []byte("any carnal pleasure."), []byte("any carnal pleasure")}
	encs := []struct {
		name string
		e    *base64.Encoding
	}{{"Std", base64.StdEncoding}, {"URL", base64.URLEncoding}, {"RawStd", base64.RawStdEncoding}, {"RawURL", base64.RawURLEncoding}}
	for _, in := range inputs {
		var parts []string
		for _, e := range encs {
			s := e.e.EncodeToString(in)
			back, err := e.e.DecodeString(s)
			ok := err == nil && bytes.Equal(back, in)
			parts = append(parts, fmt.Sprintf("%s=%q(%v)", e.name, s, ok))
		}
		fmt.Println("base64", strings.Join(parts, " "))
	}
	for _, bad := range []string{"YQ", "YQ=", "Y===", "!!!!", "YWJj\nZGVm", "YWJjZA=="} {
		d1, err1 := base64.StdEncoding.DecodeString(bad)
		d2, err2 := base64.StdEncoding.Strict().DecodeString(bad)
		fmt.Printf("base64 decode %q: %q %v | strict %q %v\n", bad, d1, err1, d2, err2)
	}
	var b64 bytes.Buffer
	w := base64.NewEncoder(base64.StdEncoding, &b64)
	for i := 0; i < 10; i++ {
		w.Write([]byte{byte(i * 37), byte(i * 91)})
	}
	w.Close()
	fmt.Println("base64 stream", b64.String())
	rd, _ := io.ReadAll(base64.NewDecoder(base64.StdEncoding, strings.NewReader(b64.String())))
	fmt.Printf("base64 stream back % x\n", rd)
	fmt.Println("EncodedLen/DecodedLen", base64.StdEncoding.EncodedLen(10), base64.RawStdEncoding.EncodedLen(10), base64.StdEncoding.DecodedLen(16))
	custom := base64.NewEncoding("ZYXWVUTSRQPONMLKJIHGFEDCBAzyxwvutsrqponmlkjihgfedcba9876543210+/").WithPadding('*')
	fmt.Println("custom alphabet", custom.EncodeToString([]byte("custom!")))

	// Base32.
	for _, s := range []string{"", "f", "fo", "foo", "foob", "fooba", "foobar"} {
		std := base32.StdEncoding.EncodeToString([]byte(s))
		hx := base32.HexEncoding.EncodeToString([]byte(s))
		np := base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString([]byte(s))
		back, err := base32.StdEncoding.DecodeString(std)
		fmt.Printf("base32 %-8q std=%-18q hex=%-18q nopad=%-12q back=%q %v\n", s, std, hx, np, back, err)
	}
	_, err = base32.StdEncoding.DecodeString("MZXW6===X")
	fmt.Println("base32 bad:", err)

	// Ascii85.
	a85 := make([]byte, ascii85.MaxEncodedLen(len(data)))
	na := ascii85.Encode(a85, data)
	fmt.Printf("ascii85 %s\n", a85[:na])
	dst := make([]byte, len(data)+8)
	nd, ns, err := ascii85.Decode(dst, a85[:na], true)
	fmt.Println("ascii85 back", bytes.Equal(dst[:nd], data), ns, err)
	zeros := make([]byte, ascii85.MaxEncodedLen(8))
	nz := ascii85.Encode(zeros, make([]byte, 8))
	fmt.Printf("ascii85 zeros %s\n", zeros[:nz])

	// PEM.
	block := &pem.Block{Type: "TEST DATA", Headers: map[string]string{"Proc-Type": "4,ENCRYPTED", "B-Header": "x", "A-Header": "y"}, Bytes: bytes.Repeat([]byte{0xde, 0xad, 0xbe, 0xef}, 20)}
	pemText := pem.EncodeToMemory(block)
	fmt.Print(string(pemText))
	pb, rest := pem.Decode(append(pemText, []byte("trailing")...))
	fmt.Println("pem decoded", pb.Type, len(pb.Bytes), bytes.Equal(pb.Bytes, block.Bytes), pb.Headers["A-Header"], string(rest))
	nb, _ := pem.Decode([]byte("no pem here"))
	fmt.Println("pem none", nb == nil)

	// CSV reading.
	in := `name,age,"quote ""inside""",notes
alice,30,"a,b","multi
line"
# a comment
bob,,x,"trailing space "
"carol",41,"",plain
`
	cr := csv.NewReader(strings.NewReader(in))
	cr.Comment = '#'
	recs, err := cr.ReadAll()
	fmt.Println("csv err", err, "records", len(recs))
	for i, rec := range recs {
		fmt.Printf("csv %d %d %q\n", i, len(rec), rec)
	}
	semi := csv.NewReader(strings.NewReader("a;b;c\n1; 2 ;3\n"))
	semi.Comma = ';'
	semi.TrimLeadingSpace = true
	for {
		rec, err := semi.Read()
		if err == io.EOF {
			break
		}
		line, col := semi.FieldPos(1)
		fmt.Printf("semi %q err=%v field1 at %d:%d offset=%d\n", rec, err, line, col, semi.InputOffset())
	}
	for _, bad := range []string{"a,\"b\nc", "a,b\nc\n", "a,\"b\"c\n", "x\"y,z\n"} {
		r := csv.NewReader(strings.NewReader(bad))
		_, err := r.ReadAll()
		var pe *csv.ParseError
		if errors.As(err, &pe) {
			fmt.Printf("csv bad %q: line %d col %d: %v\n", bad, pe.Line, pe.Column, pe.Err)
		} else {
			fmt.Printf("csv bad %q: %v\n", bad, err)
		}
	}
	lazy := csv.NewReader(strings.NewReader("a,\"b\"c,d\"e\n"))
	lazy.LazyQuotes = true
	lr, err := lazy.Read()
	fmt.Printf("lazy %q %v\n", lr, err)
	vf := csv.NewReader(strings.NewReader("a,b\nc\n"))
	vf.FieldsPerRecord = -1
	vr, err := vf.ReadAll()
	fmt.Printf("variable fields %q %v\n", vr, err)

	// CSV writing.
	var out bytes.Buffer
	cw := csv.NewWriter(&out)
	cw.Write([]string{"plain", "with,comma", "with \"quote\"", "line\nbreak", " lead", ""})
	cw.Write([]string{"#notcomment", "αβγ", "\t"})
	cw.Flush()
	fmt.Printf("csv out %q err=%v\n", out.String(), cw.Error())
	out.Reset()
	cw = csv.NewWriter(&out)
	cw.UseCRLF = true
	cw.Comma = '\t'
	cw.WriteAll([][]string{{"x", "y"}, {"1", "2\n3"}})
	fmt.Printf("csv crlf tab %q\n", out.String())
}
