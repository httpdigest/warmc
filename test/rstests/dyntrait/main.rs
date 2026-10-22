// Trait objects: Box<dyn Trait>, &dyn and &mut dyn, Rc<dyn>, a Vec of mixed
// concrete types, dyn with generic methods kept off the vtable (where Self:
// Sized), default methods called through a vtable, supertrait upcasting,
// `dyn Any` downcasting, closures stored as Box<dyn Fn>, and Drop through a
// fat pointer.
//
// Each call here is a load from the vtable and a `call_indirect`, with the
// receiver's data pointer as the first argument.  The vtable also carries the
// size, alignment and drop glue, which Box<dyn> uses to free the object -- so a
// wrong vtable offset shows as a wrong method, a wrong size passed to the
// allocator, or a missing "drop" line.

use std::any::Any;
use std::cell::RefCell;
use std::fmt;
use std::rc::Rc;

trait Animal {
    fn name(&self) -> String;
    fn sound(&self) -> &'static str;
    fn legs(&self) -> u32 {
        4
    }
    fn speak(&self) -> String {
        format!("{} the {}-legged says {}", self.name(), self.legs(), self.sound())
    }
    fn rename(&mut self, n: &str);
    // Generic: not callable through dyn, but the trait stays object-safe.
    fn with<F: Fn(&str) -> String>(&self, f: F) -> String
    where
        Self: Sized,
    {
        f(&self.name())
    }
}

struct Dog {
    name: String,
    tricks: u32,
}
struct Bird {
    name: String,
    can_fly: bool,
}
struct Snake {
    len_cm: u16,
}

impl Animal for Dog {
    fn name(&self) -> String {
        format!("{}({} tricks)", self.name, self.tricks)
    }
    fn sound(&self) -> &'static str {
        "woof"
    }
    fn rename(&mut self, n: &str) {
        self.name = n.to_string();
        self.tricks += 1;
    }
}
impl Animal for Bird {
    fn name(&self) -> String {
        self.name.clone()
    }
    fn sound(&self) -> &'static str {
        if self.can_fly {
            "tweet"
        } else {
            "squawk"
        }
    }
    fn legs(&self) -> u32 {
        2
    }
    fn rename(&mut self, n: &str) {
        self.name = n.to_uppercase();
    }
}
impl Animal for Snake {
    fn name(&self) -> String {
        format!("snake of {}cm", self.len_cm)
    }
    fn sound(&self) -> &'static str {
        "hiss"
    }
    fn legs(&self) -> u32 {
        0
    }
    fn speak(&self) -> String {
        format!("a {} only says {}", self.name(), self.sound())
    }
    fn rename(&mut self, _n: &str) {
        self.len_cm = self.len_cm.wrapping_mul(2);
    }
}

impl Drop for Dog {
    fn drop(&mut self) {
        println!("drop Dog {}", self.name);
    }
}
impl Drop for Snake {
    fn drop(&mut self) {
        println!("drop Snake {}", self.len_cm);
    }
}

fn chorus(zoo: &[Box<dyn Animal>]) -> Vec<String> {
    zoo.iter().map(|a| a.speak()).collect()
}

fn rename_all(zoo: &mut Vec<Box<dyn Animal>>) {
    for (i, a) in zoo.iter_mut().enumerate() {
        let n = format!("n{}", i);
        a.rename(&n);
    }
}

fn loudest<'a>(a: &'a dyn Animal, b: &'a dyn Animal) -> &'a dyn Animal {
    if a.sound().len() >= b.sound().len() {
        a
    } else {
        b
    }
}

// A visitor over an expression tree through dyn.
trait Expr: fmt::Display {
    fn eval(&self) -> i64;
    fn depth(&self) -> usize {
        1
    }
}
struct Num(i64);
struct Bin {
    op: char,
    l: Box<dyn Expr>,
    r: Box<dyn Expr>,
}
impl fmt::Display for Num {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}
impl fmt::Display for Bin {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "({} {} {})", self.l, self.op, self.r)
    }
}
impl Expr for Num {
    fn eval(&self) -> i64 {
        self.0
    }
}
impl Expr for Bin {
    fn eval(&self) -> i64 {
        let (a, b) = (self.l.eval(), self.r.eval());
        match self.op {
            '+' => a.wrapping_add(b),
            '-' => a.wrapping_sub(b),
            '*' => a.wrapping_mul(b),
            '/' => a.checked_div(b).unwrap_or(0),
            _ => 0,
        }
    }
    fn depth(&self) -> usize {
        1 + self.l.depth().max(self.r.depth())
    }
}

fn build(n: u32) -> Box<dyn Expr> {
    if n == 0 {
        return Box::new(Num(3));
    }
    let ops = ['+', '*', '-', '/'];
    Box::new(Bin {
        op: ops[(n % 4) as usize],
        l: build(n - 1),
        r: Box::new(Num(n as i64 + 1)),
    })
}

// Observers held as Rc<RefCell<dyn Trait>>.
trait Observer {
    fn notify(&mut self, ev: &str);
    fn report(&self) -> String;
}
struct Counter {
    n: usize,
}
struct Logger {
    lines: Vec<String>,
}
impl Observer for Counter {
    fn notify(&mut self, _ev: &str) {
        self.n += 1;
    }
    fn report(&self) -> String {
        format!("counter saw {}", self.n)
    }
}
impl Observer for Logger {
    fn notify(&mut self, ev: &str) {
        if ev.len() > 3 {
            self.lines.push(ev.to_string());
        }
    }
    fn report(&self) -> String {
        format!("logger kept {:?}", self.lines)
    }
}

fn describe_any(v: &dyn Any) -> String {
    if let Some(i) = v.downcast_ref::<i32>() {
        format!("i32 {}", i)
    } else if let Some(s) = v.downcast_ref::<String>() {
        format!("String {:?}", s)
    } else if let Some(s) = v.downcast_ref::<&str>() {
        format!("&str {:?}", s)
    } else if let Some(f) = v.downcast_ref::<f64>() {
        format!("f64 {}", f)
    } else if let Some(v) = v.downcast_ref::<Vec<u8>>() {
        format!("Vec<u8> of {}", v.len())
    } else {
        "unknown".to_string()
    }
}

// Supertrait upcasting: &dyn Sub -> &dyn Super.
trait Base {
    fn id(&self) -> u32;
}
trait Derived: Base {
    fn extra(&self) -> u32;
}
struct Thing(u32);
impl Base for Thing {
    fn id(&self) -> u32 {
        self.0
    }
}
impl Derived for Thing {
    fn extra(&self) -> u32 {
        self.0 * 100
    }
}
fn use_base(b: &dyn Base) -> u32 {
    b.id() + 1
}

fn main() {
    let mut zoo: Vec<Box<dyn Animal>> = Vec::new();
    for i in 0..9u32 {
        match i % 3 {
            0 => zoo.push(Box::new(Dog { name: format!("rex{}", i), tricks: i })),
            1 => zoo.push(Box::new(Bird { name: format!("tweety{}", i), can_fly: i % 2 == 1 })),
            _ => zoo.push(Box::new(Snake { len_cm: (i * 37) as u16 })),
        }
    }
    for line in chorus(&zoo) {
        println!("{}", line);
    }
    rename_all(&mut zoo);
    for line in chorus(&zoo) {
        println!("renamed: {}", line);
    }
    let legs: u32 = zoo.iter().map(|a| a.legs()).sum();
    println!("total legs {}", legs);
    println!("loudest {}", loudest(&*zoo[0], &*zoo[1]).name());
    println!("loudest {}", loudest(&*zoo[2], &*zoo[4]).name());
    let d = Dog { name: "solo".into(), tricks: 2 };
    println!("generic with: {}", d.with(|n| n.chars().rev().collect()));
    println!("size of &dyn {} Box<dyn> {} &Dog {}", std::mem::size_of::<&dyn Animal>(), std::mem::size_of::<Box<dyn Animal>>(), std::mem::size_of::<&Dog>());
    println!("removing index 3");
    let removed = zoo.remove(3);
    println!("removed speaks: {}", removed.speak());
    drop(removed);
    println!("truncating to 4");
    zoo.truncate(4);
    println!("after truncate {}", zoo.len());

    for n in 0..8 {
        let e = build(n);
        println!("expr {} = {} depth {}", e, e.eval(), e.depth());
    }

    let counter = Rc::new(RefCell::new(Counter { n: 0 }));
    let logger = Rc::new(RefCell::new(Logger { lines: vec![] }));
    let observers: Vec<Rc<RefCell<dyn Observer>>> = vec![counter.clone(), logger.clone(), counter.clone()];
    for ev in ["start", "go", "tick", "stop", "a", "finish"] {
        for o in &observers {
            o.borrow_mut().notify(ev);
        }
    }
    for o in &observers {
        println!("{}", o.borrow().report());
    }
    println!("counter strong {} direct {}", Rc::strong_count(&counter), counter.borrow().n);

    let anys: Vec<Box<dyn Any>> = vec![
        Box::new(42i32),
        Box::new(String::from("owned")),
        Box::new("borrowed"),
        Box::new(2.5f64),
        Box::new(vec![1u8, 2, 3]),
        Box::new('c'),
    ];
    for a in &anys {
        println!("any: {} is_i32 {}", describe_any(a.as_ref()), a.is::<i32>());
    }
    let b: Box<dyn Any> = Box::new(77i32);
    match b.downcast::<i32>() {
        Ok(v) => println!("downcast box ok {}", v),
        Err(_) => println!("downcast box failed"),
    }

    let ops: Vec<(&str, Box<dyn Fn(i64, i64) -> i64>)> = vec![
        ("add", Box::new(|a, b| a + b)),
        ("sub", Box::new(|a, b| a - b)),
        ("mul", Box::new(|a, b| a * b)),
        ("max", Box::new(std::cmp::max)),
        ("pow", Box::new(|a, b| a.pow(b as u32))),
    ];
    for (name, f) in &ops {
        let row: Vec<i64> = (1..=5).map(|x| f(x, 3)).collect();
        println!("{} {:?}", name, row);
    }

    let t = Thing(7);
    let dd: &dyn Derived = &t;
    let bb: &dyn Base = dd;
    println!("upcast id {} extra {} base {}", bb.id(), dd.extra(), use_base(dd));
    println!("end of main");
}
