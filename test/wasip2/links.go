// readlink-at, link-at and sync-data.
//
// A trace of every host call across the twelve programs shared with the
// preview1 suite comes back with these among the nine that are bound and never
// entered.  link-at is the only filesystem call taking two descriptors *and* a
// path-flags word, and readlink-at is the only one that returns a bare string
// rather than a list or a record, so each has a shape nothing else covers.
//
// symlink-at cannot be reached at all from a TinyGo guest: its symlink()
// resolves the link target as if it were a path under a preopen, so the call
// fails before the host sees it, on wasmtime as much as here.  The links this
// reads are made by run_wasip2.sh instead, which is also what lets the target
// strings be things a guest could not have written.
//
// A hard link is checked through the content of both names and the failure to
// link over an existing one, rather than through a link count -- fs.FileInfo
// on wasip2 carries no Sys() to read a count out of.
package main

import (
	"fmt"
	"os"
)

func main() {
	const d = "/d"

	for _, n := range []string{"inside", "dangling", "up", "subdir-link"} {
		tgt, err := os.Readlink(d + "/" + n)
		fmt.Printf("readlink %s: %q %v\n", n, tgt, err == nil)
	}
	_, err := os.Readlink(d + "/seed.txt")
	fmt.Println("readlink a regular file:", err != nil)
	_, err = os.Readlink(d + "/nope")
	fmt.Println("readlink a missing name:", err != nil)

	// Lstat sees the link, Stat follows it.
	li, err := os.Lstat(d + "/inside")
	if err != nil {
		fmt.Println("lstat:", err)
	} else {
		fmt.Println("lstat inside: symlink=", li.Mode()&os.ModeSymlink != 0)
	}
	si, err := os.Stat(d + "/inside")
	if err != nil {
		fmt.Println("stat:", err)
	} else {
		fmt.Println("stat inside: regular=", si.Mode().IsRegular(), "size=", si.Size())
	}
	li, err = os.Lstat(d + "/dangling")
	if err != nil {
		fmt.Println("lstat dangling:", err)
	} else {
		fmt.Println("lstat dangling: symlink=", li.Mode()&os.ModeSymlink != 0)
	}
	_, err = os.Stat(d + "/dangling")
	fmt.Println("stat dangling fails:", err != nil)

	fmt.Println("link:", os.Link(d+"/seed.txt", d+"/hard") == nil)
	b, err := os.ReadFile(d + "/hard")
	fmt.Printf("hard reads: %q %v\n", string(b), err == nil)
	fmt.Println("link over existing fails:", os.Link(d+"/seed.txt", d+"/hard") != nil)
	fmt.Println("link from missing fails:", os.Link(d+"/nope", d+"/hard2") != nil)

	// Writing through one name is visible through the other, which a copy
	// would not be.
	if err := os.WriteFile(d+"/hard", []byte("changed\n"), 0644); err != nil {
		fmt.Println("write hard:", err)
	}
	b, _ = os.ReadFile(d + "/seed.txt")
	fmt.Printf("other name sees: %q\n", string(b))

	f, err := os.Create(d + "/synced.txt")
	if err != nil {
		fmt.Println("create:", err)
		return
	}
	f.WriteString("durable\n")
	fmt.Println("sync:", f.Sync() == nil)
	fmt.Println("close:", f.Close() == nil)
	b, err = os.ReadFile(d + "/synced.txt")
	fmt.Printf("after sync: %q %v\n", string(b), err == nil)
}
