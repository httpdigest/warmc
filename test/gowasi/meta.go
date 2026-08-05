// Metadata: symlinks, hard links, and the difference between following one
// and not.  path_symlink, path_readlink, path_link, and path_filestat_get
// with and without the follow flag.
package main

import (
	"fmt"
	"os"
)

func main() {
	const dir = "/d"
	if err := os.WriteFile(dir+"/target.txt", []byte("payload\n"), 0644); err != nil {
		fmt.Println("write:", err)
		os.Exit(1)
	}
	if err := os.Symlink("target.txt", dir+"/link"); err != nil {
		fmt.Println("symlink:", err)
		os.Exit(1)
	}
	got, err := os.Readlink(dir + "/link")
	if err != nil {
		fmt.Println("readlink:", err)
		os.Exit(1)
	}
	fmt.Println("readlink", got)

	// Stat follows, Lstat does not -- so one says regular file and the other
	// says symlink, and a host that ignored the flag would give the same
	// answer twice.
	st, err := os.Stat(dir + "/link")
	if err != nil {
		fmt.Println("stat:", err)
		os.Exit(1)
	}
	fmt.Println("stat size", st.Size(), "symlink", st.Mode()&os.ModeSymlink != 0)
	ls, err := os.Lstat(dir + "/link")
	if err != nil {
		fmt.Println("lstat:", err)
		os.Exit(1)
	}
	fmt.Println("lstat symlink", ls.Mode()&os.ModeSymlink != 0)

	b, err := os.ReadFile(dir + "/link")
	if err != nil {
		fmt.Println("read through link:", err)
		os.Exit(1)
	}
	fmt.Printf("through link %q\n", string(b))

	if err := os.Link(dir+"/target.txt", dir+"/hard"); err != nil {
		fmt.Println("link:", err)
	} else {
		h, _ := os.Stat(dir + "/hard")
		fmt.Println("hard size", h.Size())
	}
}
