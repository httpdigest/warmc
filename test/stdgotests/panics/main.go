// defer, panic and recover in every arrangement the language allows: defer
// order and argument evaluation time, deferred closures rewriting named
// results, recover only working in the directly deferred function, re-panic
// with a new value and with the same value, a panic raised while another is
// unwinding (the second replaces the first), recover inside nested defers,
// panics carrying errors, structs and nil (runtime.PanicNilError), panics
// crossing many frames of recursion, defers in loops, panics recovered in a
// goroutine, and runtime.Goexit not being recoverable.
//
// On wasm the runtime cannot reset a hardware stack pointer, so recovery
// unwinds: each Go function returns a flag that says whether to keep
// unwinding, and the frame that recovered resumes through the same br_table
// that resumes a goroutine.  Every step of each scenario is logged.
package main

import (
	"errors"
	"fmt"
	"runtime"
	"strings"
	"sync"
)

var trace []string

func log(f string, a ...any) { trace = append(trace, fmt.Sprintf(f, a...)) }

func flush(title string) {
	fmt.Println("==", title)
	for _, t := range trace {
		fmt.Println("  ", t)
	}
	trace = trace[:0]
}

func deferOrder() {
	for i := 0; i < 5; i++ {
		defer log("defer loop %d", i)
	}
	x := 10
	defer log("defer arg evaluated early x=%d", x)
	defer func() { log("closure sees late x=%d", x) }()
	x = 20
	log("body done x=%d", x)
}

func namedResult() (n int, s string) {
	defer func() {
		n *= 2
		s += "!"
	}()
	n, s = 21, "hello"
	return n, s
}

func returnOverride() (err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("recovered: %v", r)
		}
	}()
	panic("boom")
}

func helperRecover() any { return recover() }

func indirectRecover() (out string) {
	defer func() {
		r := recover()
		out = fmt.Sprintf("outer recover got %v", r)
	}()
	defer func() {
		// recover called from a helper, not directly by the deferred func: nil.
		log("helper recover returned %v", helperRecover())
	}()
	panic("indirect")
}

func rePanic() (out string) {
	defer func() {
		r := recover()
		out = fmt.Sprintf("final recover: %v", r)
	}()
	defer func() {
		r := recover()
		log("first recover: %v", r)
		panic(fmt.Sprintf("re-panic(%v)", r))
	}()
	panic("original")
}

func panicDuringPanic() (out string) {
	defer func() {
		r := recover()
		out = fmt.Sprintf("recovered %v", r)
	}()
	defer func() {
		log("deferred raising a second panic")
		panic("second")
	}()
	panic("first")
}

func nestedDefers() (out []string) {
	defer func() {
		out = append(out, fmt.Sprintf("outermost recover=%v", recover()))
	}()
	defer func() {
		defer func() {
			out = append(out, fmt.Sprintf("inner-deferred recover=%v", recover()))
		}()
		out = append(out, "middle defer running, panicking inner")
		panic("inner")
	}()
	out = append(out, "body panicking outer")
	panic("outer")
}

type myErr struct{ code int }

func (e *myErr) Error() string { return fmt.Sprintf("myErr code %d", e.code) }

func classify(f func()) (s string) {
	defer func() {
		r := recover()
		switch v := r.(type) {
		case nil:
			s = "no panic"
		case runtime.Error:
			s = "runtime.Error: " + v.Error()
		case *myErr:
			s = fmt.Sprintf("*myErr %d", v.code)
		case error:
			s = "error: " + v.Error()
		case string:
			s = "string: " + v
		case fmt.Stringer:
			s = "stringer: " + v.String()
		default:
			s = fmt.Sprintf("other %T: %v", v, v)
		}
	}()
	f()
	return "returned normally"
}

type point struct{ X, Y int }

func (p point) String() string { return fmt.Sprintf("P(%d,%d)", p.X, p.Y) }

func deep(n int) int {
	defer func() {
		if n%10 == 0 {
			log("unwinding through frame %d", n)
		}
	}()
	if n == 0 {
		panic(fmt.Sprintf("bottom reached"))
	}
	return deep(n-1) + 1
}

func deepRecover(n int) (r any) {
	defer func() { r = recover() }()
	deep(n)
	return nil
}

func recoverAtLevel(level, target int) (s string) {
	if level == target {
		defer func() {
			if r := recover(); r != nil {
				s = fmt.Sprintf("caught at level %d: %v", level, r)
			}
		}()
	}
	if level == 0 {
		panic("from level 0")
	}
	return recoverAtLevel(level-1, target) + fmt.Sprintf("<-%d", level)
}

func loopWithRecover() []int {
	var out []int
	for i := 0; i < 8; i++ {
		func() {
			defer func() {
				if r := recover(); r != nil {
					out = append(out, -i)
				}
			}()
			if i%3 == 0 {
				panic(i)
			}
			out = append(out, i*i)
		}()
	}
	return out
}

func main() {
	deferOrder()
	flush("defer order")

	n, s := namedResult()
	fmt.Println("named result", n, s)
	fmt.Println("return override:", returnOverride())
	fmt.Println(indirectRecover())
	flush("indirect recover")
	fmt.Println(rePanic())
	flush("re-panic")
	fmt.Println(panicDuringPanic())
	flush("panic during panic")
	for _, l := range nestedDefers() {
		fmt.Println("nested:", l)
	}

	fmt.Println(classify(func() {}))
	fmt.Println(classify(func() { panic("plain string") }))
	fmt.Println(classify(func() { panic(&myErr{42}) }))
	fmt.Println(classify(func() { panic(errors.New("an error")) }))
	fmt.Println(classify(func() { panic(fmt.Errorf("wrapped: %w", &myErr{7})) }))
	fmt.Println(classify(func() { panic(point{3, 4}) }))
	fmt.Println(classify(func() { panic(12345) }))
	fmt.Println(classify(func() { panic(3.5) }))
	fmt.Println(classify(func() { panic([]int{1, 2}) }))
	fmt.Println(classify(func() { panic(nil) }))
	fmt.Println(classify(func() {
		var e error = &myErr{9}
		panic(e)
	}))

	// panic(nil) recovered as *runtime.PanicNilError.
	func() {
		defer func() {
			r := recover()
			_, isPN := r.(*runtime.PanicNilError)
			fmt.Printf("panic(nil): %T %v %v\n", r, r, isPN)
		}()
		panic(nil)
	}()

	fmt.Println("deep recover:", deepRecover(45))
	flush("deep unwinding")
	for t := 0; t <= 5; t++ {
		fmt.Println(recoverAtLevel(5, t))
	}
	fmt.Println("loop with recover", loopWithRecover())

	// Recover in a goroutine; main is unaffected.
	var wg sync.WaitGroup
	res := make([]string, 5)
	for i := 0; i < 5; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			defer func() {
				if r := recover(); r != nil {
					res[i] = fmt.Sprint("goroutine recovered: ", r)
				}
			}()
			if i%2 == 1 {
				panic(fmt.Sprintf("g%d failed", i))
			}
			res[i] = fmt.Sprint("goroutine ok ", i)
		}()
	}
	wg.Wait()
	for _, r := range res {
		fmt.Println(r)
	}

	// Goexit is not a panic: recover returns nil, defers still run.
	ch := make(chan string, 4)
	go func() {
		defer func() { ch <- "recover in Goexit defer: " + fmt.Sprint(recover()); close(ch) }()
		runtime.Goexit()
	}()
	for m := range ch {
		fmt.Println(m)
	}

	// Recovered value is the same value; re-panicking it and catching again.
	sentinel := &myErr{99}
	got := func() (r any) {
		defer func() { r = recover() }()
		func() {
			defer func() {
				v := recover()
				panic(v)
			}()
			panic(sentinel)
		}()
		return nil
	}()
	fmt.Println("same sentinel after re-panic:", got == sentinel)

	// Defer modifies a result after a recovered panic in a method value.
	type acc struct{ total int }
	a := &acc{}
	add := func(v int) (err error) {
		defer func() {
			if r := recover(); r != nil {
				err = fmt.Errorf("add(%d): %v", v, r)
			}
		}()
		if v < 0 {
			panic("negative")
		}
		a.total += v
		return nil
	}
	var errs []string
	for _, v := range []int{5, -1, 10, -2, 20} {
		if err := add(v); err != nil {
			errs = append(errs, err.Error())
		}
	}
	fmt.Println("acc total", a.total, "errors", strings.Join(errs, "; "))

	// Defer in a loop with closures capturing per-iteration variables.
	func() {
		for i := range 4 {
			defer func() { fmt.Println("deferred closure i =", i) }()
		}
	}()

	// recover() outside of panicking returns nil.
	fmt.Println("recover when not panicking:", recover())
}
