// log and log/slog writing into buffers with every clock-derived field
// removed: log.Logger prefixes, Lmsgprefix, Lshortfile (file:line of the
// call site), SetOutput/SetFlags/SetPrefix, Output with call depth, Panicf
// recovered; slog with TextHandler and JSONHandler, level filtering and a
// LevelVar changed at run time, With/WithGroup nesting, LogAttrs with every
// Attr kind (string, int64, uint64, float64, bool, duration, time, any,
// group), LogValuer resolution (including a recursive one), ReplaceAttr
// rewriting keys and dropping the time, AddSource with the path reduced to
// a base name, quoting rules for awkward strings, and a custom Handler that
// collects records for inspection.
//
// slog's handlers build each line with an append-based encoder and a
// sync.Pool of buffers; its Record keeps the first five attributes inline and
// spills the rest, so records with many attributes are logged too.
package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"log"
	"log/slog"
	"path/filepath"
	"strings"
	"time"
)

type user struct {
	ID   int
	Name string
	pass string
}

func (u user) LogValue() slog.Value {
	return slog.GroupValue(slog.Int("id", u.ID), slog.String("name", u.Name), slog.String("pass", "REDACTED"))
}

type lazy struct{ n int }

func (l lazy) LogValue() slog.Value {
	if l.n == 0 {
		return slog.StringValue("bottom")
	}
	return slog.AnyValue(lazy{l.n - 1})
}

type collector struct {
	attrs  []slog.Attr
	groups []string
	recs   *[]string
}

func (c *collector) Enabled(_ context.Context, l slog.Level) bool { return l >= slog.LevelInfo }
func (c *collector) Handle(_ context.Context, r slog.Record) error {
	var parts []string
	for _, a := range c.attrs {
		parts = append(parts, a.String())
	}
	r.Attrs(func(a slog.Attr) bool {
		parts = append(parts, a.Key+"="+a.Value.Resolve().String())
		return true
	})
	*c.recs = append(*c.recs, fmt.Sprintf("[%s] %s groups=%v attrs=%d {%s}", r.Level, r.Message, c.groups, r.NumAttrs(), strings.Join(parts, ", ")))
	return nil
}
func (c *collector) WithAttrs(as []slog.Attr) slog.Handler {
	n := *c
	n.attrs = append(append([]slog.Attr{}, c.attrs...), as...)
	return &n
}
func (c *collector) WithGroup(name string) slog.Handler {
	n := *c
	n.groups = append(append([]string{}, c.groups...), name)
	return &n
}

func noTime(groups []string, a slog.Attr) slog.Attr {
	switch a.Key {
	case slog.TimeKey:
		if len(groups) == 0 {
			return slog.Attr{}
		}
	case slog.SourceKey:
		if src, ok := a.Value.Any().(*slog.Source); ok {
			return slog.String("src", fmt.Sprintf("%s:%d", filepath.Base(src.File), src.Line))
		}
	case "password":
		return slog.String("password", "***")
	}
	return a
}

func main() {
	// Standard log.
	var buf bytes.Buffer
	l := log.New(&buf, "app: ", 0)
	l.Println("plain message", 42)
	l.Printf("formatted %05.2f|%q", 3.14159, "q")
	l.SetFlags(log.Lmsgprefix)
	l.Print("prefix moved after flags")
	l.SetFlags(log.Lshortfile)
	l.SetPrefix("[short] ")
	l.Println("with file and line")
	l.Output(1, "via Output depth 1")
	helper := func() { l.Output(2, "attributed to caller") }
	helper()
	l.SetFlags(0)
	l.Print("no newline added twice\n")
	l.Print("")
	func() {
		defer func() { l.Printf("recovered panic value: %v", recover()) }()
		l.Panicf("panic with %d", 7)
	}()
	fmt.Print(buf.String())
	fmt.Println("prefix", l.Prefix(), "flags", l.Flags(), "writer is buf", l.Writer() == &buf)

	// slog TextHandler.
	buf.Reset()
	lv := new(slog.LevelVar)
	lv.Set(slog.LevelDebug)
	th := slog.NewTextHandler(&buf, &slog.HandlerOptions{Level: lv, ReplaceAttr: noTime, AddSource: true})
	lg := slog.New(th)
	lg.Debug("debug msg", "k", 1)
	lg.Info("hello", "user", "alice", "count", 3, "ratio", 0.25, "ok", true)
	lg.Warn("quoting", "empty", "", "space", "a b", "quote", `say "hi"`, "eq", "a=b", "nl", "line1\nline2", "unicode", "héllo", "ctl", "\x01")
	lg.Error("failure", "err", errors.New("disk full"), "dur", 1500*time.Millisecond, "when", time.Date(2024, 1, 2, 3, 4, 5, 6000, time.UTC))
	lv.Set(slog.LevelWarn)
	lg.Info("filtered out")
	lg.Warn("still shown", "password", "hunter2")
	lv.Set(slog.LevelInfo)
	lg.With("svc", "api", "ver", 2).WithGroup("req").Info("request", "method", "GET", "path", "/x", slog.Group("hdr", "ua", "go", "len", 10))
	lg.LogAttrs(context.Background(), slog.LevelInfo, "attrs",
		slog.String("s", "str"), slog.Int64("i", -9), slog.Uint64("u", 1<<63), slog.Float64("f", 1e-7),
		slog.Bool("b", false), slog.Duration("d", 90*time.Second), slog.Time("t", time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC)),
		slog.Any("slice", []int{1, 2}), slog.Any("map", map[string]int{"b": 2, "a": 1}), slog.Any("nil", nil),
		slog.Group("g", slog.Int("x", 1), slog.Group("inner", slog.Int("y", 2))), slog.Group("emptygroup"))
	lg.Info("valuer", "u", user{7, "bob", "secret"}, "lazy", lazy{3})
	lg.Info("odd args", "dangling")
	lg.Info("non-string key", 42, "v")
	lg.Log(context.Background(), slog.Level(2), "custom level 2")
	lg.Log(context.Background(), slog.Level(-8), "below debug")
	lg.Log(context.Background(), slog.LevelError+4, "above error")
	fmt.Print(buf.String())

	// slog JSONHandler.
	buf.Reset()
	jh := slog.NewJSONHandler(&buf, &slog.HandlerOptions{ReplaceAttr: noTime})
	jl := slog.New(jh).With("app", "demo")
	jl.Info("json line", "n", 1, "f", 2.5, "s", "<tag> & \"q\"", "b", true, "nil", nil)
	jl.WithGroup("outer").With("a", 1).WithGroup("inner").Warn("nested", "b", 2, slog.Group("g", "c", 3))
	jl.Error("err value", "err", fmt.Errorf("wrapped: %w", errors.New("root")), "dur", time.Second, "u", user{1, "x", "y"})
	huge := 1e308
	jl.Info("special floats", "inf", huge*10, "nan", huge*10-huge*10, "big", uint64(1<<64-1), "bytes", []byte("hi"))
	fmt.Print(buf.String())

	// Custom handler.
	var recs []string
	cl := slog.New(&collector{recs: &recs})
	cl.Debug("dropped")
	cl.Info("one", "a", 1)
	cl.With("base", true).WithGroup("grp").Warn("two", "b", 2, "c", 3, "d", 4, "e", 5, "f", 6, "g", 7)
	cl.Error("three", "lazy", lazy{2})
	for _, r := range recs {
		fmt.Println("collected", r)
	}

	// Levels and attrs as values.
	for _, lvl := range []slog.Level{slog.LevelDebug - 1, slog.LevelDebug, slog.LevelInfo + 1, slog.LevelWarn, slog.LevelError, slog.LevelError + 2} {
		var parsed slog.Level
		err := parsed.UnmarshalText([]byte(lvl.String()))
		fmt.Printf("level %d %s parsed=%d err=%v\n", int(lvl), lvl, int(parsed), err)
	}
	a := slog.Group("req", slog.String("id", "r1"), slog.Int("n", 5))
	fmt.Println("attr string", a.String(), "kind", a.Value.Kind(), "equal", a.Equal(slog.Group("req", slog.String("id", "r1"), slog.Int("n", 5))))
	v := slog.AnyValue(int8(-3))
	fmt.Println("AnyValue kinds", v.Kind(), slog.AnyValue(uint16(9)).Kind(), slog.AnyValue(float32(1.5)).Kind(), slog.AnyValue(time.Minute).Kind(), slog.AnyValue("s").Kind(), slog.AnyValue(struct{}{}).Kind())

	// Many records through one handler.
	buf.Reset()
	many := slog.New(slog.NewTextHandler(&buf, &slog.HandlerOptions{ReplaceAttr: noTime}))
	for i := 0; i < 50; i++ {
		many.Info("tick", "i", i, "sq", i*i, "even", i%2 == 0)
	}
	lines := strings.Split(strings.TrimSpace(buf.String()), "\n")
	fmt.Println("many lines", len(lines), "first", lines[0], "last", lines[len(lines)-1])
	fmt.Println("total bytes", buf.Len())
	fmt.Println("DiscardHandler enabled:", slog.DiscardHandler.Enabled(context.Background(), slog.LevelError))
}
