// Preopens named by their host path: --dir=DIR with no ::GUEST gives the
// guest the directory under DIR as written.  Each argument is such a
// directory; the program reads seed.txt from each through the name it was
// given.  The host kept the name in a buffer of the function that parsed the
// flag, so the guest got whatever that stack held later.
package main

import (
	"fmt"
	"os"
)

func main() {
	for _, d := range os.Args[1:] {
		b, err := os.ReadFile(d + "/seed.txt")
		if err != nil {
			fmt.Println("error:", err)
			continue
		}
		fmt.Printf("%s", b)
	}
}
