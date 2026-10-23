// RGB to grey over interleaved bytes: grey[i] = (77*r + 150*g + 29*b + 128) >> 8,
// with r, g and b at rgb[3*i], rgb[3*i+1] and rgb[3*i+2].  TinyGo emits three
// i32.load8_u off one pointer that advances by three, three i32.mul by
// constants, the adds, an i32.shr_u by 8 and an i32.store8 -- a fixed-point
// dot product computed in i32 on byte inputs.
//
// NEON's LD3 loads 48 bytes and deinterleaves them into three vectors in one
// instruction, which is how warmc loads the pixels, and the sum needs 16
// bits, so warmc computes it in 16-bit lanes: UMULL and UMLAL from the bytes,
// and the rounding shift as an ADDHN that narrows to the bytes stored -- the
// instructions LLVM's native build picks.  Here to keep that working.
package main

//go:noinline
func kern(rgb, grey []byte) {
	for i := range grey {
		r := uint32(rgb[3*i])
		g := uint32(rgb[3*i+1])
		b := uint32(rgb[3*i+2])
		grey[i] = byte((77*r + 150*g + 29*b + 128) >> 8)
	}
}

func main() {
	const n = 4096
	rgb := make([]byte, 3*n)
	grey := make([]byte, n)
	for i := range rgb {
		rgb[i] = byte(i*7 + i>>5)
	}
	total := 0
	for r := 0; r < 60000; r++ {
		rgb[(r*7)%(3*n)] = byte(r)
		kern(rgb, grey)
		total += int(grey[(r*13)&(n-1)])
	}
	for i := range grey {
		total += int(grey[i]) * (i&7 + 1)
	}
	println("total =", total)
}
