// Every field of descriptor-stat, which is the largest record that crosses
// the boundary.
//
// It is a record of six fields whose last three are option<datetime>, and
// datetime is itself a record of a u64 and a u32.  That makes it 96 bytes with
// the timestamps at 24, 48 and 72 -- and the host writes those offsets by
// hand, because `--dump-canon` cannot resolve `datetime` across the instance
// boundary and prints a size of 48 for the whole record.  So the one number
// warmc could have told us is the one it gets wrong, and the offsets here are
// checked against the only other thing that knows them: wasmtime.
//
// A timestamp cannot be compared between two engines -- they ran at different
// moments over different directories -- so what is printed is what survives
// that: whether the clock is plausible, and whether the order of two writes is
// the order of their modification times.  A host that read the seconds from
// the wrong offset answers 1970 or a number far in the future; one that got
// the option discriminant wrong answers a zero time.
package main

import (
	"fmt"
	"os"
	"time"
)

func main() {
	const d = "/d"

	for _, name := range []string{"/seed.txt", "/sub"} {
		fi, err := os.Stat(d + name)
		if err != nil {
			fmt.Println("stat", name, err)
			continue
		}
		fmt.Printf("%s size=%d dir=%v regular=%v\n",
			name, fi.Size(), fi.IsDir(), fi.Mode().IsRegular())
		mt := fi.ModTime()
		fmt.Printf("%s mtime sane=%v zero=%v\n", name,
			mt.Year() >= 2020 && mt.Year() < 2200, mt.IsZero())
	}

	if err := os.WriteFile(d+"/first.txt", []byte("one"), 0644); err != nil {
		fmt.Println("write first:", err)
	}
	// Enough of a gap that two writes cannot share a timestamp at any
	// plausible clock resolution.
	time.Sleep(20 * time.Millisecond)
	if err := os.WriteFile(d+"/second.txt", []byte("two"), 0644); err != nil {
		fmt.Println("write second:", err)
	}
	a, err1 := os.Stat(d + "/first.txt")
	b, err2 := os.Stat(d + "/second.txt")
	if err1 != nil || err2 != nil {
		fmt.Println("stat pair:", err1, err2)
		return
	}
	fmt.Println("second is newer:", b.ModTime().After(a.ModTime()))
	fmt.Println("sizes:", a.Size(), b.Size())

	// The link is made by run_wasip2.sh: a TinyGo guest cannot make one.
	li, err := os.Lstat(d + "/inside")
	if err != nil {
		fmt.Println("lstat symlink:", err)
	} else {
		fmt.Println("lstat symlink:", li.Mode()&os.ModeSymlink != 0,
			li.Mode().IsRegular())
	}
	si, err := os.Stat(d + "/inside")
	if err != nil {
		fmt.Println("stat through symlink:", err)
	} else {
		fmt.Println("stat through symlink:", si.Size(), si.Mode().IsRegular())
	}

	f, err := os.Open(d + "/sub")
	if err != nil {
		fmt.Println("open dir:", err)
		return
	}
	dfi, err := f.Stat()
	if err != nil {
		fmt.Println("fstat a directory:", err)
	} else {
		fmt.Println("fstat a directory:", dfi.IsDir(), dfi.Mode().IsRegular())
	}
	f.Close()
}
