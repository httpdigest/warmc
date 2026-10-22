// The sync package and sync/atomic: Mutex and RWMutex guarding shared state
// under contention, TryLock, Once (including a panicking Once), OnceFunc,
// OnceValue and OnceValues, Cond with Signal and Broadcast driving a bounded
// queue and a barrier, sync.Map (Load/Store/LoadOrStore/LoadAndDelete/Swap/
// CompareAndSwap/Range, keys sorted before printing), and the typed atomics
// (Int32/Int64/Uint32/Uint64/Bool/Pointer/Value) including CompareAndSwap
// retry loops.
//
// Contention is real even on one thread: a goroutine that blocks on a lock
// parks in the runtime's semaphore table and is woken by the unlocker.  The
// printed values are all ones that are fixed regardless of interleaving.
package main

import (
	"fmt"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
)

type account struct {
	mu  sync.Mutex
	bal int
	log []int
}

func (a *account) deposit(n int) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.bal += n
	a.log = append(a.log, n)
}

type cache struct {
	mu sync.RWMutex
	m  map[string]int
}

func (c *cache) get(k string) (int, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	v, ok := c.m[k]
	return v, ok
}

func (c *cache) set(k string, v int) {
	c.mu.Lock()
	c.m[k] = v
	c.mu.Unlock()
}

type queue struct {
	mu       sync.Mutex
	notEmpty *sync.Cond
	notFull  *sync.Cond
	items    []int
	max      int
}

func newQueue(max int) *queue {
	q := &queue{max: max}
	q.notEmpty = sync.NewCond(&q.mu)
	q.notFull = sync.NewCond(&q.mu)
	return q
}

func (q *queue) put(v int) {
	q.mu.Lock()
	for len(q.items) == q.max {
		q.notFull.Wait()
	}
	q.items = append(q.items, v)
	q.notEmpty.Signal()
	q.mu.Unlock()
}

func (q *queue) get() int {
	q.mu.Lock()
	for len(q.items) == 0 {
		q.notEmpty.Wait()
	}
	v := q.items[0]
	q.items = q.items[1:]
	q.notFull.Signal()
	q.mu.Unlock()
	return v
}

type node struct {
	val  int
	next *node
}

func main() {
	// Mutex under contention.
	var acc account
	var wg sync.WaitGroup
	for g := 0; g < 10; g++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := 1; i <= 100; i++ {
				acc.deposit(g*1000 + i)
			}
		}()
	}
	wg.Wait()
	sort.Ints(acc.log)
	fmt.Println("balance", acc.bal, "entries", len(acc.log), "first", acc.log[:5], "last", acc.log[len(acc.log)-3:])

	// TryLock.
	var m sync.Mutex
	fmt.Println("TryLock free:", m.TryLock())
	fmt.Println("TryLock held:", m.TryLock())
	m.Unlock()
	fmt.Println("TryLock after unlock:", m.TryLock())
	m.Unlock()
	var rw sync.RWMutex
	rw.RLock()
	fmt.Println("RW TryRLock with reader:", rw.TryRLock())
	fmt.Println("RW TryLock with readers:", rw.TryLock())
	rw.RUnlock()
	rw.RUnlock()
	fmt.Println("RW TryLock free:", rw.TryLock())
	fmt.Println("RW TryRLock with writer:", rw.TryRLock())
	rw.Unlock()

	// RWMutex: many readers, some writers.
	c := &cache{m: map[string]int{}}
	for i := 0; i < 10; i++ {
		c.set(fmt.Sprint("k", i), i)
	}
	hits := make([]int, 8)
	var wg2 sync.WaitGroup
	for r := 0; r < 8; r++ {
		wg2.Add(1)
		go func() {
			defer wg2.Done()
			for i := 0; i < 20; i++ {
				if _, ok := c.get(fmt.Sprint("k", i)); ok {
					hits[r]++
				}
			}
		}()
	}
	for w := 0; w < 3; w++ {
		wg2.Add(1)
		go func() {
			defer wg2.Done()
			c.set(fmt.Sprint("w", w), w*w)
		}()
	}
	wg2.Wait()
	fmt.Println("reader hits", hits)
	keys := make([]string, 0, len(c.m))
	for k := range c.m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		fmt.Printf("cache %s=%d\n", k, c.m[k])
	}

	// Once, run from many goroutines.
	var once sync.Once
	var initCount atomic.Int32
	var wg3 sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg3.Add(1)
		go func() {
			defer wg3.Done()
			once.Do(func() { initCount.Add(1) })
		}()
	}
	wg3.Wait()
	fmt.Println("once ran", initCount.Load(), "times")

	// A panicking Once still counts as done.
	var pOnce sync.Once
	func() {
		defer func() { fmt.Println("once panic recovered:", recover()) }()
		pOnce.Do(func() { panic("init failed") })
	}()
	ran := false
	pOnce.Do(func() { ran = true })
	fmt.Println("second Do after panic ran:", ran)

	// OnceFunc / OnceValue / OnceValues.
	calls := 0
	f := sync.OnceFunc(func() { calls++ })
	f()
	f()
	f()
	fmt.Println("OnceFunc calls", calls)
	vcalls := 0
	ov := sync.OnceValue(func() []string { vcalls++; return strings.Fields("x y z") })
	fmt.Println("OnceValue", ov(), ov(), "calls", vcalls)
	ovs := sync.OnceValues(func() (int, error) { return 42, fmt.Errorf("err %d", 7) })
	a1, e1 := ovs()
	a2, e2 := ovs()
	fmt.Println("OnceValues", a1, e1, a2, e2, e1 == e2)

	// Cond-driven bounded queue: 3 producers, 2 consumers.
	q := newQueue(4)
	var pwg, cwg sync.WaitGroup
	got := make([][]int, 2)
	for p := 0; p < 3; p++ {
		pwg.Add(1)
		go func() {
			defer pwg.Done()
			for i := 0; i < 20; i++ {
				q.put(p*100 + i)
			}
		}()
	}
	for cn := 0; cn < 2; cn++ {
		cwg.Add(1)
		go func() {
			defer cwg.Done()
			for i := 0; i < 30; i++ {
				got[cn] = append(got[cn], q.get())
			}
		}()
	}
	pwg.Wait()
	cwg.Wait()
	all := append(append([]int{}, got[0]...), got[1]...)
	sort.Ints(all)
	fmt.Println("cond queue consumed", len(all), "sum", func() int {
		s := 0
		for _, v := range all {
			s += v
		}
		return s
	}())
	fmt.Println(all)

	// Cond Broadcast barrier.
	var bmu sync.Mutex
	bc := sync.NewCond(&bmu)
	ready := false
	arrived := 0
	phase := make([]string, 6)
	var bwg sync.WaitGroup
	for i := 0; i < 6; i++ {
		bwg.Add(1)
		go func() {
			defer bwg.Done()
			bmu.Lock()
			arrived++
			for !ready {
				bc.Wait()
			}
			phase[i] = fmt.Sprintf("g%d-released", i)
			bmu.Unlock()
		}()
	}
	for {
		bmu.Lock()
		n := arrived
		bmu.Unlock()
		if n == 6 {
			break
		}
		// Let the others reach the barrier.
		ch := make(chan struct{})
		go func() { close(ch) }()
		<-ch
	}
	bmu.Lock()
	ready = true
	bc.Broadcast()
	bmu.Unlock()
	bwg.Wait()
	fmt.Println("barrier", phase)

	// sync.Map.
	var sm sync.Map
	for i := 0; i < 12; i++ {
		sm.Store(fmt.Sprintf("key%02d", i), i*i)
	}
	actual, loaded := sm.LoadOrStore("key03", -1)
	fmt.Println("LoadOrStore existing", actual, loaded)
	actual, loaded = sm.LoadOrStore("new", -1)
	fmt.Println("LoadOrStore new", actual, loaded)
	val, ld := sm.LoadAndDelete("key05")
	fmt.Println("LoadAndDelete", val, ld)
	val, ld = sm.LoadAndDelete("key05")
	fmt.Println("LoadAndDelete again", val, ld)
	prev, ld := sm.Swap("key01", "one")
	fmt.Println("Swap", prev, ld)
	fmt.Println("CAS wrong", sm.CompareAndSwap("key02", 5, 50))
	fmt.Println("CAS right", sm.CompareAndSwap("key02", 4, 40))
	fmt.Println("CompareAndDelete", sm.CompareAndDelete("key04", 16))
	var smKeys []string
	sm.Range(func(k, v any) bool {
		smKeys = append(smKeys, fmt.Sprintf("%v=%v", k, v))
		return true
	})
	sort.Strings(smKeys)
	for _, s := range smKeys {
		fmt.Println("syncmap", s)
	}
	sm.Clear()
	cnt := 0
	sm.Range(func(k, v any) bool { cnt++; return true })
	fmt.Println("after Clear", cnt)

	// Typed atomics.
	var i32 atomic.Int32
	var i64 atomic.Int64
	var u32 atomic.Uint32
	var u64 atomic.Uint64
	var bl atomic.Bool
	var wg4 sync.WaitGroup
	for g := 0; g < 16; g++ {
		wg4.Add(1)
		go func() {
			defer wg4.Done()
			for k := 0; k < 50; k++ {
				i32.Add(int32(g))
				i64.Add(-int64(k))
				u32.Add(1)
				u64.Add(uint64(g) << 32)
				for {
					old := i64.Load()
					if i64.CompareAndSwap(old, old+1) {
						break
					}
				}
			}
		}()
	}
	wg4.Wait()
	fmt.Println("i32", i32.Load(), "i64", i64.Load(), "u32", u32.Load(), "u64", u64.Load())
	fmt.Println("bool swap", bl.Swap(true), bl.Load(), bl.CompareAndSwap(false, true), bl.CompareAndSwap(true, false), bl.Load())
	fmt.Println("u32 sub via add", u32.Add(^uint32(0)), "swap", u32.Swap(7), u32.Load())
	fmt.Println("i32 And/Or", i32.And(0xff), i32.Or(0x100), i32.Load())

	// atomic.Pointer: lock-free stack push.
	var head atomic.Pointer[node]
	var wg5 sync.WaitGroup
	for g := 0; g < 8; g++ {
		wg5.Add(1)
		go func() {
			defer wg5.Done()
			for k := 0; k < 10; k++ {
				n := &node{val: g*10 + k}
				for {
					old := head.Load()
					n.next = old
					if head.CompareAndSwap(old, n) {
						break
					}
				}
			}
		}()
	}
	wg5.Wait()
	var vals []int
	for n := head.Load(); n != nil; n = n.next {
		vals = append(vals, n.val)
	}
	sort.Ints(vals)
	fmt.Println("lock-free stack", len(vals), vals)

	// atomic.Value with a config struct.
	type config struct {
		Name string
		N    int
	}
	var av atomic.Value
	fmt.Println("empty Value", av.Load() == nil)
	av.Store(config{"first", 1})
	old := av.Swap(config{"second", 2})
	fmt.Printf("Value swap old=%+v new=%+v\n", old, av.Load())
	fmt.Println("Value CAS", av.CompareAndSwap(config{"second", 2}, config{"third", 3}), av.Load())
	func() {
		defer func() { fmt.Println("Value inconsistent type:", recover()) }()
		av.Store("a string")
	}()

	// Raw atomic functions.
	var raw int64 = 10
	fmt.Println("AddInt64", atomic.AddInt64(&raw, 5), "Swap", atomic.SwapInt64(&raw, 100), "Load", atomic.LoadInt64(&raw))
	var rawu uint32 = 1
	fmt.Println("CAS uint32", atomic.CompareAndSwapUint32(&rawu, 1, 2), rawu, atomic.CompareAndSwapUint32(&rawu, 1, 3), rawu)
}
