// The filesystem error surface, which is where a host is most easily wrong
// and least easily caught.
//
// preview1 hands back an errno the host got from the kernel and barely
// touches.  preview2 hands back `error-code`, a 37-case enum whose numbering
// has nothing to do with any platform's, so every call site is a mapping the
// host wrote by hand -- twice over, because the sandbox walk returns preview1
// numbers and those are mapped again.  A wrong entry is invisible in the happy
// path and turns into the wrong branch in a program that checks for
// os.IsNotExist or os.IsExist.
//
// Each line is the message Go builds out of the code that came back, so a
// mapping that lands one case away shows up as a different word.  Nothing here
// is about warmc's opinion of the right answer: wasmtime maps the same
// kernel error to the same case, or the two disagree and this says so.
package main

import (
	"fmt"
	"os"
	"strings"
)

func show(what string, err error) {
	if err == nil {
		fmt.Println(what, "ok")
		return
	}
	fmt.Println(what, err)
}

func main() {
	const d = "/d"

	show("open missing:", func() error { _, e := os.Open(d + "/nope"); return e }())
	show("stat missing:", func() error { _, e := os.Stat(d + "/nope"); return e }())
	show("open dir for write:", func() error {
		_, e := os.OpenFile(d+"/sub", os.O_WRONLY, 0)
		return e
	}())
	show("create exclusive over existing:", func() error {
		_, e := os.OpenFile(d+"/seed.txt", os.O_CREATE|os.O_EXCL, 0644)
		return e
	}())
	show("mkdir over existing dir:", os.Mkdir(d+"/sub", 0755))
	show("rmdir a file:", func() error {
		// os.Remove tries unlink then rmdir, so name the syscall directly by
		// removing something that is a file through the directory path.
		return os.Remove(d + "/sub/inner.txt/deeper")
	}())
	show("open through a file:", func() error {
		_, e := os.Open(d + "/seed.txt/inner")
		return e
	}())
	show("mkdir under a file:", os.Mkdir(d+"/seed.txt/child", 0755))
	show("remove missing:", os.Remove(d+"/nope"))
	show("rename missing:", os.Rename(d+"/nope", d+"/other"))
	show("rename onto non-empty dir:", os.Rename(d+"/seed.txt", d+"/sub"))
	show("readlink a regular file:", func() error {
		_, e := os.Readlink(d + "/seed.txt")
		return e
	}())
	show("link onto existing:", os.Link(d+"/seed.txt", d+"/sub/inner.txt"))
	show("name too long:", func() error {
		_, e := os.Create(d + "/" + strings.Repeat("n", 4096))
		return e
	}())
	show("empty path:", func() error { _, e := os.Open(d + "/"); return e }())

	// A directory that is not empty cannot go, and one that is can.
	os.Mkdir(d+"/full", 0755)
	os.WriteFile(d+"/full/x", []byte("x"), 0644)
	show("rmdir non-empty:", os.Remove(d+"/full"))
	os.Remove(d + "/full/x")
	show("rmdir emptied:", os.Remove(d+"/full"))
}
