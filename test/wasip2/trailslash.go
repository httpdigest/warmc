// Every path operation on a name that ends in a slash, through the WASI 0.2
// host: the leaf before it must be a directory, or a link to one where the
// operation follows it.  Making a directory or renaming by such a name acts
// on the entry itself; the rest act on what the name walks into.  Each line
// is the error Go builds of the code that came back, and the tree left
// behind is compared with wasmtime's too.
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
	fmt.Println(what, err)
}

func main() {
	const d = "/d/t"
	show("setup dir:", os.Mkdir(d, 0o755))
	show("setup d:", os.Mkdir(d+"/d", 0o755))
	show("setup f:", os.WriteFile(d+"/f", []byte("f\n"), 0o644))
	show("setup ld:", os.Symlink("d", d+"/ld"))
	show("setup lf:", os.Symlink("f", d+"/lf"))
	show("setup lx:", os.Symlink("x", d+"/lx"))

	show("mkdir n/:", os.Mkdir(d+"/n/", 0o755))
	show("mkdir n2//:", os.Mkdir(d+"/n2//", 0o755))
	show("mkdir d/:", os.Mkdir(d+"/d/", 0o755))
	show("mkdir f/:", os.Mkdir(d+"/f/", 0o755))
	// not lx/, a link to nothing: wasmtime's preview 2 host hands it to the
	// kernel, and macOS's makes the link's target where Linux's answers
	// EEXIST, as warmc does on both and test/wasi/trailslash.wat checks
	open := func(name string, flag int) error {
		f, e := os.OpenFile(name, flag, 0o644)
		if e == nil {
			f.Close()
		}
		return e
	}
	show("open f/:", open(d+"/f/", os.O_RDONLY))
	show("open d/:", open(d+"/d/", os.O_RDONLY))
	show("open ld/:", open(d+"/ld/", os.O_RDONLY))
	show("open lf/:", open(d+"/lf/", os.O_RDONLY))
	show("create new/:", open(d+"/new/", os.O_CREATE|os.O_WRONLY))
	show("create d/:", open(d+"/d/", os.O_CREATE|os.O_WRONLY))
	statE := func(name string) error { _, e := os.Lstat(name); return e }
	show("lstat f/:", statE(d+"/f/"))
	show("lstat d/:", statE(d+"/d/"))
	show("lstat ld/:", statE(d+"/ld/"))
	show("readlink ld/:", func() error { _, e := os.Readlink(d + "/ld/"); return e }())
	show("rename n/ m:", os.Rename(d+"/n/", d+"/m"))
	show("rename m k/:", os.Rename(d+"/m", d+"/k/"))
	show("rename f/ g:", os.Rename(d+"/f/", d+"/g"))
	show("rename f g/:", os.Rename(d+"/f", d+"/g/"))
	show("rename ld/ le:", os.Rename(d+"/ld/", d+"/le"))
	show("link f h/:", os.Link(d+"/f", d+"/h/"))
	show("symlink f s/:", os.Symlink("f", d+"/s/"))
	show("remove f/:", os.Remove(d+"/f/"))
	show("remove k/:", os.Remove(d+"/k/"))
	show("remove n2//:", os.Remove(d+"/n2//"))

	entries, err := os.ReadDir(d)
	show("readdir:", err)
	for _, e := range entries {
		fmt.Println(" ", e.Name())
	}
}
