// encoding/json: marshal a slice of records and unmarshal it back.
//
// The largest body of library code in this corpus, and the most typical of a
// Go service: everything goes through reflect, the encoder caches one encoder
// function per type and calls it indirectly, the decoder scans byte by byte
// through a state machine of function values, and both allocate.  So this row
// says what warmc does with a lot of code that is warm rather than one loop
// that is hot.
package main

import (
	"encoding/json"
	"fmt"
)

type rec struct {
	ID    int      `json:"id"`
	Name  string   `json:"name"`
	Tags  []string `json:"tags"`
	Score float64  `json:"score"`
	Live  bool     `json:"live"`
}

func main() {
	const n = 32
	recs := make([]rec, n)
	for i := range recs {
		recs[i] = rec{ID: i, Name: fmt.Sprint("item-", i),
			Tags: []string{"a", "bb", "ccc"}[:i%3+1], Score: float64(i) / 4,
			Live: i&1 == 0}
	}
	total := 0
	for r := 0; r < 1500; r++ {
		recs[r%n].ID = r
		b, err := json.Marshal(recs)
		if err != nil {
			panic(err)
		}
		var back []rec
		if err := json.Unmarshal(b, &back); err != nil {
			panic(err)
		}
		total += len(b) + back[r%n].ID + len(back[(r+1)%n].Tags)
	}
	fmt.Println("total =", total)
}
