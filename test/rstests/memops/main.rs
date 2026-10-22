// std::mem and the size/layout of ordinary types: mem::swap, replace, take
// (with Default), size_of / align_of / size_of_val for primitives, tuples,
// arrays, slices, enums with and without niches, Option of references and
// boxes, trait objects and closures; mem::discriminant; zero-sized types;
// mem::needs_drop; swapping large structs and array elements; and `Default`
// derived and hand-written.
//
// mem::swap of a large struct is an inlined three-way memcpy through a stack
// temporary; of two Vec headers, three words each.  These copies are lowered to
// memory.copy or to load/store sequences depending on size, and a wrong width
// or overlapping-copy direction shows immediately as a swapped-but-corrupted
// value.

use std::collections::{BTreeMap, VecDeque};
use std::hint::black_box as bb;
use std::mem;
use std::rc::Rc;

#[derive(Debug, Default, Clone, PartialEq)]
struct Big {
    id: u64,
    name: String,
    data: [u32; 16],
    tags: Vec<&'static str>,
    ratio: f64,
}

#[derive(Debug, PartialEq)]
struct Settings {
    depth: u8,
    label: &'static str,
    weights: Vec<i32>,
}
impl Default for Settings {
    fn default() -> Self {
        Settings { depth: 3, label: "default", weights: vec![1, 2, 3] }
    }
}

#[derive(Debug)]
enum Small {
    A,
    B,
    C,
}
#[derive(Debug)]
#[allow(dead_code)]
enum WithData {
    Int(i32),
    Pair(u8, u8),
    Text(String),
    Nothing,
}
struct Zst;
#[derive(Debug, Clone, Copy, Default)]
struct Unit;

fn swaps() {
    let mut a = bb(1);
    let mut b = bb(2);
    mem::swap(&mut a, &mut b);
    println!("swap ints {} {}", a, b);
    let mut s1 = String::from("first string");
    let mut s2 = String::from("second");
    mem::swap(&mut s1, &mut s2);
    println!("swap strings {:?} {:?}", s1, s2);
    let mut x = Big { id: 1, name: "one".into(), data: [1; 16], tags: vec!["a"], ratio: 0.5 };
    let mut y = Big { id: 2, name: "two".into(), data: std::array::from_fn(|i| i as u32 * 7), tags: vec!["b", "c"], ratio: -1.25 };
    mem::swap(&mut x, &mut y);
    println!("swap big x {:?}", x);
    println!("swap big y {:?}", y);
    let mut arr = [10, 20, 30, 40, 50];
    arr.swap(0, 4);
    let (l, r) = arr.split_at_mut(2);
    mem::swap(&mut l[1], &mut r[1]);
    println!("array swaps {:?}", arr);
    let mut va = vec![1, 2, 3];
    let mut vb = vec![9; 7];
    mem::swap(&mut va, &mut vb);
    println!("swap vecs {:?} {:?}", va, vb);
    let mut grid = [[0u8; 3]; 3];
    for i in 0..3 {
        for j in 0..3 {
            grid[i][j] = (i * 3 + j) as u8;
        }
    }
    let (top, rest) = grid.split_at_mut(1);
    mem::swap(&mut top[0], &mut rest[1]);
    println!("swap rows {:?}", grid);
}

fn replace_take() {
    let mut v = vec![1, 2, 3];
    let old = mem::replace(&mut v, vec![7, 8]);
    println!("replace old {:?} new {:?}", old, v);
    let taken = mem::take(&mut v);
    println!("take {:?} left {:?} cap {}", taken, v, v.capacity());
    let mut s = String::from("text");
    let t = mem::take(&mut s);
    println!("take string {:?} left {:?}", t, s);
    let mut big = Big { id: 9, name: "nine".into(), data: [9; 16], tags: vec!["x"], ratio: 9.5 };
    let prev = mem::take(&mut big);
    println!("take big prev id {} name {} now {:?}", prev.id, prev.name, big);
    let mut settings = Settings { depth: 9, label: "custom", weights: vec![] };
    let old_settings = mem::take(&mut settings);
    println!("take settings old {:?} now {:?}", old_settings, settings);
    let mut opt = Some(5);
    let o = mem::replace(&mut opt, None);
    println!("replace option {:?} {:?}", o, opt);
    let mut map: BTreeMap<&str, Vec<u32>> = BTreeMap::new();
    map.insert("a", vec![1, 2]);
    map.insert("b", vec![3]);
    if let Some(e) = map.get_mut("a") {
        let moved = mem::take(e);
        println!("take from map entry {:?}", moved);
    }
    println!("map after {:?}", map);
    // A state machine advanced with mem::replace.
    #[derive(Debug)]
    enum State {
        Idle,
        Running(Vec<u32>),
        Done(u32),
    }
    let mut st = State::Idle;
    for step in 0..6u32 {
        st = match mem::replace(&mut st, State::Idle) {
            State::Idle => State::Running(vec![step]),
            State::Running(mut v) if v.len() < 3 => {
                v.push(step * step);
                State::Running(v)
            }
            State::Running(v) => State::Done(v.iter().sum()),
            State::Done(n) => State::Done(n + 1),
        };
        println!("state step {} -> {:?}", step, st);
    }
    let mut dq: VecDeque<i32> = (1..=4).collect();
    let d = mem::take(&mut dq);
    println!("take deque {:?} {:?}", d, dq);
    println!("defaults {:?} {:?} {:?} {:?} {:?}", i32::default(), String::default(), <(u8, bool, char)>::default(), <[i16; 3]>::default(), Option::<u8>::default());
    println!("default settings {:?} unit {:?}", Settings::default(), Unit);
}

fn sizes() {
    macro_rules! sz {
        ($($t:ty),*) => {
            $( println!("{:<28} size {:3} align {}", stringify!($t), mem::size_of::<$t>(), mem::align_of::<$t>()); )*
        };
    }
    sz!(u8, u16, u32, u64, u128, usize, f32, f64, bool, char, (), Zst, Unit);
    sz!((u8, u16), (u8, u64, u8), [u32; 5], [u8; 0], &u8, &[u8], &str, Box<u64>, Box<[u8]>, Box<dyn Fn()>);
    sz!(Option<u8>, Option<u32>, Option<&u8>, Option<Box<u8>>, Option<char>, Option<bool>, Option<Option<bool>>, Option<Rc<u8>>);
    sz!(Result<u32, ()>, Result<(), u8>, Result<u64, String>, Small, WithData, Option<Small>, Option<WithData>);
    sz!(Vec<u8>, String, Rc<String>, VecDeque<u8>, Big, Settings);
    let arr = [1u16; 7];
    let sl: &[u16] = &arr[2..];
    let st = "héllo";
    let bx: Box<dyn Fn() -> u8> = Box::new(|| 3);
    let cap = bb(5u64);
    let closure = move |x: u64| x + cap;
    println!("size_of_val arr {} slice {} str {} closure {} dyn closure {}", mem::size_of_val(&arr), mem::size_of_val(sl), mem::size_of_val(st), mem::size_of_val(&closure), mem::size_of_val(&*bx));
    println!("closure call {} bx call {}", closure(1), bx());
    let a = WithData::Int(1);
    let b = WithData::Int(99);
    let c = WithData::Text("x".into());
    println!("discriminant same {} different {}", mem::discriminant(&a) == mem::discriminant(&b), mem::discriminant(&a) == mem::discriminant(&c));
    println!("small as int {} {} {}", Small::A as i32, Small::B as u8, Small::C as i64);
    println!("needs_drop u32 {} String {} Big {} Zst {} (u8, Vec<u8>) {}", mem::needs_drop::<u32>(), mem::needs_drop::<String>(), mem::needs_drop::<Big>(), mem::needs_drop::<Zst>(), mem::needs_drop::<(u8, Vec<u8>)>());
    let zs: Vec<Zst> = (0..1000).map(|_| Zst).collect();
    let units = vec![Unit; 50];
    println!("zst vec len {} units {} size_of_val {}", zs.len(), units.len(), mem::size_of_val(&zs[..]));
    let _ = (WithData::Pair(1, 2), WithData::Nothing);
}

fn main() {
    swaps();
    replace_take();
    sizes();
}
