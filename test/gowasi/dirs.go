// Directories: make them, list them, walk them, rename and remove.
//
// os.ReadDir is fd_readdir, which is the one preview1 call with a resumable
// cursor -- and a walk calls it once per directory with whatever buffer the
// runtime chose, so a host that mishandles the cookie shows up here and
// nowhere in a hand-written case.
package main

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
)

func main() {
	const root = "/d/tree"
	for _, d := range []string{root + "/a/x", root + "/a/y", root + "/b"} {
		if err := os.MkdirAll(d, 0755); err != nil {
			fmt.Println("mkdir:", err)
			os.Exit(1)
		}
	}
	// Enough of them that one fd_readdir call will not hold the whole
	// directory, which is what makes the cursor matter.
	for i := 0; i < 40; i++ {
		p := fmt.Sprintf("%s/a/f%02d.txt", root, i)
		if err := os.WriteFile(p, []byte(fmt.Sprintf("%d\n", i)), 0644); err != nil {
			fmt.Println("write:", err)
			os.Exit(1)
		}
	}

	ents, err := os.ReadDir(root + "/a")
	if err != nil {
		fmt.Println("readdir:", err)
		os.Exit(1)
	}
	names := []string{}
	for _, e := range ents {
		names = append(names, fmt.Sprintf("%s/%v", e.Name(), e.IsDir()))
	}
	sort.Strings(names)
	fmt.Println("entries", len(names))
	for _, n := range names[:5] {
		fmt.Println(" ", n)
	}

	var seen []string
	err = filepath.WalkDir(root, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			seen = append(seen, p)
		}
		return nil
	})
	if err != nil {
		fmt.Println("walk:", err)
		os.Exit(1)
	}
	sort.Strings(seen)
	fmt.Println("dirs", seen)

	if err := os.Rename(root+"/b", root+"/c"); err != nil {
		fmt.Println("rename:", err)
		os.Exit(1)
	}
	// Emptied by hand rather than with os.RemoveAll: TinyGo's does not work
	// on wasip1 at all, so a case built on it would agree with wasmtime about
	// failing and exercise none of what is below.
	sub, err := os.ReadDir(root + "/a")
	if err != nil {
		fmt.Println("readdir a:", err)
		os.Exit(1)
	}
	for _, e := range sub {
		p := root + "/a/" + e.Name()
		var err error
		if e.IsDir() {
			err = os.Remove(p)
		} else {
			err = os.Remove(p)
		}
		if err != nil {
			fmt.Println("remove:", err)
			os.Exit(1)
		}
	}
	if err := os.Remove(root + "/a"); err != nil {
		fmt.Println("rmdir a:", err)
		os.Exit(1)
	}
	rest, _ := os.ReadDir(root)
	for _, e := range rest {
		fmt.Println("left", e.Name())
	}
}
