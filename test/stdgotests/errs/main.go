// Errors as values: errors.New identity, fmt.Errorf with %w (one and
// several), errors.Unwrap, errors.Is and errors.As walking long wrapping
// chains, trees built by errors.Join and multi-%w, custom Is and As methods,
// Unwrap() []error, sentinel errors from the standard library (io.EOF,
// fs.ErrNotExist, strconv.ErrRange, context.Canceled), errors.ErrUnsupported,
// error types with pointer and value receivers, and an error-returning
// pipeline that stops at the first failure.
//
// errors.Is/As are recursive walks with type assertions at every node and a
// reflect-based target check in As; this prints the result of every query
// so a wrong branch or a missed node in the walk is visible.
package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"strconv"
	"strings"
)

var (
	ErrNotFound   = errors.New("not found")
	ErrPermission = errors.New("permission denied")
	ErrTimeout    = errors.New("timeout")
)

type QueryError struct {
	Query string
	Err   error
}

func (e *QueryError) Error() string { return "query " + strconv.Quote(e.Query) + ": " + e.Err.Error() }
func (e *QueryError) Unwrap() error { return e.Err }

type CodeError struct{ Code int }

func (e CodeError) Error() string { return fmt.Sprintf("code %d", e.Code) }

// Is matches any CodeError with the same class (hundreds digit).
func (e CodeError) Is(target error) bool {
	t, ok := target.(CodeError)
	return ok && t.Code/100 == e.Code/100
}

type Temporary interface{ Temporary() bool }

type netErr struct {
	msg  string
	temp bool
}

func (e *netErr) Error() string   { return "net: " + e.msg }
func (e *netErr) Temporary() bool { return e.temp }

type multi struct{ errs []error }

func (m multi) Error() string {
	var s []string
	for _, e := range m.errs {
		s = append(s, e.Error())
	}
	return "multi(" + strings.Join(s, ", ") + ")"
}
func (m multi) Unwrap() []error { return m.errs }

// As converts to a *QueryError with a synthetic query.
type asConverter struct{}

func (asConverter) Error() string { return "converter" }
func (asConverter) As(target any) bool {
	if qe, ok := target.(**QueryError); ok {
		*qe = &QueryError{"synthesised", ErrTimeout}
		return true
	}
	return false
}

func chain(depth int, root error) error {
	err := root
	for i := 0; i < depth; i++ {
		switch i % 3 {
		case 0:
			err = fmt.Errorf("layer %d: %w", i, err)
		case 1:
			err = &QueryError{fmt.Sprintf("q%d", i), err}
		case 2:
			err = fmt.Errorf("ctx%d (%w)", i, err)
		}
	}
	return err
}

func report(label string, err error) {
	var qe *QueryError
	var ce CodeError
	var ne *netErr
	var tmp Temporary
	var pe *fs.PathError
	isQ := errors.As(err, &qe)
	isC := errors.As(err, &ce)
	isN := errors.As(err, &ne)
	isT := errors.As(err, &tmp)
	isP := errors.As(err, &pe)
	fmt.Printf("%s\n  msg: %v\n  Is: notfound=%v perm=%v timeout=%v code404=%v code500=%v EOF=%v\n",
		label, err, errors.Is(err, ErrNotFound), errors.Is(err, ErrPermission), errors.Is(err, ErrTimeout),
		errors.Is(err, CodeError{404}), errors.Is(err, CodeError{500}), errors.Is(err, io.EOF))
	fmt.Printf("  As: query=%v", isQ)
	if isQ {
		fmt.Printf("(%s)", qe.Query)
	}
	fmt.Printf(" code=%v", isC)
	if isC {
		fmt.Printf("(%d)", ce.Code)
	}
	fmt.Printf(" net=%v temporary=%v", isN, isT)
	if isT {
		fmt.Printf("(%v)", tmp.Temporary())
	}
	fmt.Printf(" path=%v\n", isP)
}

func step(name string, fail bool) func(int) (int, error) {
	return func(x int) (int, error) {
		if fail {
			return 0, fmt.Errorf("step %s failed on %d: %w", name, x, ErrNotFound)
		}
		return x*2 + 1, nil
	}
}

func run(steps ...func(int) (int, error)) (int, error) {
	x := 1
	for i, s := range steps {
		var err error
		x, err = s(x)
		if err != nil {
			return x, fmt.Errorf("pipeline stopped at %d: %w", i, err)
		}
	}
	return x, nil
}

func main() {
	e1 := errors.New("x")
	e2 := errors.New("x")
	fmt.Println("distinct errors.New:", e1 == e2, errors.Is(e1, e2), e1.Error() == e2.Error())

	report("plain sentinel", ErrNotFound)
	report("wrapped once", fmt.Errorf("lookup: %w", ErrNotFound))
	report("%v does not wrap", fmt.Errorf("lookup: %v", ErrNotFound))
	report("deep chain", chain(10, ErrPermission))
	report("custom Is (404 vs 4xx)", chain(4, CodeError{418}))
	report("net temporary", fmt.Errorf("dial: %w", &netErr{"reset", true}))
	report("join", errors.Join(ErrNotFound, nil, &QueryError{"sel", CodeError{503}}, fmt.Errorf("io: %w", io.EOF)))
	report("multi %w", fmt.Errorf("both %w and %w", ErrTimeout, &netErr{"down", false}))
	report("custom Unwrap []error", multi{[]error{chain(3, ErrPermission), CodeError{200}}})
	report("As method", fmt.Errorf("wrapped converter: %w", asConverter{}))
	report("nested joins", errors.Join(errors.Join(ErrTimeout, CodeError{404}), errors.Join(nil, nil), multi{[]error{&netErr{"x", false}}}))

	// Unwrap walk.
	err := chain(7, ErrTimeout)
	for i := 0; err != nil; i++ {
		fmt.Printf("unwrap %d: %T %v\n", i, err, err)
		err = errors.Unwrap(err)
	}
	fmt.Println("Unwrap of join is nil:", errors.Unwrap(errors.Join(ErrTimeout)) == nil)
	j := errors.Join(ErrNotFound, ErrTimeout)
	if u, ok := j.(interface{ Unwrap() []error }); ok {
		fmt.Println("join Unwrap() []error len", len(u.Unwrap()))
	}
	fmt.Printf("join message %q\n", errors.Join(errors.New("a"), errors.New("b\nc")).Error())
	fmt.Println("Join(nil, nil) == nil:", errors.Join(nil, nil) == nil)

	// Standard library sentinels.
	_, err = strconv.Atoi("99999999999999999999")
	fmt.Println("strconv range:", errors.Is(err, strconv.ErrRange), err)
	_, err = os.Open("/definitely/not/here")
	var pe *fs.PathError
	fmt.Println("open missing: IsNotExist", errors.Is(err, fs.ErrNotExist), "PathError", errors.As(err, &pe))
	if pe != nil {
		fmt.Println("  op", pe.Op, "path", pe.Path)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	fmt.Println("context:", errors.Is(fmt.Errorf("wrapped: %w", ctx.Err()), context.Canceled))
	r := strings.NewReader("ab")
	buf := make([]byte, 4)
	_, err = io.ReadFull(r, buf)
	fmt.Println("ReadFull short:", err, errors.Is(err, io.ErrUnexpectedEOF), errors.Is(err, io.EOF))
	fmt.Println("ErrUnsupported:", errors.Is(fmt.Errorf("op: %w", errors.ErrUnsupported), errors.ErrUnsupported))

	// Pipelines.
	for _, failAt := range []int{-1, 0, 2, 4} {
		var steps []func(int) (int, error)
		for i := 0; i < 5; i++ {
			steps = append(steps, step(fmt.Sprint("s", i), i == failAt))
		}
		v, err := run(steps...)
		fmt.Printf("pipeline failAt=%d -> %d, err=%v, notfound=%v\n", failAt, v, err, errors.Is(err, ErrNotFound))
	}

	// Comparisons and As with value vs pointer targets.
	var target CodeError
	fmt.Println("As value target:", errors.As(fmt.Errorf("w: %w", CodeError{301}), &target), target.Code)
	func() {
		defer func() { fmt.Println("As with non-pointer target panics:", recover()) }()
		errors.As(ErrNotFound, target)
	}()
	func() {
		defer func() { fmt.Println("As with nil target panics:", recover()) }()
		errors.As(ErrNotFound, nil)
	}()
	fmt.Println("Is(nil, nil):", errors.Is(nil, nil), "Is(err, nil):", errors.Is(ErrNotFound, nil))

	// A long chain is still walkable.
	long := chain(300, CodeError{404})
	var q *QueryError
	errors.As(long, &q)
	fmt.Println("long chain: Is 4xx", errors.Is(long, CodeError{400}), "outermost query", q.Query, "len(msg)", len(long.Error()))
}
