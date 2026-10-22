// Traits and generics, statically dispatched: associated types and consts,
// default methods overridden or not, generic functions with trait bounds and
// where clauses, generic structs with impls specialised by bound, blanket
// impls over every T: Trait, supertraits, trait static methods, iterator-like
// associated-type plumbing, PhantomData marker types, and Default/Clone/
// PartialEq/Ord/Hash derives.
//
// After monomorphisation all of this is ordinary direct calls, so what it tests
// is that many near-identical copies of one generic function -- the same body at
// u8, i64, f64 and a struct -- each come out right: different load widths,
// different compare instructions, a struct returned through memory in one copy
// and in registers in the next.

use std::collections::hash_map::DefaultHasher;
use std::fmt::Debug;
use std::hash::{Hash, Hasher};
use std::marker::PhantomData;

trait Shape {
    const SIDES: u32;
    const NAME: &'static str;
    fn area(&self) -> f64;
    fn perimeter(&self) -> f64;
    // Default methods, one of which some impls override.
    fn describe(&self) -> String {
        format!("{} with {} sides, area {:.3}, perimeter {:.3}", Self::NAME, Self::SIDES, self.area(), self.perimeter())
    }
    fn ratio(&self) -> f64 {
        self.area() / self.perimeter()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Default)]
struct Square(f64);
#[derive(Debug, Clone, Copy, PartialEq)]
struct Rect {
    w: f64,
    h: f64,
}
#[derive(Debug, Clone, Copy, PartialEq)]
struct Tri {
    a: f64,
    b: f64,
    c: f64,
}

impl Shape for Square {
    const SIDES: u32 = 4;
    const NAME: &'static str = "square";
    fn area(&self) -> f64 {
        self.0 * self.0
    }
    fn perimeter(&self) -> f64 {
        4.0 * self.0
    }
}
impl Shape for Rect {
    const SIDES: u32 = 4;
    const NAME: &'static str = "rect";
    fn area(&self) -> f64 {
        self.w * self.h
    }
    fn perimeter(&self) -> f64 {
        2.0 * (self.w + self.h)
    }
    fn describe(&self) -> String {
        format!("rect {}x{} (overridden describe)", self.w, self.h)
    }
}
impl Shape for Tri {
    const SIDES: u32 = 3;
    const NAME: &'static str = "triangle";
    fn area(&self) -> f64 {
        let s = self.perimeter() / 2.0;
        (s * (s - self.a) * (s - self.b) * (s - self.c)).sqrt()
    }
    fn perimeter(&self) -> f64 {
        self.a + self.b + self.c
    }
}

fn total_area<S: Shape>(shapes: &[S]) -> f64 {
    shapes.iter().map(|s| s.area()).sum()
}

fn largest<T: PartialOrd + Copy>(xs: &[T]) -> Option<T> {
    let mut it = xs.iter();
    let mut best = *it.next()?;
    for &x in it {
        if x > best {
            best = x;
        }
    }
    Some(best)
}

fn summarize<T>(xs: &[T]) -> String
where
    T: Debug + Clone + Into<f64>,
{
    let total: f64 = xs.iter().cloned().map(Into::into).sum();
    format!("{:?} -> total {}", xs, total)
}

// Associated types: a tiny container abstraction.
trait Container {
    type Item;
    fn get(&self, i: usize) -> Option<&Self::Item>;
    fn first(&self) -> Option<&Self::Item> {
        self.get(0)
    }
    fn len(&self) -> usize;
}

struct Stack<T> {
    items: Vec<T>,
}

impl<T> Container for Stack<T> {
    type Item = T;
    fn get(&self, i: usize) -> Option<&T> {
        self.items.get(i)
    }
    fn len(&self) -> usize {
        self.items.len()
    }
}

struct Evens {
    upto: u32,
    cache: Vec<u32>,
}

impl Container for Evens {
    type Item = u32;
    fn get(&self, i: usize) -> Option<&u32> {
        if (i as u32) * 2 <= self.upto {
            self.cache.get(i)
        } else {
            None
        }
    }
    fn len(&self) -> usize {
        (self.upto / 2 + 1) as usize
    }
}

fn dump<C: Container>(c: &C) -> String
where
    C::Item: Debug,
{
    let mut parts = Vec::new();
    for i in 0..c.len() + 1 {
        parts.push(format!("{:?}", c.get(i)));
    }
    format!("len {} first {:?} items [{}]", c.len(), c.first(), parts.join(", "))
}

// A blanket impl: anything Debug gets `.tagged()`.
trait Tagged {
    fn tagged(&self) -> String;
}
impl<T: Debug> Tagged for T {
    fn tagged(&self) -> String {
        format!("<{:?}>", self)
    }
}

// Supertraits.
trait Named {
    fn name(&self) -> String;
}
trait Greeter: Named {
    fn greet(&self) -> String {
        format!("hello from {}", self.name())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]
struct Person {
    last: String,
    first: String,
    age: u32,
}
impl Named for Person {
    fn name(&self) -> String {
        format!("{} {}", self.first, self.last)
    }
}
impl Greeter for Person {}

// Static trait functions and a generic constructor.
trait Zero {
    fn zero() -> Self;
    fn is_zero(&self) -> bool;
}
impl Zero for i32 {
    fn zero() -> Self {
        0
    }
    fn is_zero(&self) -> bool {
        *self == 0
    }
}
impl Zero for f64 {
    fn zero() -> Self {
        0.0
    }
    fn is_zero(&self) -> bool {
        *self == 0.0
    }
}
impl Zero for String {
    fn zero() -> Self {
        String::new()
    }
    fn is_zero(&self) -> bool {
        self.is_empty()
    }
}

fn count_zeros<T: Zero>(xs: &[T]) -> usize {
    xs.iter().filter(|x| x.is_zero()).count()
}

// A generic struct with an impl that exists only for some T.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Pair<T> {
    a: T,
    b: T,
}
impl<T> Pair<T> {
    fn new(a: T, b: T) -> Self {
        Pair { a, b }
    }
    fn swap(self) -> Pair<T> {
        Pair { a: self.b, b: self.a }
    }
}
impl<T: PartialOrd + Copy + Debug> Pair<T> {
    fn larger(&self) -> T {
        if self.a > self.b {
            self.a
        } else {
            self.b
        }
    }
}
impl Pair<f64> {
    fn mean(&self) -> f64 {
        (self.a + self.b) / 2.0
    }
}

// Typed identifiers distinguished only by a phantom marker.
#[derive(Debug, Clone, Copy)]
struct Meters;
#[derive(Debug, Clone, Copy)]
struct Feet;
#[derive(Debug, Clone, Copy)]
struct Length<U> {
    v: f64,
    _u: PhantomData<U>,
}
impl<U> Length<U> {
    fn new(v: f64) -> Self {
        Length { v, _u: PhantomData }
    }
    fn add(self, o: Length<U>) -> Length<U> {
        Length::new(self.v + o.v)
    }
}
impl Length<Feet> {
    fn to_meters(self) -> Length<Meters> {
        Length::new(self.v * 0.3048)
    }
}

// Generic trait with a type parameter, implemented several times for one type.
trait ConvertTo<T> {
    fn convert(&self) -> T;
}
struct Celsius(f64);
impl ConvertTo<f64> for Celsius {
    fn convert(&self) -> f64 {
        self.0 * 9.0 / 5.0 + 32.0
    }
}
impl ConvertTo<String> for Celsius {
    fn convert(&self) -> String {
        format!("{:.1}C", self.0)
    }
}
impl ConvertTo<i64> for Celsius {
    fn convert(&self) -> i64 {
        (self.0 + 273.15).round() as i64
    }
}

fn hash_of<T: Hash>(t: &T) -> u64 {
    let mut h = DefaultHasher::new();
    t.hash(&mut h);
    h.finish()
}

fn main() {
    let squares = [Square(1.0), Square(2.5), Square(3.0)];
    let rects = [Rect { w: 1.0, h: 2.0 }, Rect { w: 3.5, h: 0.5 }];
    let tris = [Tri { a: 3.0, b: 4.0, c: 5.0 }, Tri { a: 2.0, b: 2.0, c: 2.0 }];
    for s in &squares {
        println!("{} ratio {:.4}", s.describe(), s.ratio());
    }
    for r in &rects {
        println!("{} ratio {:.4}", r.describe(), r.ratio());
    }
    for t in &tris {
        println!("{} ratio {:.4}", t.describe(), t.ratio());
    }
    println!(
        "totals {:.3} {:.3} {:.3} sides {} {} {}",
        total_area(&squares),
        total_area(&rects),
        total_area(&tris),
        Square::SIDES,
        <Rect as Shape>::SIDES,
        Tri::SIDES
    );
    println!("default square {:?} eq {}", Square::default(), Square::default() == Square(0.0));

    println!("largest i32 {:?}", largest(&[3, -7, 42, 0, 41]));
    println!("largest f64 {:?}", largest(&[0.5, -1.25, 9.75, 9.5]));
    println!("largest char {:?}", largest(&['q', 'z', 'a']));
    println!("largest str {:?}", largest(&["pear", "apple", "zucchini", "fig"]));
    println!("largest empty {:?}", largest::<u8>(&[]));
    println!("largest u8 {:?}", largest(&[200u8, 255, 3]));
    println!("{}", summarize(&[1u8, 2, 3, 250]));
    println!("{}", summarize(&[1.5f32, -2.25]));
    println!("{}", summarize(&[100000i32, -5, 7]));

    let st = Stack { items: vec!["a", "bb", "ccc"] };
    println!("stack {}", dump(&st));
    let empty: Stack<i64> = Stack { items: vec![] };
    println!("empty {}", dump(&empty));
    let ev = Evens { upto: 10, cache: (0..=5).map(|i| i * 2).collect() };
    println!("evens {}", dump(&ev));

    println!("tagged {} {} {} {}", 5.tagged(), "x".tagged(), Some(1.5).tagged(), (1, 'c', [2u8; 3]).tagged());
    println!("tagged nested {}", vec![Pair::new(1, 2), Pair::new(3, 4)].tagged());

    let mut people = vec![
        Person { last: "Smith".into(), first: "Ann".into(), age: 31 },
        Person { last: "Jones".into(), first: "Bob".into(), age: 25 },
        Person { last: "Smith".into(), first: "Al".into(), age: 40 },
        Person { last: "Adams".into(), first: "Zed".into(), age: 25 },
    ];
    for p in &people {
        println!("{} / {}", p.greet(), p.name());
    }
    people.sort();
    println!("sorted by derive(Ord): {:?}", people.iter().map(|p| p.name()).collect::<Vec<_>>());
    people.sort_by(|a, b| a.age.cmp(&b.age).then_with(|| b.first.cmp(&a.first)));
    println!("sorted by age then first desc: {:?}", people.iter().map(|p| (p.age, p.first.clone())).collect::<Vec<_>>());
    println!("max {:?}", people.iter().max());
    println!("default person {:?}", Person::default());
    println!("clone eq {}", people[0].clone() == people[0]);
    println!("hash equal for clones {}", hash_of(&people[1]) == hash_of(&people[1].clone()));
    println!("hash differs {}", hash_of(&people[1]) != hash_of(&people[2]));
    println!("hash of (1u32, \"x\") {:016x}", hash_of(&(1u32, "x")));
    println!("hash of person0 {:016x}", hash_of(&people[0]));

    println!("zeros {} {} {}", count_zeros(&[0, 1, 0, 2]), count_zeros(&[0.0, -0.0, 1.0]), count_zeros(&[String::new(), "a".into()]));
    println!("zero values {} {} {:?}", i32::zero(), f64::zero(), String::zero());

    let p = Pair::new(3, 9);
    let q = Pair::new(2.5, -1.0);
    let r = Pair::new("left", "right");
    println!("pair {:?} larger {} swap {:?}", p, p.larger(), p.swap());
    println!("pair {:?} larger {} mean {} swap {:?}", q, q.larger(), q.mean(), q.swap());
    println!("pair {:?} larger {} swap {:?}", r, r.larger(), r.swap());

    let a: Length<Feet> = Length::new(10.0);
    let b: Length<Feet> = Length::new(2.5);
    let m = a.add(b).to_meters();
    let m2 = m.add(Length::<Meters>::new(1.0));
    println!("feet {:?} -> meters {:.4} plus one {:.4} size {}", a.add(b).v, m.v, m2.v, std::mem::size_of::<Length<Feet>>());

    for c in [Celsius(-40.0), Celsius(0.0), Celsius(36.6), Celsius(100.0)] {
        let f: f64 = c.convert();
        let s: String = c.convert();
        let k: i64 = c.convert();
        println!("{} = {:.2}F = {}K", s, f, k);
    }
}
