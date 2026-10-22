// Drop order, made observable by a type that prints when it is dropped:
// locals in reverse declaration order, struct fields in declaration order,
// tuple and array elements front to back, Vec elements front to back,
// temporaries at the end of their statement, shadowed bindings at scope end,
// moved-out values dropped by the new owner, `mem::drop` and `mem::forget`,
// `Option::take` and assignment over a live value (old value dropped first),
// early return and `break` out of nested scopes, drops of partially moved
// structs, Box<dyn> through its vtable, ManuallyDrop, and element drops when a
// Vec is truncated, drained, retained or cleared.
//
// The compiler places each drop call itself, as a call to drop glue at a fixed
// point in the control flow, with drop flags for values that are only
// conditionally initialised.  Those flags are locals the back end has to keep
// right across branches -- a miscompile shows as a missing, doubled or
// reordered line.

use std::cell::RefCell;
use std::mem::{self, ManuallyDrop};

thread_local! {
    static LOG: RefCell<Vec<String>> = RefCell::new(Vec::new());
}

fn log(s: String) {
    println!("  {}", s);
    LOG.with(|l| l.borrow_mut().push(s));
}

struct D(&'static str);
impl Drop for D {
    fn drop(&mut self) {
        log(format!("drop {}", self.0));
    }
}

struct Pair {
    _a: D,
    _b: D,
}

struct Outer {
    _first: D,
    _inner: Pair,
    _last: D,
}

impl Drop for Outer {
    fn drop(&mut self) {
        log("drop Outer (before its fields)".to_string());
    }
}

fn make(name: &'static str) -> D {
    log(format!("make {}", name));
    D(name)
}

fn early(flag: bool) -> u32 {
    let _a = make("early-a");
    if flag {
        let _b = make("early-b");
        return 1;
    }
    let _c = make("early-c");
    2
}

fn conditional(init: bool) {
    let x;
    if init {
        x = make("cond-x");
        log(format!("x is {}", x.0));
    }
    let _y = make("cond-y");
    // x is dropped (if it was initialised) after y, by drop flag.
}

fn take_ownership(d: D) {
    log(format!("took {}", d.0));
}

fn give_back(d: D) -> D {
    log(format!("passing through {}", d.0));
    d
}

trait Speak {
    fn hi(&self) -> String;
}
impl Speak for D {
    fn hi(&self) -> String {
        format!("hi from {}", self.0)
    }
}

fn section(name: &str) {
    println!("== {}", name);
}

fn main() {
    section("locals reverse order");
    {
        let _a = make("a");
        let _b = make("b");
        let _c = make("c");
    }
    section("struct fields in order, Drop impl first");
    {
        let _o = Outer { _first: D("first"), _inner: Pair { _a: D("inner.a"), _b: D("inner.b") }, _last: D("last") };
    }
    section("tuple and array");
    {
        let _t = (D("t0"), D("t1"), D("t2"));
        let _arr = [D("arr0"), D("arr1")];
    }
    section("vec elements front to back");
    {
        let v = vec![D("v0"), D("v1"), D("v2")];
        log(format!("vec has {}", v.len()));
    }
    section("temporaries end of statement");
    {
        let n = make("temp").0.len();
        log(format!("after temp statement, len {}", n));
        let kept = &make("extended");
        log(format!("lifetime-extended {}", kept.0));
    }
    section("shadowing");
    {
        let x = make("shadow1");
        let x = D(if x.0.len() > 3 { "shadow2" } else { "never" });
        log(format!("current {}", x.0));
    }
    section("moves");
    {
        let a = make("moved-into-fn");
        take_ownership(a);
        log("after take_ownership".to_string());
        let b = give_back(make("round-trip"));
        log(format!("got back {}", b.0));
        let c = make("moved-into-vec");
        let mut v = Vec::new();
        v.push(c);
        log("vec owns it now".to_string());
        drop(v);
        log("after drop(v)".to_string());
    }
    section("mem::drop, forget, replace, take, swap");
    {
        let a = make("explicit");
        mem::drop(a);
        let f = make("forgotten");
        mem::forget(f);
        let mut slot = make("slot-old");
        let old = mem::replace(&mut slot, D("slot-new"));
        log(format!("replaced {}", old.0));
        let mut o = Some(make("in-option"));
        let taken = o.take();
        log(format!("taken {} option now none {}", taken.as_ref().map(|d| d.0).unwrap_or("-"), o.is_none()));
        let mut p = make("p");
        let mut q = make("q");
        mem::swap(&mut p, &mut q);
        log(format!("swapped p={} q={}", p.0, q.0));
    }
    section("assignment drops old value first");
    {
        let mut x = make("assign-1");
        log(format!("now {}", x.0));
        x = make("assign-2");
        log(format!("now {}", x.0));
        x = make("assign-3");
        log(format!("now {}", x.0));
    }
    section("early return");
    log(format!("early(true) = {}", early(true)));
    log(format!("early(false) = {}", early(false)));
    section("conditional init (drop flags)");
    conditional(true);
    conditional(false);
    section("loop with break");
    {
        let _outer = make("loop-outer");
        for i in 0..5 {
            let _it = D(["it0", "it1", "it2", "it3", "it4"][i]);
            if i == 2 {
                let _brk = make("before-break");
                break;
            }
        }
        log("after loop".to_string());
    }
    section("partial move out of struct");
    {
        let p = Pair { _a: make("pm-a"), _b: make("pm-b") };
        let Pair { _a: a, .. } = p;
        log(format!("moved out {}", a.0));
        drop(a);
        log("end of partial-move scope".to_string());
    }
    section("Box<dyn> through vtable");
    {
        let b: Box<dyn Speak> = Box::new(make("boxed-dyn"));
        log(b.hi());
        let list: Vec<Box<dyn Speak>> = vec![Box::new(D("dyn0")), Box::new(D("dyn1"))];
        log(format!("{} items", list.len()));
    }
    section("ManuallyDrop");
    {
        let mut m = ManuallyDrop::new(make("manual"));
        log(format!("holding {}", m.0));
        let _plain = make("plain");
        unsafe { ManuallyDrop::drop(&mut m) };
        log("dropped manually".to_string());
    }
    section("truncate, drain, clear, retain");
    {
        let mut v = vec![D("e0"), D("e1"), D("e2"), D("e3"), D("e4"), D("e5")];
        v.truncate(4);
        log("truncated".to_string());
        let drained: Vec<D> = v.drain(1..3).collect();
        log(format!("drained {}", drained.len()));
        drop(drained);
        v.retain(|d| d.0 != "e0");
        log("retained".to_string());
        v.clear();
        log("cleared".to_string());
    }
    section("closures own their captures");
    {
        let d = make("captured");
        let f = move || d.0.len();
        log(format!("closure result {}", f()));
        drop(f);
        log("closure dropped".to_string());
    }
    section("match arm temporaries");
    {
        match make("scrutinee").0 {
            "scrutinee" => log("matched".to_string()),
            _ => log("not matched".to_string()),
        }
        log("after match".to_string());
    }
    let total = LOG.with(|l| l.borrow().len());
    let drops = LOG.with(|l| l.borrow().iter().filter(|s| s.starts_with("drop ")).count());
    println!("log lines {} drops {}", total, drops);
}
