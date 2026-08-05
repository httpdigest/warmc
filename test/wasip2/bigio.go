// Lists big enough that returning one is an allocation problem.
//
// A preview1 guest passes the host a buffer and the host fills it.  A preview2
// host returns a `list<u8>`, which means it has to allocate inside the guest
// by calling back into the guest's own cabi_realloc while the guest is
// suspended in a host call -- re-entering the instance from C, mid-call.  Any
// short write, off-by-one in the allocated length, or failure to bounds-check
// the pointer that comes back shows up here and nowhere in a small read.
//
// The checksum is over content the program generated, so a read that returns
// the right number of the wrong bytes is caught as well as a short one.  The
// directory is large enough that read-directory-entry is pumped many times
// rather than once, and it is sorted before printing because the order a
// directory is read in is not specified and the two engines need not agree.
package main

import (
	"fmt"
	"os"
	"sort"
)

func sum(b []byte) uint64 {
	var h uint64 = 1469598103934665603
	for _, c := range b {
		h ^= uint64(c)
		h *= 1099511628211
	}
	return h
}

func main() {
	const d = "/d"

	big := make([]byte, 1<<20)
	for i := range big {
		big[i] = byte(i*7 + i/251)
	}
	if err := os.WriteFile(d+"/big.bin", big, 0644); err != nil {
		fmt.Println("write big:", err)
		return
	}
	back, err := os.ReadFile(d + "/big.bin")
	fmt.Println("big:", len(back), err == nil, sum(back) == sum(big))

	// A read whose length is not a round number, at an offset that is not
	// either, so a length computed from the wrong end is visible.
	f, err := os.Open(d + "/big.bin")
	if err != nil {
		fmt.Println("open big:", err)
		return
	}
	odd := make([]byte, 65537)
	n, err := f.ReadAt(odd, 12345)
	fmt.Println("odd read:", n, err == nil, sum(odd) == sum(big[12345:12345+65537]))
	f.Close()

	// Empty is the other end of the same code: a zero-length list must not
	// allocate and must not come back as a null the guest then reads.
	if err := os.WriteFile(d+"/empty.bin", nil, 0644); err != nil {
		fmt.Println("write empty:", err)
	}
	e, err := os.ReadFile(d + "/empty.bin")
	fmt.Println("empty:", len(e), err == nil)

	if err := os.Mkdir(d+"/many", 0755); err != nil {
		fmt.Println("mkdir many:", err)
		return
	}
	for i := 0; i < 200; i++ {
		os.WriteFile(fmt.Sprintf("%s/many/f%03d", d, i), []byte{byte(i)}, 0644)
	}
	names, err := os.ReadDir(d + "/many")
	if err != nil {
		fmt.Println("readdir many:", err)
		return
	}
	got := make([]string, 0, len(names))
	for _, e := range names {
		got = append(got, e.Name())
	}
	sort.Strings(got)
	fmt.Println("entries:", len(got), got[0], got[len(got)-1])

	// A name at the length limit, to pin the string that comes back out of
	// read-directory-entry rather than the count of them.
	long := d + "/many/" + string(make([]byte, 0))
	for i := 0; i < 200; i++ {
		long += "x"
	}
	if err := os.WriteFile(long, []byte("y"), 0644); err != nil {
		fmt.Println("long name:", err)
	} else {
		names, _ = os.ReadDir(d + "/many")
		longest := ""
		for _, e := range names {
			if len(e.Name()) > len(longest) {
				longest = e.Name()
			}
		}
		fmt.Println("longest entry:", len(longest))
	}
}
