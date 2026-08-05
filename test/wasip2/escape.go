// Getting out of the preopen, five ways.
//
// A preview2 descriptor is a capability and every path is relative to one, so
// the only way out is to talk the host into resolving a component of the path
// somewhere else: an absolute path, a `..` that climbs past the root, a
// symlink whose target is absolute, one whose target climbs, and one reached
// part-way along a longer path rather than at its end.  The last is the one a
// check written at the leaf misses.
//
// The host walks a path one component at a time with openat and O_NOFOLLOW,
// reading and re-splicing any symlink itself.  That walk is shared with the
// preview1 host -- rt/wasifs.c -- so this is the second suite standing on it.
//
// The links are made by run_wasip2.sh rather than here, because TinyGo's
// wasip2 symlink() resolves the link *target* as a path under a preopen and so
// can never make one.  A guest that could would still be testing the same
// walk; a guest that silently cannot would be testing nothing, which is what
// this case did until the links moved out.
package main

import (
	"fmt"
	"os"
)

func try(what, path string) {
	b, err := os.ReadFile(path)
	if err != nil {
		fmt.Println(what, "refused")
		return
	}
	fmt.Println(what, "READ", len(b), "bytes")
}

func main() {
	const d = "/d"

	// Out.
	try("absolute:", "/etc/hosts")
	try("dotdot past the root:", d+"/../../../etc/hosts")
	try("through an absolute link:", d+"/abs-link")
	try("through a climbing link:", d+"/rel-link")
	try("through a link mid-path:", d+"/up/etc/hosts")

	// In, and still working -- otherwise refusing everything would pass.
	try("dotdot that stays in:", d+"/sub/../seed.txt")
	try("through a link that stays in:", d+"/inside")
	try("through a link to a directory:", d+"/subdir-link/inner.txt")

	// A dangling link is not an escape, and reading it fails for its own
	// reason rather than the sandbox's.
	_, err := os.ReadFile(d + "/dangling")
	fmt.Println("dangling:", err != nil)

	// Writing out is the same walk with a different call at the end.
	err = os.WriteFile(d+"/../escaped.txt", []byte("x"), 0644)
	fmt.Println("write past the root:", err != nil)
	err = os.WriteFile(d+"/abs-link", []byte("x"), 0644)
	fmt.Println("write through an absolute link:", err != nil)
	err = os.Mkdir(d+"/../escaped-dir", 0755)
	fmt.Println("mkdir past the root:", err != nil)
	err = os.Remove(d + "/../seed.txt")
	fmt.Println("remove past the root:", err != nil)
}
