// Building strings by appending into a byte slice with strconv.AppendInt, the
// same source as test/gobench/strbuild.  What most Go output paths do
// underneath.
package main

import (
	"fmt"
	"strconv"
)

func main() {
	total := 0
	buf := make([]byte, 0, 1<<16)
	for r := 0; r < 100000; r++ {
		buf = buf[:0]
		for i := 0; i < 64; i++ {
			buf = append(buf, 'v')
			buf = strconv.AppendInt(buf, int64(r*64+i), 10)
			buf = append(buf, ';')
		}
		total += len(buf)
	}
	fmt.Println("total =", total)
}
