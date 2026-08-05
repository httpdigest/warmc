// Binary layout: byte order, packing, and a file read back through ReadAt at
// offsets that straddle every boundary.
//
// encoding/binary is unaligned loads and stores of every width in both byte
// orders, which is a different corner of the back end from arithmetic; the
// straddling reads are what a positional read gets wrong when its offset
// arithmetic is off by a word.
package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
)

func main() {
	var buf bytes.Buffer
	for i := 0; i < 64; i++ {
		binary.Write(&buf, binary.LittleEndian, uint16(i*7+1))
		binary.Write(&buf, binary.BigEndian, uint32(i*1103515245+12345))
		binary.Write(&buf, binary.LittleEndian, uint64(i)<<40|0x0123456789)
		binary.Write(&buf, binary.BigEndian, float64(i)/7)
	}
	raw := buf.Bytes()
	fmt.Println("encoded", len(raw))

	h := uint64(1469598103934665603)
	for i := 0; i+8 <= len(raw); i++ {
		h = (h ^ binary.LittleEndian.Uint64(raw[i:])) * 1099511628211
		h = (h ^ binary.BigEndian.Uint64(raw[i:])) * 1099511628211
		h = (h ^ uint64(binary.LittleEndian.Uint32(raw[i:]))) * 1099511628211
		h = (h ^ uint64(binary.BigEndian.Uint16(raw[i:]))) * 1099511628211
	}
	fmt.Printf("unaligned %016x\n", h)

	p := "/d/blob.bin"
	if err := os.WriteFile(p, raw, 0644); err != nil {
		fmt.Println("write:", err)
		os.Exit(1)
	}
	f, err := os.Open(p)
	if err != nil {
		fmt.Println("open:", err)
		os.Exit(1)
	}
	defer f.Close()
	// Every size against every offset near a boundary, including reads that
	// run off the end and must come back short.
	g := uint64(1469598103934665603)
	for _, sz := range []int{1, 3, 7, 8, 9, 63, 64, 65} {
		for _, off := range []int64{0, 1, 7, 8, 4095, int64(len(raw)) - 3, int64(len(raw))} {
			b := make([]byte, sz)
			n, err := f.ReadAt(b, off)
			g = (g ^ uint64(n)) * 1099511628211
			if err != nil {
				g = (g ^ 0xdead) * 1099511628211
			}
			for _, x := range b[:n] {
				g = (g ^ uint64(x)) * 1099511628211
			}
		}
	}
	fmt.Printf("readat %016x\n", g)
}
