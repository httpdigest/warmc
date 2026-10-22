// Errors.  errors.New values compare by identity, not text; fmt.Errorf with
// %w wraps (once or several times in one call), and errors.Unwrap, Is and As
// walk the chain -- including through errors.Join trees, custom Is and As
// methods, and value- against pointer-receiver error types.  Sentinel errors
// from the standard library (strconv.ErrSyntax/ErrRange, io.EOF), error
// values formatted with %v %s %q and %+v, a Stringer and a GoStringer, and
// the typed-nil pitfall where a nil *T returned as error is not nil.  A
// small validation pipeline returns, wraps and inspects errors the way real
// programs do.
package main

import (
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"strconv"
)

var ErrNotFound = errors.New("not found")
var ErrPermission = errors.New("permission denied")

type ValidationError struct {
	Field string
	Value int
}

func (e ValidationError) Error() string {
	return fmt.Sprintf("invalid %s: %d", e.Field, e.Value)
}

type TempError struct {
	Op    string
	Err   error
	Retry int
}

func (e *TempError) Error() string { return e.Op + ": " + e.Err.Error() }
func (e *TempError) Unwrap() error { return e.Err }

// Is lets any *TempError match a TempError with a zero Op as a wildcard.
func (e *TempError) Is(target error) bool {
	t, ok := target.(*TempError)
	return ok && t.Op == "" && t.Err == nil
}

type codeErr int

func (c codeErr) Error() string { return "code " + strconv.Itoa(int(c)) }
func (c codeErr) As(target any) bool {
	if p, ok := target.(**httpCode); ok {
		*p = &httpCode{int(c) * 100}
		return true
	}
	return false
}

// httpCode is what a codeErr converts itself to under errors.As.
type httpCode struct{ n int }

func (h *httpCode) Error() string { return "http " + strconv.Itoa(h.n) }

type point struct{ X, Y int }

func (p point) String() string   { return fmt.Sprintf("P(%d,%d)", p.X, p.Y) }
func (p point) GoString() string { return fmt.Sprintf("point{%d, %d}", p.X, p.Y) }

var limit = 100

func init() {
	if len(os.Args) > 1000 {
		limit = 1
	}
}

func lookup(key string) (int, error) {
	switch key {
	case "a":
		return 1, nil
	case "b":
		return 2, nil
	case "secret":
		return 0, ErrPermission
	}
	return 0, fmt.Errorf("lookup %q: %w", key, ErrNotFound)
}

func validate(v int) error {
	if v < 0 {
		return ValidationError{"value", v}
	}
	if v > limit {
		return &TempError{"validate", ValidationError{"range", v}, 3}
	}
	return nil
}

func process(inputs []string) error {
	var errs []error
	for _, in := range inputs {
		n, err := strconv.Atoi(in)
		if err != nil {
			errs = append(errs, fmt.Errorf("parse %s: %w", in, err))
			continue
		}
		if err := validate(n); err != nil {
			errs = append(errs, fmt.Errorf("item %d: %w", n, err))
		}
	}
	return errors.Join(errs...)
}

type maybeErr struct{}

func (*maybeErr) Error() string { return "maybe" }

//go:noinline
func typedNil(fail bool) error {
	var p *maybeErr
	if fail {
		p = &maybeErr{}
	}
	return p
}

func describe(err error) string {
	var ve ValidationError
	var te *TempError
	var ne *strconv.NumError
	var code *httpCode
	// The As calls are made before the targets are read: Go leaves the order
	// of a call and a plain variable read in one argument list unspecified.
	isVE := errors.As(err, &ve)
	isTE := errors.As(err, &te)
	isNE := errors.As(err, &ne)
	isCode := errors.As(err, &code)
	return fmt.Sprintf("nf=%v perm=%v valid=%v(%v) temp=%v(%v) num=%v(%v) code=%v(%d) syntax=%v range=%v eof=%v",
		errors.Is(err, ErrNotFound), errors.Is(err, ErrPermission),
		isVE, ve, isTE, te != nil && te.Retry == 3,
		isNE, ne != nil && ne.Func == "Atoi", isCode, codeNum(code),
		errors.Is(err, strconv.ErrSyntax), errors.Is(err, strconv.ErrRange),
		errors.Is(err, io.EOF))
}

func main() {
	fmt.Println("-- identity")
	e1 := errors.New("x")
	e2 := errors.New("x")
	fmt.Println(e1 == e2, e1 == e1, e1.Error() == e2.Error(), errors.Is(e1, e2))
	fmt.Println("-- lookups")
	for _, k := range []string{"a", "b", "secret", "zzz"} {
		v, err := lookup(k)
		fmt.Println(k, v, err, describe(err))
	}
	fmt.Println("-- wrapping chains")
	base := ValidationError{"age", -5}
	w1 := fmt.Errorf("layer1: %w", base)
	w2 := fmt.Errorf("layer2: %w", w1)
	w3 := &TempError{"op3", w2, 3}
	w4 := fmt.Errorf("layer4 [%v]: %w", 42, w3)
	chain := []error{base, w1, w2, w3, w4}
	for i, e := range chain {
		fmt.Println(i, e)
		fmt.Println("  ", describe(e))
		fmt.Println("   unwrap:", errors.Unwrap(e))
	}
	fmt.Println(errors.Is(w4, &TempError{}), errors.Is(w2, &TempError{}),
		errors.Is(w4, base), errors.Is(w4, ValidationError{"age", -6}))
	fmt.Println("-- multiple %w and Join")
	m := fmt.Errorf("both: %w and %w", ErrNotFound, codeErr(7))
	fmt.Println(m, errors.Unwrap(m) == nil, describe(m))
	j := errors.Join(ErrPermission, nil, w3, codeErr(2))
	fmt.Printf("%q\n", j.Error())
	fmt.Println(describe(j))
	fmt.Println(errors.Join(nil, nil) == nil)
	if u, ok := j.(interface{ Unwrap() []error }); ok {
		for i, e := range u.Unwrap() {
			fmt.Println("  joined", i, e)
		}
	}
	fmt.Println("-- pipeline")
	err := process([]string{"5", "x", "-3", "500", "99", "1e3", "99999999999999999999", "7"})
	fmt.Println(err)
	fmt.Println(describe(err))
	fmt.Println(process([]string{"1", "2"}) == nil)
	fmt.Println("-- standard errors")
	_, err = strconv.ParseInt("12x", 10, 64)
	fmt.Println(err, describe(err))
	_, err = strconv.ParseUint("999999999999999999999", 10, 64)
	fmt.Println(err, describe(err))
	_, err = strconv.ParseFloat("abc", 64)
	var ne *strconv.NumError
	if errors.As(err, &ne) {
		fmt.Println(ne.Func, ne.Num, ne.Err)
	}
	wrappedEOF := fmt.Errorf("read header: %w", io.EOF)
	fmt.Println(wrappedEOF, errors.Is(wrappedEOF, io.EOF), wrappedEOF == io.EOF,
		errors.Is(io.ErrUnexpectedEOF, io.EOF))
	pe := &fs.PathError{Op: "open", Path: "/x", Err: fs.ErrNotExist}
	fmt.Println(pe, errors.Is(pe, fs.ErrNotExist), errors.Is(pe, os.ErrNotExist))
	fmt.Println("-- formatting")
	ve := ValidationError{"f", 1}
	p := point{1, 2}
	fmt.Printf("%v|%s|%q|%+v|%#v\n", ve, ve, ve, ve, ve)
	fmt.Printf("%v|%s|%d|%+v|%#v\n", p, p, p, p, p)
	fmt.Printf("%v %v\n", []point{{1, 2}, {3, 4}}, map[string]point{"k": {5, 6}})
	fmt.Printf("%v|%s\n", w3, w4)
	var nilErr error
	fmt.Printf("%v %v\n", nilErr, nilErr == nil)
	fmt.Println("-- typed nil")
	t1, t2 := typedNil(false), typedNil(true)
	fmt.Println(t1 == nil, t2 == nil, t1 != nil)
	fmt.Println("-- retry loop")
	attempts := 0
	var last error
	for attempts < 10 {
		attempts++
		if attempts < 4 {
			last = &TempError{"try" + strconv.Itoa(attempts), ErrNotFound, attempts}
			var te *TempError
			if errors.As(last, &te) && errors.Is(last, ErrNotFound) {
				fmt.Println("retrying after", te.Op, te.Retry)
				continue
			}
		}
		last = nil
		break
	}
	fmt.Println(attempts, last)
}

func codeNum(h *httpCode) int {
	if h == nil {
		return -1
	}
	return h.n
}
