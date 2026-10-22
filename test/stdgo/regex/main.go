// regexp: three compiled patterns over 200 generated log lines, with
// submatches taken as strings, as indices, and not at all.
//
// Go's regexp promises linear time, so it picks an engine per call rather than
// backtracking without bound.  None of these patterns is one-pass and both the
// programs and the lines are short, so every call here goes to the bit-state
// backtracker: tryBacktrack pops jobs off a slice, marks each (instruction,
// position) pair in a visited bitmap, and dispatches on the instruction's op,
// reading the text a rune at a time through the regexp's `input` interface --
// a call_indirect per step.  The status pattern has a literal prefix, so its
// search skips ahead with strings.Index first.  A library engine that is an
// interpreter, the shape an ahead-of-time compiler has least to work with.
package main

import (
	"fmt"
	"regexp"
	"strings"
)

var users = []string{"ada", "bob", "carol", "dmitri", "eve", "fatima"}
var hosts = []string{"example", "mail", "corp-net", "x9"}
var tlds = []string{"com", "org", "net", "io"}
var verbs = []string{"GET", "POST", "PUT", "DELETE"}

func main() {
	var sb strings.Builder
	x := uint32(12345)
	next := func() uint32 {
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		return x
	}
	lines := make([]string, 0, 200)
	for i := 0; i < 200; i++ {
		sb.Reset()
		a := next()
		fmt.Fprintf(&sb, "%s /api/v%d/items/%d from %s@%s.%s status=%d",
			verbs[a&3], a>>2&3, a>>4&0xffff, users[a>>20%6], hosts[a>>24&3],
			tlds[a>>28&3], 200+int(a>>8&3)*100+int(a>>12&7))
		if a&31 == 0 {
			sb.WriteString(" malformed@@")
		}
		lines = append(lines, sb.String())
	}
	req := regexp.MustCompile(`^(GET|POST|PUT|DELETE) /api/v(\d)/items/(\d+)`)
	mail := regexp.MustCompile(`([a-z]+)@([a-z0-9-]+)\.(com|org|net)\b`)
	status := regexp.MustCompile(`status=([45]\d\d)`)
	var acc uint64
	for r := 0; r < 150; r++ {
		for _, ln := range lines {
			if m := req.FindStringSubmatch(ln); m != nil {
				acc += uint64(len(m[1]) + int(m[2][0]) + len(m[3]))
			}
			if m := mail.FindStringSubmatchIndex(ln); m != nil {
				acc = acc*31 + uint64(m[2]+m[4]*3+m[6]*5+m[7])
			} else {
				acc++
			}
			if status.MatchString(ln) {
				acc += 7
			}
		}
	}
	fmt.Println("acc =", acc)
}
