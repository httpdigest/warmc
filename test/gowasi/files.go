// Files: create, write, append, read back, stat, remove.
//
// Everything here goes through wasi-libc's path resolution rather than
// calling preview1 directly, which is the point: a hand-written case knows
// what it is testing and a real program does not.
package main

import (
	"fmt"
	"os"
)

func main() {
	const dir = "/d"
	p := dir + "/note.txt"

	if err := os.WriteFile(p, []byte("first\n"), 0644); err != nil {
		fmt.Println("write:", err)
		os.Exit(1)
	}
	f, err := os.OpenFile(p, os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		fmt.Println("append open:", err)
		os.Exit(1)
	}
	if _, err := f.WriteString("second\n"); err != nil {
		fmt.Println("append:", err)
		os.Exit(1)
	}
	f.Close()

	b, err := os.ReadFile(p)
	if err != nil {
		fmt.Println("read:", err)
		os.Exit(1)
	}
	fmt.Printf("contents %q\n", string(b))

	st, err := os.Stat(p)
	if err != nil {
		fmt.Println("stat:", err)
		os.Exit(1)
	}
	fmt.Println("size", st.Size(), "dir", st.IsDir())

	if err := os.Remove(p); err != nil {
		fmt.Println("remove:", err)
		os.Exit(1)
	}
	if _, err := os.Stat(p); err == nil {
		fmt.Println("still there")
		os.Exit(1)
	}
	fmt.Println("gone")
}
