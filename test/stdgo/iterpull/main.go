// iter.Pull: push iterators turned into pull iterators and merged -- three
// generators under two merges -- and stopped early one round in four.
//
// rangefunc is push iteration, where the iterator calls the loop body.  Pull
// inverts that with a coroutine: each generator runs on a goroutine of its own,
// and every next() is runtime.coroswitch, which hands control straight to the
// other goroutine through gogo, with no trip through the scheduler's run queue.
// On wasm even that cannot just swap a stack pointer.  gogo returns 1, every
// wasm frame on the way out returns too, and the runtime's wasm_pc_f_loop
// re-enters the target goroutine at its saved PC by call_indirect and the
// br_table at the top of that function.  Every value pulled is two of these
// switches, there and back.  chans pays for the same unwind by way of the
// scheduler; this pays for it without one.  The deferred stop functions run on
// generators that have not finished whenever a round breaks out early.
package main

import (
	"fmt"
	"iter"
)

// A generator of sorted values: a running sum of small pseudo-random steps.
func ascending(seed uint32, n int) iter.Seq[uint64] {
	return func(yield func(uint64) bool) {
		x := seed
		var v uint64
		for i := 0; i < n; i++ {
			x ^= x << 13
			x ^= x >> 17
			x ^= x << 5
			v += uint64(x&63) + 1
			if !yield(v) {
				return
			}
		}
	}
}

// Merge two sorted sequences by pulling from each in turn.
func merge(a, b iter.Seq[uint64]) iter.Seq[uint64] {
	return func(yield func(uint64) bool) {
		na, sa := iter.Pull(a)
		defer sa()
		nb, sb := iter.Pull(b)
		defer sb()
		va, oka := na()
		vb, okb := nb()
		for oka || okb {
			if oka && (!okb || va <= vb) {
				if !yield(va) {
					return
				}
				va, oka = na()
			} else {
				if !yield(vb) {
					return
				}
				vb, okb = nb()
			}
		}
	}
}

func main() {
	var acc uint64
	for r := 0; r < 140; r++ {
		s := merge(ascending(uint32(r)*2654435761+1, 2000),
			merge(ascending(uint32(r)+77, 1500), ascending(99991, 1000+r)))
		var prev uint64
		i := 0
		for v := range s {
			if v < prev {
				panic("unsorted")
			}
			prev = v
			acc = acc*31 + v
			i++
			// Stop one round in four early, so the deferred stops run on
			// generators that have not finished.
			if r&3 == 1 && i == 3000 {
				break
			}
		}
		acc += uint64(i)
	}
	fmt.Println("acc =", acc)
}
