// stdin, stdout and stderr as wasi:io streams.
//
// In preview1 the three are file descriptors and fd_read/fd_write reach them
// by number.  In preview2 each is a resource: wasi:cli/stdin's get-stdin
// hands back an own<input-stream>, and reading it is
// [method]input-stream.blocking-read, which returns its bytes as a list the
// host allocates in the guest.  End of input is not a zero-length read but a
// stream-error::closed, and getting that wrong makes a reader either loop or
// stop early -- which is what this checks by reading to EOF.
package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
)

func main() {
	b, err := io.ReadAll(os.Stdin)
	fmt.Println("stdin", len(b), err == nil)
	w := bufio.NewWriter(os.Stdout)
	for i := 0; i < 3; i++ {
		fmt.Fprintf(w, "line %d\n", i)
	}
	w.Flush()
	fmt.Fprintln(os.Stderr, "on stderr")
}
