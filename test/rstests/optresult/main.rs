// Option and Result: every common combinator (map, and_then, or_else, filter,
// unwrap_or*, ok_or, zip, xor, take, replace, get_or_insert_with, as_ref,
// as_mut, copied, flatten, transpose, map_or, is_some_and, map_err, ok, err,
// and, or, unwrap_or_default, iter), the `?` operator on both, a custom error
// enum with Display and From conversions so `?` converts between error types,
// Box<dyn Error> with source chains, and an error-propagating parser.
//
// Option<T> and Result<T, E> are enums whose layout depends on T and E -- a
// null-pointer niche for Option<&T>, a separate tag for Option<u64>, the
// error's own discriminant for Result<(), Error> -- so the same combinator is
// compiled against several layouts.  Errors are always handled, never
// unwrapped on a failure, because panic=abort would trap.

use std::error::Error;
use std::fmt;
use std::hint::black_box as bb;
use std::num::ParseIntError;

#[derive(Debug, PartialEq)]
enum CalcError {
    Parse(String),
    DivZero,
    Overflow { op: char, a: i64, b: i64 },
    Empty,
}

impl fmt::Display for CalcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            CalcError::Parse(s) => write!(f, "cannot parse {:?}", s),
            CalcError::DivZero => write!(f, "division by zero"),
            CalcError::Overflow { op, a, b } => write!(f, "overflow in {} {} {}", a, op, b),
            CalcError::Empty => write!(f, "empty input"),
        }
    }
}

impl Error for CalcError {}

impl From<ParseIntError> for CalcError {
    fn from(e: ParseIntError) -> Self {
        CalcError::Parse(e.to_string())
    }
}

fn parse_num(s: &str) -> Result<i64, CalcError> {
    let n: i64 = s.trim().parse()?;
    Ok(n)
}

fn apply(op: char, a: i64, b: i64) -> Result<i64, CalcError> {
    let r = match op {
        '+' => a.checked_add(b),
        '-' => a.checked_sub(b),
        '*' => a.checked_mul(b),
        '/' => {
            if b == 0 {
                return Err(CalcError::DivZero);
            }
            a.checked_div(b)
        }
        _ => return Err(CalcError::Parse(op.to_string())),
    };
    r.ok_or(CalcError::Overflow { op, a, b })
}

// "3 + 4 * 2" evaluated left to right.
fn eval(expr: &str) -> Result<i64, CalcError> {
    let mut toks = expr.split_whitespace();
    let first = toks.next().ok_or(CalcError::Empty)?;
    let mut acc = parse_num(first)?;
    while let Some(op) = toks.next() {
        let rhs = toks.next().ok_or_else(|| CalcError::Parse(format!("missing operand after {}", op)))?;
        let c = op.chars().next().ok_or(CalcError::Empty)?;
        acc = apply(c, acc, parse_num(rhs)?)?;
    }
    Ok(acc)
}

#[derive(Debug)]
struct ConfigError {
    line: usize,
    cause: Box<dyn Error>,
}
impl fmt::Display for ConfigError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "config line {}", self.line)
    }
}
impl Error for ConfigError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        Some(self.cause.as_ref())
    }
}

fn parse_config(text: &str) -> Result<Vec<(String, i64)>, Box<dyn Error>> {
    let mut out = Vec::new();
    for (i, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (k, v) = line.split_once(':').ok_or_else(|| ConfigError { line: i + 1, cause: Box::new(CalcError::Empty) })?;
        let n = eval(v).map_err(|e| ConfigError { line: i + 1, cause: Box::new(e) })?;
        out.push((k.trim().to_string(), n));
    }
    if out.is_empty() {
        return Err("no entries".into());
    }
    Ok(out)
}

fn chain(e: &dyn Error) -> String {
    let mut s = e.to_string();
    let mut cur = e.source();
    while let Some(c) = cur {
        s.push_str(" <- ");
        s.push_str(&c.to_string());
        cur = c.source();
    }
    s
}

fn first_even_square(v: &[i32]) -> Option<i32> {
    let x = v.iter().find(|&&x| x % 2 == 0)?;
    x.checked_mul(*x)
}

fn lookup(names: &[&str], ages: &[Option<u32>], who: &str) -> Option<u32> {
    let i = names.iter().position(|&n| n == who)?;
    let age = (*ages.get(i)?)?;
    Some(age + 1)
}

fn options() {
    let a: Option<i32> = bb(Some(4));
    let n: Option<i32> = bb(None);
    println!("map {:?} {:?}", a.map(|x| x * 3), n.map(|x| x * 3));
    println!("and_then {:?} {:?} {:?}", a.and_then(|x| if x > 3 { Some(x - 3) } else { None }), a.and_then(|x| x.checked_sub(10).filter(|y| *y > 0)), n.and_then(Some));
    println!("or {:?} or_else {:?} xor {:?} {:?} {:?}", n.or(Some(9)), n.or_else(|| Some(-1)), a.xor(n), a.xor(Some(1)), n.xor(n));
    println!("filter {:?} {:?}", a.filter(|x| x % 2 == 0), a.filter(|x| x % 3 == 0));
    println!("unwrap_or {} {} else {} default {}", a.unwrap_or(0), n.unwrap_or(0), n.unwrap_or_else(|| 77), n.unwrap_or_default());
    println!("map_or {} {} map_or_else {}", a.map_or(-1, |x| x * x), n.map_or(-1, |x| x * x), n.map_or_else(|| 100, |x| x));
    println!("ok_or {:?} {:?}", a.ok_or("none"), n.ok_or("none"));
    println!("zip {:?} {:?} unzip {:?}", a.zip(Some('z')), a.zip(None::<char>), Some((1, "one")).unzip());
    println!("is_some_and {} {} is_none_or {}", a.is_some_and(|x| x > 3), n.is_some_and(|x| x > 3), n.is_none_or(|x| x > 100));
    println!("and {:?} {:?}", a.and(Some("b")), n.and(Some("b")));
    println!("flatten {:?} {:?}", Some(Some(5)).flatten(), Some(None::<i32>).flatten());
    let t: Option<Result<i32, String>> = Some(Ok(3));
    let t2: Option<Result<i32, String>> = Some(Err("bad".into()));
    println!("transpose {:?} {:?} {:?}", t.transpose(), t2.transpose(), None::<Result<i32, String>>.transpose());
    let mut slot: Option<String> = None;
    println!("get_or_insert_with {:?}", slot.get_or_insert_with(|| "fresh".into()));
    slot.as_mut().map(|s| s.push_str("+mut"));
    println!("as_mut {:?} as_deref {:?} len via as_ref {:?}", slot, slot.as_deref(), slot.as_ref().map(|s| s.len()));
    let old = slot.replace("new".into());
    println!("replace old {:?} now {:?}", old, slot);
    let taken = slot.take();
    println!("take {:?} left {:?}", taken, slot);
    let mut cnt = Some(0);
    if let Some(c) = cnt.as_mut() {
        *c += 5;
    }
    println!("as_mut add {:?}", cnt);
    let ins = *cnt.insert(42);
    println!("insert {} now {:?}", ins, cnt);
    let v = [Some(1), None, Some(3)];
    let refs: Vec<Option<&i32>> = v.iter().map(|o| o.as_ref()).collect();
    let copied: Vec<Option<i32>> = refs.iter().map(|o| o.copied()).collect();
    println!("as_ref copied {:?} {:?}", refs, copied);
    let total: i32 = v.iter().flatten().sum();
    let all: Option<Vec<i32>> = v.iter().cloned().collect();
    println!("sum flatten {} collect {:?} iter {:?}", total, all, a.iter().chain(n.iter()).collect::<Vec<_>>());
    println!("first_even_square {:?} {:?} {:?}", first_even_square(&[3, 5, 6, 8]), first_even_square(&[1, 3]), first_even_square(&[70000]));
    let names = ["ann", "bob", "cy"];
    let ages = [Some(30), None, Some(7)];
    for w in ["ann", "bob", "cy", "dee"] {
        println!("lookup {} {:?}", w, lookup(&names, &ages, w));
    }
    println!("cmp {} {} {:?}", Some(1) < Some(2), None < Some(i32::MIN), Some(3).max(None));
}

fn results() {
    let ok: Result<i32, String> = bb(Ok(10));
    let er: Result<i32, String> = bb(Err("boom".into()));
    println!("map {:?} {:?}", ok.clone().map(|x| x + 1), er.clone().map(|x| x + 1));
    println!("map_err {:?} {:?}", ok.clone().map_err(|e| e.len()), er.clone().map_err(|e| e.len()));
    println!("and_then {:?} {:?}", ok.clone().and_then(|x| if x > 5 { Err(format!("{} too big", x)) } else { Ok(x) }), er.clone().and_then(|x| Ok::<i32, String>(x * 2)));
    println!("or_else {:?} {:?}", ok.clone().or_else(|e| Err::<i32, usize>(e.len())), er.clone().or_else(|e| Ok::<i32, String>(e.len() as i32)));
    println!("ok {:?} {:?} err {:?} {:?}", ok.clone().ok(), er.clone().ok(), ok.clone().err(), er.clone().err());
    println!("unwrap_or {} {} else {} default {}", ok.clone().unwrap_or(0), er.clone().unwrap_or(0), er.clone().unwrap_or_else(|e| e.len() as i32 * 100), er.clone().unwrap_or_default());
    println!("map_or {} {} is_ok_and {} is_err_and {}", ok.clone().map_or(0, |x| x * x), er.clone().map_or(0, |x| x * x), ok.clone().is_ok_and(|x| x == 10), er.clone().is_err_and(|e| e.starts_with('b')));
    println!("and {:?} or {:?}", ok.clone().and(Ok::<&str, String>("next")), er.clone().or(Ok::<i32, ()>(1)));
    println!("as_ref {:?} iter {:?}", er.as_ref().map(|x| x + 1), ok.iter().collect::<Vec<_>>());
    let r: Result<Option<i32>, String> = Ok(Some(4));
    println!("transpose {:?}", r.transpose());
    println!("copied {:?} flatten-ish {:?}", Ok::<&i32, ()>(&5).copied(), Ok::<Result<i32, ()>, ()>(Ok(3)).and_then(|x| x));

    for e in ["1 + 2 * 3", "10 / 0", "9223372036854775807 + 1", "", "7 -", "4 x 5", "abc", "  100 - 250 / 5 ", "-9223372036854775808 / -1", "6 * 7"] {
        match eval(bb(e)) {
            Ok(v) => println!("eval {:?} = {}", e, v),
            Err(err) => println!("eval {:?} failed: {} / {:?}", e, err, err),
        }
    }
    let errs: Vec<CalcError> = ["x", "1 / 0", ""].iter().filter_map(|e| eval(e).err()).collect();
    println!("errors collected {:?}", errs);
    println!("eq {}", eval("1 / 0") == Err(CalcError::DivZero));

    let configs = [
        "# comment\nwidth: 80\nheight: 20 * 3\n\ndepth: 7 - 10\n",
        "a: 1\nb 2\n",
        "a: 1\nb: 5 / 0\n",
        "a: 9223372036854775807 * 2\n",
        "# only comments\n\n",
        "x: 1\ny: oops\n",
    ];
    for c in configs {
        match parse_config(c) {
            Ok(v) => println!("config ok {:?}", v),
            Err(e) => println!("config error: {}", chain(e.as_ref())),
        }
    }
    let int_errs = ["", "-", "+", "99999999999", "12x", "-129", "256", "٣"];
    for s in int_errs {
        let a = s.parse::<i32>();
        let b = s.parse::<u8>();
        let c = s.parse::<i8>();
        println!("parse {:?}: i32 {:?} u8 {:?} i8 {:?}", s, a.map_err(|e| e.kind().clone()), b.map_err(|e| e.to_string()), c.is_ok());
    }
    let sums: Result<i32, CalcError> = ["1", "2", "3"].iter().map(|s| parse_num(s).map(|x| x as i32)).sum();
    let sums2: Result<i32, CalcError> = ["1", "z", "3"].iter().map(|s| parse_num(s).map(|x| x as i32)).sum();
    println!("sum results {:?} {:?}", sums, sums2);
}

fn main() {
    options();
    results();
    println!(
        "sizes Option<&u8> {} Option<u64> {} Result<(), CalcError> {} Result<u8, ()> {} Option<Option<bool>> {}",
        std::mem::size_of::<Option<&u8>>(),
        std::mem::size_of::<Option<u64>>(),
        std::mem::size_of::<Result<(), CalcError>>(),
        std::mem::size_of::<Result<u8, ()>>(),
        std::mem::size_of::<Option<Option<bool>>>()
    );
}
