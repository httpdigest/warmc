// context without the clock: WithCancel trees whose cancellation propagates
// to every descendant, WithCancelCause and context.Cause, WithValue lookups
// through a chain of parents (with shadowing and typed keys), AfterFunc and
// its stop function, WithoutCancel detaching a child, and goroutines that
// observe Done() through select and report how far they got.
//
// No deadlines or timeouts: those read the clock, which makes output depend on
// the machine.  Cancellation here is always an explicit call, and goroutines
// are synchronised so that what they report is fixed.
package main

import (
	"context"
	"errors"
	"fmt"
	"sort"
	"sync"
)

type ctxKey string
type intKey int

func describe(name string, ctx context.Context) {
	err := ctx.Err()
	select {
	case <-ctx.Done():
		fmt.Printf("%-10s done  err=%v cause=%v\n", name, err, context.Cause(ctx))
	default:
		fmt.Printf("%-10s alive err=%v\n", name, err)
	}
}

func counter(ctx context.Context, step chan struct{}, out chan<- int) {
	n := 0
	for {
		select {
		case <-ctx.Done():
			out <- n
			return
		case <-step:
			n++
			step <- struct{}{}
		}
	}
}

func main() {
	bg := context.Background()
	fmt.Println("Background:", bg, "TODO:", context.TODO())
	fmt.Println("Background Done is nil:", bg.Done() == nil, "Err:", bg.Err())

	// A tree of cancel contexts.
	root, cancelRoot := context.WithCancel(bg)
	a, cancelA := context.WithCancel(root)
	b, cancelB := context.WithCancel(root)
	a1, cancelA1 := context.WithCancel(a)
	a2 := context.WithValue(a, ctxKey("who"), "a2")
	b1, cancelB1 := context.WithCancel(b)
	defer cancelA1()
	defer cancelB1()
	all := []struct {
		n string
		c context.Context
	}{{"root", root}, {"a", a}, {"b", b}, {"a1", a1}, {"a2", a2}, {"b1", b1}}
	show := func(label string) {
		fmt.Println("--", label)
		for _, x := range all {
			describe(x.n, x.c)
		}
	}
	show("initial")
	cancelA()
	show("after cancel a")
	cancelA() // idempotent
	cancelB()
	show("after cancel b")
	cancelRoot()
	show("after cancel root")

	// Cancel causes.
	cc, cancelCause := context.WithCancelCause(bg)
	child, cancelChild := context.WithCancel(cc)
	defer cancelChild()
	myErr := errors.New("shutdown requested")
	cancelCause(myErr)
	describe("cc", cc)
	describe("cc-child", child)
	fmt.Println("Is Canceled:", errors.Is(cc.Err(), context.Canceled), "cause is myErr:", context.Cause(child) == myErr)
	cc2, cancelCause2 := context.WithCancelCause(bg)
	cancelCause2(nil)
	fmt.Println("nil cause becomes:", context.Cause(cc2))

	// Values through a chain.
	v := context.WithValue(bg, ctxKey("user"), "alice")
	v = context.WithValue(v, intKey(1), 100)
	v = context.WithValue(v, ctxKey("role"), "admin")
	v = context.WithValue(v, ctxKey("user"), "bob") // shadows
	vc, vcancel := context.WithCancel(v)
	for _, k := range []any{ctxKey("user"), ctxKey("role"), intKey(1), intKey(2), "user", 1} {
		fmt.Printf("Value(%T %v) = %v\n", k, k, vc.Value(k))
	}
	vcancel()
	fmt.Println("value still visible after cancel:", vc.Value(ctxKey("role")))
	fmt.Println("String of value ctx:", context.WithValue(bg, ctxKey("k"), "v"))

	// AfterFunc: runs on cancel; stop prevents it.
	af, afCancel := context.WithCancel(bg)
	ran := make(chan string, 2)
	stop1 := context.AfterFunc(af, func() { ran <- "first" })
	stop2 := context.AfterFunc(af, func() { ran <- "second" })
	fmt.Println("stop2 before cancel:", stop2())
	afCancel()
	fmt.Println("AfterFunc ran:", <-ran)
	fmt.Println("stop1 after run:", stop1())
	select {
	case s := <-ran:
		fmt.Println("BUG: stopped func ran", s)
	default:
		fmt.Println("stopped AfterFunc did not run")
	}

	// WithoutCancel keeps values, drops cancellation.
	parent, pcancel := context.WithCancel(context.WithValue(bg, ctxKey("trace"), "t-1"))
	detached := context.WithoutCancel(parent)
	pcancel()
	describe("parent", parent)
	describe("detached", detached)
	fmt.Println("detached keeps value:", detached.Value(ctxKey("trace")))

	// Workers that stop on cancellation after a fixed number of steps.
	wctx, wcancel := context.WithCancel(bg)
	outs := make([]chan int, 4)
	steps := make([]chan struct{}, 4)
	for i := range outs {
		outs[i] = make(chan int, 1)
		steps[i] = make(chan struct{})
		go counter(wctx, steps[i], outs[i])
	}
	for i := range outs {
		for k := 0; k < (i+1)*5; k++ {
			steps[i] <- struct{}{}
			<-steps[i]
		}
	}
	wcancel()
	for i := range outs {
		fmt.Printf("worker %d stopped after %d steps\n", i, <-outs[i])
	}

	// Cancellation fan-out: many goroutines blocked on Done, all released.
	fctx, fcancel := context.WithCancelCause(bg)
	var wg sync.WaitGroup
	var mu sync.Mutex
	var seen []string
	for i := 0; i < 25; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			sub, c := context.WithCancel(fctx)
			defer c()
			<-sub.Done()
			mu.Lock()
			seen = append(seen, fmt.Sprintf("g%02d:%v", i, context.Cause(sub)))
			mu.Unlock()
		}()
	}
	fcancel(fmt.Errorf("broadcast %d", 25))
	wg.Wait()
	sort.Strings(seen)
	for _, s := range seen {
		fmt.Println(s)
	}

	// Done channel identity: the same channel each call; closed once.
	dctx, dcancel := context.WithCancel(bg)
	d1, d2 := dctx.Done(), dctx.Done()
	fmt.Println("same Done chan:", d1 == d2)
	dcancel()
	_, ok := <-d1
	fmt.Println("Done closed:", !ok)
	fmt.Println(dctx.Err() == context.Canceled, errors.Is(dctx.Err(), context.Canceled))
}
