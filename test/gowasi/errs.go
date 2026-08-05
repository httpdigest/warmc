// The refusals.  A host is as much its errors as its successes: a program
// branches on them, and two hosts that succeed alike but fail differently are
// not interchangeable.
package main

import (
	"fmt"
	"os"
)

func show(what string, err error) {
	if err == nil {
		fmt.Println(what, "ok")
		return
	}
	// The message, not the wrapped errno: it is what a program prints and
	// what a user sees.
	fmt.Println(what, err)
}

func main() {
	const dir = "/d"
	_, err := os.Open(dir + "/missing.txt")
	show("open missing:", err)

	show("mkdir over file:", func() error {
		os.WriteFile(dir+"/f", []byte("x"), 0644)
		return os.Mkdir(dir+"/f", 0755)
	}())

	show("rmdir non-empty:", func() error {
		os.Mkdir(dir+"/full", 0755)
		os.WriteFile(dir+"/full/inner", []byte("x"), 0644)
		return os.Remove(dir + "/full")
	}())

	show("remove missing:", os.Remove(dir+"/nope"))

	// Reading a directory as a file is deliberately not here: TinyGo panics
	// on it before either host is consulted, so the case would test the two
	// engines' panic messages and nothing else.
	f, err := os.Open(dir + "/full")
	if err == nil {
		_, err = f.Read(make([]byte, 4))
		f.Close()
	}
	show("read a directory:", err)

	show("create exclusive twice:", func() error {
		f, e := os.OpenFile(dir+"/excl", os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
		if e != nil {
			return e
		}
		f.Close()
		f2, e := os.OpenFile(dir+"/excl", os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
		if e == nil {
			f2.Close()
		}
		return e
	}())

	show("escape with ..:", func() error {
		_, e := os.Open(dir + "/../escape.secret")
		return e
	}())

	show("absolute outside:", func() error {
		_, e := os.Open("/etc/hosts")
		return e
	}())

	// Descending *through* a regular file.  The walk opens each component
	// with O_DIRECTORY|O_NOFOLLOW and, when that says ENOTDIR, tries to read
	// the component as a symlink before giving up -- so for a while the
	// error it reported was the readlink's EINVAL rather than the ENOTDIR
	// that was the actual reason.  Both hosts walk with the same code, so
	// this case is here rather than in either suite's own directory.
	show("open through a file:", func() error {
		os.WriteFile(dir+"/plain", []byte("x"), 0644)
		_, e := os.Open(dir + "/plain/inner")
		return e
	}())
	show("mkdir under a file:", os.Mkdir(dir+"/plain/child", 0755))
}
