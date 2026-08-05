// The stream calls the shared programs never make: flush, and dropping a
// stream or an error.
//
// A trace of every host call across the twelve programs shared with the
// preview1 suite shows blocking-flush and the three resource-drops among the
// nine that are bound and never entered -- TinyGo takes stdout once and never
// gives it back, so the drops are reached only by a program that closes
// something.  A drop that freed the wrong side would close file descriptor 1
// and take the rest of the output with it, which is what the writes after the
// closes are here to notice.
//
// get-random-bytes is in the same list, and cannot be compared to wasmtime by
// value.  What is comparable is everything around the value: the length asked
// for is the length returned, a second draw is not the first, and zero bytes
// is not an error.
package main

import (
	"crypto/rand"
	"fmt"
	"os"
)

func main() {
	const d = "/d"

	// Closing a file drops its descriptor; the writes afterwards go through
	// the stdout stream, which must be untouched by that.
	f, err := os.Create(d + "/closed.txt")
	if err != nil {
		fmt.Println("create:", err)
		return
	}
	f.WriteString("written\n")
	fmt.Println("close file:", f.Close() == nil)
	fmt.Println("double close is an error:", f.Close() != nil)

	g, err := os.Open(d + "/closed.txt")
	fmt.Println("reopen:", err == nil)
	if err == nil {
		b := make([]byte, 32)
		n, _ := g.Read(b)
		fmt.Println("read back:", string(b[:n]))
		g.Close()
	}

	// os.Stdout.Sync is a flush on a stream rather than on a descriptor.
	fmt.Println("stdout sync:", os.Stdout.Sync() == nil)

	one := make([]byte, 64)
	if _, err := rand.Read(one); err != nil {
		fmt.Println("random:", err)
	}
	two := make([]byte, 64)
	if _, err := rand.Read(two); err != nil {
		fmt.Println("random:", err)
	}
	same := true
	for i := range one {
		if one[i] != two[i] {
			same = false
			break
		}
	}
	fmt.Println("random 64:", len(one), "repeats:", same)

	zero := make([]byte, 0)
	n, err := rand.Read(zero)
	fmt.Println("random 0:", n, err == nil)

	// And stderr still works after all of it, which it would not if a drop
	// had closed a standard descriptor.
	fmt.Fprintln(os.Stderr, "stderr survives")
	fmt.Println("done")
}
