// time without the clock: time.Duration arithmetic, String formatting
// (including sub-microsecond and negative durations and the extremes),
// Hours/Minutes/Seconds/Milliseconds/Microseconds conversions, Round and
// Truncate at many units, Abs including math.MinInt64, and ParseDuration of
// fractional, multi-unit and invalid inputs; and fixed instants built with
// time.Date in UTC and in time.FixedZone offsets (never time.Now and never
// the zone database): normalisation of out-of-range fields, Add/Sub/AddDate
// across month ends and leap years, Before/After/Equal/Compare across zones,
// Weekday, YearDay, ISOWeek, Truncate/Round to units, Unix/UnixMilli
// round trips, Format with every layout element and the predefined layouts,
// and Parse/ParseInLocation of the same, with their error messages.
//
// Duration printing is integer arithmetic on int64 nanoseconds with
// fraction-trimming; calendar code is the days-since-epoch arithmetic in
// time's absolute-date conversion, full of divisions by 400/100/4.
package main

import (
	"fmt"
	"math"
	"time"
)

func main() {
	durs := []time.Duration{0, 1, 999, time.Microsecond, 1500 * time.Nanosecond, time.Millisecond + 1, time.Second,
		90 * time.Second, time.Hour + 2*time.Minute + 3*time.Second + 4*time.Millisecond, -time.Minute, 100 * time.Hour,
		time.Duration(math.MaxInt64), time.Duration(math.MinInt64), 1234567890123, -1}
	for _, d := range durs {
		fmt.Printf("%-24s ns=%d h=%.6f m=%.4f s=%.3f ms=%d us=%d abs=%v\n", d.String(), int64(d), d.Hours(), d.Minutes(), d.Seconds(), d.Milliseconds(), d.Microseconds(), d.Abs())
	}
	d := 3*time.Hour + 25*time.Minute + 45*time.Second + 678912345*time.Nanosecond
	for _, u := range []time.Duration{time.Nanosecond, time.Microsecond, time.Millisecond, time.Second, 2 * time.Second, time.Minute, 15 * time.Minute, time.Hour, 0, -1} {
		fmt.Printf("%v Round(%v)=%v Truncate=%v | neg Round=%v Truncate=%v\n", d, u, d.Round(u), d.Truncate(u), (-d).Round(u), (-d).Truncate(u))
	}
	fmt.Println("arith", d*2, d/3, d%time.Minute, d-4*time.Hour, time.Duration(2.5*float64(time.Second)), 10*time.Millisecond*time.Duration(7))
	maxD := durs[11]
	fmt.Println("overflow wraps", maxD+1, maxD*2, -durs[12])

	for _, s := range []string{"1h", "1h30m", "1.5h", "-2m3.5s", "300ms", "1us", "1µs", "1μs", "2ns", "0", "+5s", ".5s", "1h1h", "3.000000001s",
		"9223372036854775807ns", "9223372036854775808ns", "2562047h", "2562048h", "", "1", "1x", "h", "-", "1.s", "5m-3s"} {
		v, err := time.ParseDuration(s)
		fmt.Printf("ParseDuration(%q) = %v %v\n", s, v, err)
	}

	// Fixed instants.
	utc := time.Date(2024, time.February, 29, 23, 59, 59, 999999999, time.UTC)
	est := time.FixedZone("EST", -5*3600)
	ist := time.FixedZone("IST", 5*3600+1800)
	odd := time.FixedZone("", -(3*3600 + 25*60 + 7))
	fmt.Println("utc", utc, "| in EST", utc.In(est), "| in IST", utc.In(ist), "| odd offset", utc.In(odd))
	fmt.Println("next ns", utc.Add(1), "weekday", utc.Weekday(), "yearday", utc.YearDay())
	y, w := utc.ISOWeek()
	fmt.Println("ISOWeek", y, w, "Unix", utc.Unix(), "UnixMilli", utc.UnixMilli(), "UnixNano", utc.UnixNano())
	fmt.Println("from Unix", time.Unix(utc.Unix(), int64(utc.Nanosecond())).UTC().Equal(utc), time.UnixMilli(utc.UnixMilli()).UTC(), time.Unix(0, 0).UTC())

	// Normalisation.
	norm := []time.Time{
		time.Date(2023, 13, 1, 0, 0, 0, 0, time.UTC),
		time.Date(2023, 2, 30, 0, 0, 0, 0, time.UTC),
		time.Date(2024, 1, 0, 0, 0, 0, 0, time.UTC),
		time.Date(2024, 3, 1, -1, 0, 0, 0, time.UTC),
		time.Date(2024, 1, 1, 0, 0, 0, -1, time.UTC),
		time.Date(2000, 0, 0, 25, 61, 61, 1e9+5, time.UTC),
		time.Date(1, 1, 1, 0, 0, 0, 0, time.UTC),
		time.Date(-500, 6, 15, 12, 0, 0, 0, time.UTC),
		time.Date(9999, 12, 31, 23, 59, 59, 0, time.UTC),
		time.Date(1900, 3, 1, 0, 0, 0, 0, time.UTC),
	}
	for _, t := range norm {
		fmt.Printf("normalised %s weekday=%s yday=%d\n", t.Format(time.RFC3339Nano), t.Weekday(), t.YearDay())
	}

	// AddDate across month ends and leap years.
	start := time.Date(2023, 1, 31, 12, 0, 0, 0, time.UTC)
	for _, add := range [][3]int{{0, 1, 0}, {0, 2, 0}, {1, 1, 0}, {0, 0, 365}, {0, 0, -31}, {-1, 0, 0}, {0, 13, 0}, {0, -1, 0}, {4, 0, 29}} {
		fmt.Printf("AddDate%v -> %s\n", add, start.AddDate(add[0], add[1], add[2]).Format("2006-01-02 Mon"))
	}
	leap := time.Date(2024, 2, 29, 0, 0, 0, 0, time.UTC)
	fmt.Println("leap +1y", leap.AddDate(1, 0, 0).Format(time.DateOnly), "+4y", leap.AddDate(4, 0, 0).Format(time.DateOnly), "-100y", leap.AddDate(-100, 0, 0).Format(time.DateOnly))
	for _, yr := range []int{1600, 1700, 1900, 2000, 2023, 2024, 2100, 2400} {
		dec31 := time.Date(yr, 12, 31, 0, 0, 0, 0, time.UTC)
		fmt.Printf("year %d days=%d jan1=%s\n", yr, dec31.YearDay(), time.Date(yr, 1, 1, 0, 0, 0, 0, time.UTC).Weekday())
	}

	// Sub, comparisons across zones.
	a := time.Date(2024, 3, 10, 1, 30, 0, 0, est)
	b := time.Date(2024, 3, 10, 6, 30, 0, 0, time.UTC)
	c := time.Date(2024, 3, 10, 12, 0, 0, 0, ist)
	fmt.Println("Equal across zones", a.Equal(b), a == b, "Compare", a.Compare(c), c.Compare(a), b.Compare(a), "Before", a.Before(c), "After", a.After(c))
	fmt.Println("Sub", c.Sub(a), a.Sub(c), b.Sub(a), "Since-free diff in days", c.Sub(time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC)).Hours()/24)
	big1 := time.Date(1, 1, 1, 0, 0, 0, 0, time.UTC)
	big2 := time.Date(9999, 1, 1, 0, 0, 0, 0, time.UTC)
	fmt.Println("Sub saturates", big2.Sub(big1), big1.Sub(big2))
	fmt.Println("zero time", time.Time{}, time.Time{}.IsZero(), time.Time{}.Unix())

	// Truncate / Round instants.
	t0 := time.Date(2024, 7, 4, 15, 47, 31, 512345678, time.UTC)
	for _, u := range []time.Duration{time.Millisecond, time.Second, time.Minute, 15 * time.Minute, time.Hour, 24 * time.Hour} {
		fmt.Printf("instant Truncate(%v)=%s Round=%s\n", u, t0.Truncate(u).Format(time.RFC3339Nano), t0.Round(u).Format(time.RFC3339Nano))
	}

	// Formatting with every element.
	t1 := time.Date(2009, time.November, 10, 23, 4, 5, 123456789, time.FixedZone("CET", 3600))
	layouts := []string{
		time.Layout, time.ANSIC, time.UnixDate, time.RubyDate, time.RFC822, time.RFC822Z, time.RFC850, time.RFC1123, time.RFC1123Z,
		time.RFC3339, time.RFC3339Nano, time.Kitchen, time.Stamp, time.StampMilli, time.StampMicro, time.StampNano, time.DateTime, time.DateOnly, time.TimeOnly,
		"2006-01-02T15:04:05.000Z07:00", "Mon Monday Jan January 1 01 _2 2 02 __2 002 06 2006", "3 03 15 4 04 5 05 PM pm .0 .00 .999 .999999 ,000 ,999999999",
		"-07 -0700 -07:00 -070000 Z07 Z0700 Z07:00 MST", "literal text 2006 with no verbs? yes",
	}
	for _, l := range layouts {
		fmt.Printf("%-40q %s | utc: %s\n", l, t1.Format(l), t1.UTC().Format(l))
	}
	fmt.Println("AppendFormat", string(t1.AppendFormat([]byte("ts="), time.RFC3339)), "GoString", t1.GoString())
	mb, _ := t1.MarshalText()
	js, _ := t1.MarshalJSON()
	fmt.Println("MarshalText", string(mb), "MarshalJSON", string(js))

	// Parsing.
	parses := [][2]string{
		{time.RFC3339, "2024-02-29T12:30:45Z"},
		{time.RFC3339, "2024-02-29T12:30:45.123456+05:30"},
		{time.RFC3339Nano, "2024-02-29T12:30:45.000000001-08:00"},
		{time.RFC1123Z, "Thu, 29 Feb 2024 12:30:45 +0100"},
		{time.DateTime, "2023-12-31 23:59:60"},
		{time.DateOnly, "2023-02-29"},
		{time.DateOnly, "2024-02-29"},
		{time.Kitchen, "3:04PM"},
		{"02/01/2006 15h04", "25/12/2025 08h15"},
		{"Jan _2 2006", "Feb  3 2021"},
		{"2006-002", "2024-060"},
		{time.RFC3339, "2024-13-01T00:00:00Z"},
		{time.RFC3339, "not a time"},
		{"2006-01-02", "2024-1-5"},
		{"15:04:05.000", "10:11:12.345"},
		{time.ANSIC, "Mon Jan  2 15:04:05 2006 extra"},
	}
	for _, p := range parses {
		t, err := time.Parse(p[0], p[1])
		if err != nil {
			fmt.Printf("Parse(%q, %q): %v\n", p[0], p[1], err)
			continue
		}
		_, off := t.Zone()
		fmt.Printf("Parse(%q) = %s offset=%d unix=%d\n", p[1], t.Format(time.RFC3339Nano), off, t.Unix())
	}
	tl, err := time.ParseInLocation(time.DateTime, "2024-06-01 08:00:00", ist)
	fmt.Println("ParseInLocation", tl, err, tl.UTC())
	var ut time.Time
	err = ut.UnmarshalJSON([]byte(`"2001-02-03T04:05:06.7+08:00"`))
	fmt.Println("UnmarshalJSON", ut.UTC(), err)

	// Months and weekdays as strings and arithmetic.
	for m := time.January; m <= time.December; m++ {
		first := time.Date(2025, m, 1, 0, 0, 0, 0, time.UTC)
		last := first.AddDate(0, 1, -1)
		fmt.Printf("%-9s %d days, starts %s, ends %s\n", m, last.Day(), first.Weekday(), last.Weekday())
	}
	fmt.Println("bad month/weekday", time.Month(13), time.Weekday(7))

	// A schedule: every 7h45m from a fixed start, printed in two zones.
	sched := time.Date(2024, 12, 31, 18, 0, 0, 0, time.UTC)
	for i := 0; i < 8; i++ {
		fmt.Printf("slot %d: %s | %s\n", i, sched.Format("Mon 2006-01-02 15:04 MST"), sched.In(est).Format("Mon Jan 2 3:04PM MST"))
		sched = sched.Add(7*time.Hour + 45*time.Minute)
	}
}
