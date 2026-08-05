// Positional and seeking I/O: fd_seek, fd_tell, fd_pread, fd_pwrite,
// fd_filestat_set_size and fd_sync, none of which os.ReadFile ever reaches.
package main

import (
	"fmt"
	"io"
	"os"
)

func main() {
	p := "/d/data.bin"
	f, err := os.Create(p)
	if err != nil {
		fmt.Println("create:", err)
		os.Exit(1)
	}
	if _, err := f.Write([]byte("0123456789abcdef")); err != nil {
		fmt.Println("write:", err)
		os.Exit(1)
	}
	if _, err := f.WriteAt([]byte("XY"), 4); err != nil {
		fmt.Println("writeat:", err)
		os.Exit(1)
	}
	at, err := f.Seek(-6, io.SeekEnd)
	if err != nil {
		fmt.Println("seek:", err)
		os.Exit(1)
	}
	fmt.Println("seek end-6 ->", at)
	buf := make([]byte, 3)
	if _, err := io.ReadFull(f, buf); err != nil {
		fmt.Println("read:", err)
		os.Exit(1)
	}
	fmt.Printf("read %q\n", string(buf))
	pos, _ := f.Seek(0, io.SeekCurrent)
	fmt.Println("tell", pos)

	got := make([]byte, 4)
	if _, err := f.ReadAt(got, 2); err != nil {
		fmt.Println("readat:", err)
		os.Exit(1)
	}
	fmt.Printf("readat %q\n", string(got))

	if err := f.Truncate(8); err != nil {
		fmt.Println("truncate:", err)
		os.Exit(1)
	}
	if err := f.Sync(); err != nil {
		fmt.Println("sync:", err)
		os.Exit(1)
	}
	f.Close()

	b, _ := os.ReadFile(p)
	fmt.Printf("final %q\n", string(b))
}
