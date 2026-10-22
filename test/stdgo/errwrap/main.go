// errors.Is and errors.As through fmt.Errorf %w chains up to three deep and
// errors.Join, over a sentinel, a pointer error type, a value error type with
// its own Unwrap, and an As target that is an interface.
//
// Go errors are ordinary values, but inspecting one walks the chain at run
// time.  errors.is compares each link with runtime.ifaceeq, asks it for an Is
// method with runtime.typeAssert, and looks for Unwrap() error or
// Unwrap() []error with runtime.interfaceSwitch, recursing into a Join;
// errors.as checks assignability with internal/reflectlite and stores through
// it.  Building the chain is fmt.Errorf, which formats a message and allocates
// the wrapper.  So this is type assertions, itab lookups, allocation and
// formatting together, which is what an error path in real Go costs.
package main

import (
	"errors"
	"fmt"
	"io/fs"
)

var errNotFound = errors.New("not found")
var errTimeout = errors.New("timeout")

type codeErr struct {
	code int
	op   string
}

func (e *codeErr) Error() string { return fmt.Sprintf("%s: code %d", e.op, e.code) }

type tempErr struct{ err error }

func (e tempErr) Error() string   { return "temporary: " + e.err.Error() }
func (e tempErr) Unwrap() error   { return e.err }
func (e tempErr) Temporary() bool { return true }

//go:noinline
func fail(i int) error {
	var err error
	switch i % 5 {
	case 0:
		return nil
	case 1:
		err = errNotFound
	case 2:
		err = &codeErr{i & 1023, "read"}
	case 3:
		err = tempErr{errTimeout}
	default:
		err = errors.Join(&codeErr{i & 7, "write"}, fs.ErrPermission)
	}
	// Wrap a depth that varies with i.
	for d := 0; d < i%4; d++ {
		err = fmt.Errorf("layer %d: %w", d, err)
	}
	return err
}

func main() {
	var acc uint64
	for i := 0; i < 48000; i++ {
		err := fail(i)
		if err == nil {
			acc += 1
			continue
		}
		if errors.Is(err, errNotFound) {
			acc += 3
		}
		if errors.Is(err, errTimeout) {
			acc += 5
		}
		if errors.Is(err, fs.ErrPermission) {
			acc += 7
		}
		var ce *codeErr
		if errors.As(err, &ce) {
			acc += uint64(ce.code) + uint64(len(ce.op))
		}
		var t interface{ Temporary() bool }
		if errors.As(err, &t) && t.Temporary() {
			acc += 11
		}
		if i%64 == 0 {
			acc += uint64(len(err.Error()))
		}
		acc = acc * 31 % 1000000007
	}
	fmt.Println("acc =", acc)
}
