// Closures: Fn, FnMut and FnOnce taken generically and as trait objects;
// captures by reference, by mutable reference and by move; closures returned
// from functions as `impl Fn` and as `Box<dyn Fn>`; closures stored in structs
// and vectors; composition, currying and memoisation; recursion through a
// boxed closure; and function pointers mixed with non-capturing closures.
//
// A capturing closure is a struct of its captures plus a function taking that
// struct by pointer; a non-capturing one coerces to a plain `fn` and becomes a
// table entry for `call_indirect`.  A boxed closure is a fat pointer whose
// vtable holds the call and drop entries.  Each of those shapes reaches the
// back end differently, so the same arithmetic is run through all of them.

use std::cell::RefCell;
use std::collections::BTreeMap;
use std::hint::black_box as bb;
use std::rc::Rc;

fn apply<F: Fn(i32) -> i32>(f: F, x: i32) -> i32 {
    f(x)
}
fn apply_twice<F>(f: F, x: i32) -> i32
where
    F: Fn(i32) -> i32,
{
    f(f(x))
}
fn call_mut_n<F: FnMut() -> u32>(mut f: F, n: usize) -> Vec<u32> {
    (0..n).map(|_| f()).collect()
}
fn consume<F: FnOnce() -> String>(f: F) -> String {
    f()
}
fn apply_dyn(f: &dyn Fn(i64) -> i64, xs: &[i64]) -> Vec<i64> {
    xs.iter().map(|&x| f(x)).collect()
}
fn apply_ptr(f: fn(i64) -> i64, xs: &[i64]) -> Vec<i64> {
    xs.iter().map(|&x| f(x)).collect()
}

fn make_adder(n: i32) -> impl Fn(i32) -> i32 {
    move |x| x + n
}
fn make_counter() -> impl FnMut() -> u32 {
    let mut c = 0;
    move || {
        c += 1;
        c * c
    }
}
fn make_op(name: &str) -> Box<dyn Fn(f64, f64) -> f64> {
    match name {
        "add" => Box::new(|a, b| a + b),
        "hyp" => Box::new(|a: f64, b: f64| (a * a + b * b).sqrt()),
        "pow" => Box::new(f64::powf),
        _ => {
            let n = name.len() as f64;
            Box::new(move |a, b| a * n - b)
        }
    }
}
fn compose<A, B, C>(f: impl Fn(A) -> B, g: impl Fn(B) -> C) -> impl Fn(A) -> C {
    move |x| g(f(x))
}
fn curry(a: i64) -> impl Fn(i64) -> Box<dyn Fn(i64) -> i64> {
    move |b| Box::new(move |c| a * 100 + b * 10 + c)
}
fn double(x: i64) -> i64 {
    x * 2
}

struct Button {
    label: String,
    on_click: Box<dyn FnMut(&str) -> usize>,
}

struct Memo<F: Fn(u64) -> u64> {
    f: F,
    cache: BTreeMap<u64, u64>,
    misses: u32,
}
impl<F: Fn(u64) -> u64> Memo<F> {
    fn get(&mut self, k: u64) -> u64 {
        if let Some(&v) = self.cache.get(&k) {
            return v;
        }
        self.misses += 1;
        let v = (self.f)(k);
        self.cache.insert(k, v);
        v
    }
}

fn sort_with<T: Clone, K: Ord>(xs: &[T], key: impl Fn(&T) -> K) -> Vec<T> {
    let mut v = xs.to_vec();
    v.sort_by_key(|x| key(x));
    v
}

fn main() {
    let k = bb(7);
    let add_k = |x| x + k;
    let mul_k = move |x: i32| x * k;
    println!("apply {} {} twice {} {}", apply(add_k, 1), apply(mul_k, 3), apply_twice(add_k, 1), apply_twice(mul_k, 3));
    println!("apply fn item {}", apply(i32::wrapping_neg, 5));
    println!("apply nested {}", apply(|x| apply(|y| y * y, x) + 1, 9));

    let mut log = Vec::new();
    let mut push = |s: &str| log.push(s.to_uppercase());
    push("a");
    push("bee");
    push("sea");
    println!("FnMut captured log {:?}", log);

    let mut n = 0u32;
    let got = call_mut_n(
        || {
            n += 3;
            n * 2
        },
        6,
    );
    println!("call_mut_n {:?} n after {}", got, n);
    let mut counter = make_counter();
    let c1: Vec<u32> = (0..5).map(|_| counter()).collect();
    println!("make_counter {:?} next {}", c1, counter());

    let name = String::from("owned string");
    let once = move || {
        let mut s = name;
        s.push_str(" consumed");
        s
    };
    println!("FnOnce {}", consume(once));
    let v = vec![1, 2, 3];
    let moved_vec = move || v.into_iter().map(|x| x.to_string()).collect::<Vec<_>>().join("+");
    println!("FnOnce vec {}", consume(moved_vec));

    let adders: Vec<Box<dyn Fn(i32) -> i32>> = (1..=4).map(|i| Box::new(make_adder(i * 10)) as Box<dyn Fn(i32) -> i32>).collect();
    println!("adders {:?}", adders.iter().map(|f| f(5)).collect::<Vec<_>>());
    for name in ["add", "hyp", "pow", "other"] {
        let f = make_op(name);
        println!("op {} -> {} {} {}", name, f(3.0, 4.0), f(1.5, 2.0), f(-2.0, 3.0));
    }

    let inc_then_sq = compose(|x: i32| x + 1, |x: i32| (x * x) as i64);
    let to_str = compose(inc_then_sq, |x: i64| format!("<{}>", x));
    println!("compose {} {} {}", to_str(1), to_str(-5), to_str(99));
    let curried = curry(4);
    let c2 = curried(5);
    println!("curry {} {}", c2(6), curry(1)(2)(3));

    let xs = [1i64, -2, 30, 400];
    let offset = bb(1000i64);
    println!("dyn {:?}", apply_dyn(&|x| x * x + offset, &xs));
    println!("ptr {:?}", apply_ptr(double, &xs));
    println!("ptr from closure {:?}", apply_ptr(|x| x - 1, &xs));
    let table: [(&str, fn(i64) -> i64); 4] = [("double", double), ("neg", |x| -x), ("abs", i64::abs), ("sq", |x| x * x)];
    for (nm, f) in table.iter() {
        println!("table {} {:?}", nm, apply_ptr(*f, &xs));
    }

    let clicks = Rc::new(RefCell::new(Vec::new()));
    let mut buttons: Vec<Button> = Vec::new();
    for i in 0..3 {
        let log = clicks.clone();
        let mut count = 0usize;
        buttons.push(Button {
            label: format!("btn{}", i),
            on_click: Box::new(move |who| {
                count += i + 1;
                log.borrow_mut().push(format!("{} by {} (count {})", i, who, count));
                count
            }),
        });
    }
    for round in 0..3 {
        for b in buttons.iter_mut() {
            let who = format!("user{}", round);
            let c = (b.on_click)(&who);
            println!("{} clicked -> {}", b.label, c);
        }
    }
    for line in clicks.borrow().iter() {
        println!("click log: {}", line);
    }
    println!("rc count {}", Rc::strong_count(&clicks));
    drop(buttons);
    println!("rc count after drop {}", Rc::strong_count(&clicks));

    let mut memo = Memo {
        f: |n| (1..=n).map(|k| k * k % 1009).sum(),
        cache: BTreeMap::new(),
        misses: 0,
    };
    let qs = [10u64, 20, 10, 300, 20, 300, 5, 10];
    let rs: Vec<u64> = qs.iter().map(|&q| memo.get(q)).collect();
    println!("memo {:?} misses {}", rs, memo.misses);

    // Recursion through a closure that receives itself.
    fn fix<F: Fn(&dyn Fn(u64) -> u64, u64) -> u64>(f: &F, n: u64) -> u64 {
        f(&|k| fix(f, k), n)
    }
    let fact = |rec: &dyn Fn(u64) -> u64, n: u64| if n <= 1 { 1 } else { n * rec(n - 1) };
    let fibr = |rec: &dyn Fn(u64) -> u64, n: u64| if n < 2 { n } else { rec(n - 1) + rec(n - 2) };
    println!("fix fact {:?}", (0..15).map(|n| fix(&fact, n)).collect::<Vec<_>>());
    println!("fix fib {:?}", (0..20).map(|n| fix(&fibr, n)).collect::<Vec<_>>());

    let people = vec![("zoe", 31), ("adam", 45), ("mia", 22), ("bob", 31)];
    println!("sort by age {:?}", sort_with(&people, |p| p.1));
    println!("sort by name len, rev name {:?}", sort_with(&people, |p| (p.0.len(), std::cmp::Reverse(p.0))));
    let threshold = bb(30);
    let older: Vec<_> = people.iter().filter(|p| p.1 > threshold).map(|p| p.0).collect();
    println!("older than {} {:?}", threshold, older);

    // Closures capturing different things in a loop.
    let mut fns: Vec<Box<dyn Fn() -> String>> = Vec::new();
    for i in 0..4 {
        let s = "*".repeat(i + 1);
        fns.push(Box::new(move || format!("{}{}", i, s)));
    }
    println!("loop captures {:?}", fns.iter().map(|f| f()).collect::<Vec<_>>());

    // A closure that mutates through a captured &mut and then is dropped.
    let mut total = 0i64;
    {
        let mut add = |x: i64| total += x * x;
        for x in -3..=3 {
            add(x);
        }
    }
    println!("total via &mut capture {}", total);
    let mut stack = vec![1];
    let mut step = || {
        let top = *stack.last().unwrap_or(&0);
        stack.push(top * 2 + 1);
        stack.len()
    };
    let lens: Vec<usize> = (0..5).map(|_| step()).collect();
    println!("stack lens {:?} stack {:?}", lens, stack);
    let sizes = (std::mem::size_of_val(&add_k), std::mem::size_of_val(&|| 1), std::mem::size_of_val(&move || (k, total)));
    println!("closure sizes {:?}", sizes);
}
