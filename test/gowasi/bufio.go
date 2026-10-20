// Buffered reading, over a file and over standard input.
//
// A Scanner refills from the underlying reader whenever its buffer runs dry,
// so a file read this way becomes many small `fd_read` calls at moving
// offsets rather than the single big one `os.ReadFile` makes -- and standard
// input is a pipe, where a short read means what it says.
//
// tinygo: -scheduler=asyncify
//
// Since Go 1.27 the standard library this reaches can start a goroutine
// (time.AfterFunc's goFunc), which TinyGo refuses to build without a
// scheduler.
package main

import (
	"bufio"
	"fmt"
	"os"
	"strings"
)

func main() {
	p := "/d/lines.txt"
	var b strings.Builder
	for i := 0; i < 500; i++ {
		fmt.Fprintf(&b, "line %04d %s\n", i, strings.Repeat("x", i%37))
	}
	if err := os.WriteFile(p, []byte(b.String()), 0644); err != nil {
		fmt.Println("write:", err)
		os.Exit(1)
	}

	f, err := os.Open(p)
	if err != nil {
		fmt.Println("open:", err)
		os.Exit(1)
	}
	sc := bufio.NewScanner(f)
	// Small on purpose: the default is large enough to swallow the file whole,
	// and then the refill path never runs.
	sc.Buffer(make([]byte, 64), 4096)
	n, chars, longest := 0, 0, 0
	for sc.Scan() {
		t := sc.Text()
		n++
		chars += len(t)
		if len(t) > longest {
			longest = len(t)
		}
	}
	fmt.Println("scan", n, chars, longest, sc.Err())
	f.Close()

	f, _ = os.Open(p)
	r := bufio.NewReaderSize(f, 32)
	first, _ := r.ReadString('\n')
	rest := 0
	for {
		line, err := r.ReadString('\n')
		rest += len(line)
		if err != nil {
			break
		}
	}
	fmt.Printf("reader %q rest=%d\n", strings.TrimRight(first, "\n"), rest)
	f.Close()

	in := bufio.NewScanner(os.Stdin)
	got := []string{}
	for in.Scan() {
		got = append(got, in.Text())
	}
	fmt.Println("stdin", len(got), got)
}
