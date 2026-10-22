// encoding/binary with bytes.Buffer and bytes.Reader: a 28-byte header struct
// written and read back with binary.Write and binary.Read in alternating byte
// orders, then signed and unsigned varints, then the fixed-width accessors
// over the raw bytes.
//
// binary.Write has a fast path for fixed-size scalars and slices of them, and a
// struct is neither, so this goes through reflect: encoder.value switches on
// each field's Kind, recurses into the struct and into its array element by
// element, and stores every field through the ByteOrder interface -- PutUint16,
// 32 or 64 by call_indirect through an itab.  The accessors at the end are the
// opposite extreme.  Go's wasm back end merges no
// byte loads, so LittleEndian.Uint64 is eight i64.load8_u shifted and or'ed
// together, sharing four of them with the BigEndian.Uint32 beside it.
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

type header struct {
	Magic   uint32
	Version uint16
	Flags   uint16
	Len     int64
	Score   float64
	Tag     [4]byte
}

func main() {
	var buf bytes.Buffer
	var acc uint64
	scratch := make([]byte, 0, 1<<14)
	for r := 0; r < 700; r++ {
		buf.Reset()
		for i := 0; i < 64; i++ {
			h := header{Magic: 0xfeedface ^ uint32(r), Version: uint16(i),
				Flags: uint16(i * r), Len: int64(i*r) - 5000,
				Score: float64(i) / 3, Tag: [4]byte{'r', byte(r), 'i', byte(i)}}
			order := binary.ByteOrder(binary.LittleEndian)
			if i&1 == 1 {
				order = binary.BigEndian
			}
			if err := binary.Write(&buf, order, &h); err != nil {
				panic(err)
			}
		}
		rd := bytes.NewReader(buf.Bytes())
		for i := 0; i < 64; i++ {
			var h header
			order := binary.ByteOrder(binary.LittleEndian)
			if i&1 == 1 {
				order = binary.BigEndian
			}
			if err := binary.Read(rd, order, &h); err != nil {
				panic(err)
			}
			acc += uint64(h.Magic) + uint64(h.Flags) + uint64(h.Len) +
				uint64(h.Score*8) + uint64(h.Tag[3])
		}

		// Varints: append, then decode with Uvarint and ReadVarint.
		scratch = scratch[:0]
		for i := 0; i < 1024; i++ {
			v := uint64(i*r) << uint(i%50)
			scratch = binary.AppendUvarint(scratch, v)
			scratch = binary.AppendVarint(scratch, -int64(v>>3))
		}
		br := bytes.NewReader(scratch)
		for {
			u, err := binary.ReadUvarint(br)
			if err == io.EOF {
				break
			}
			s, err := binary.ReadVarint(br)
			if err != nil {
				panic(err)
			}
			acc = acc*31 + u ^ uint64(s)
		}
		// Fixed-width accessors on the raw bytes.
		b := buf.Bytes()
		for i := 0; i+8 <= len(b); i += 8 {
			acc += binary.LittleEndian.Uint64(b[i:]) ^
				uint64(binary.BigEndian.Uint32(b[i:]))
		}
	}
	fmt.Println("acc =", acc)
}
