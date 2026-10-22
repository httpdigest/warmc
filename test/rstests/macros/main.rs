// macro_rules! macros: fragment specifiers (expr, ident, ty, tt, literal,
// pat, block, path), repetition with `*`, `+` and `?` and separators,
// recursion and counting by tt-munching, macros that define functions,
// structs, enums and trait impls, hygiene (a macro's locals do not collide
// with the caller's), nested macros, internal rules selected by an `@go`
// marker, and std macros: vec!, format_args!, concat!, stringify!, matches!,
// assert!/assert_eq! (passing), write! and module_path!.
//
// Macros are expanded before anything reaches LLVM, so what runs is ordinary
// code -- but code with an unusual shape: deep chains of nested arithmetic
// expressions, many tiny generated functions, and large `match` statements
// from repetition, which stress the constant folder and the instruction
// selector in ways hand-written code rarely does.

// An empty repetition leaves a macro's `let mut` unmutated.
#![allow(unused_mut)]

use std::collections::BTreeMap;
use std::fmt::Write;
use std::hint::black_box as bb;

macro_rules! square {
    ($x:expr) => {
        $x * $x
    };
}

macro_rules! max_of {
    ($x:expr) => { $x };
    ($x:expr, $($rest:expr),+) => {{
        let a = $x;
        let b = max_of!($($rest),+);
        if a > b { a } else { b }
    }};
}

macro_rules! count {
    () => { 0usize };
    ($head:tt $($tail:tt)*) => { 1usize + count!($($tail)*) };
}

macro_rules! hashmap {
    ($($k:expr => $v:expr),* $(,)?) => {{
        let mut m = BTreeMap::new();
        $( m.insert($k, $v); )*
        m
    }};
}

macro_rules! make_struct {
    ($name:ident { $($field:ident : $ty:ty = $default:expr),* $(,)? }) => {
        #[derive(Debug, Clone, PartialEq)]
        struct $name {
            $($field: $ty),*
        }
        impl $name {
            fn new() -> Self {
                $name { $($field: $default),* }
            }
            fn field_names() -> Vec<&'static str> {
                vec![$(stringify!($field)),*]
            }
        }
    };
}

make_struct!(Config {
    width: u32 = 640,
    height: u32 = 480,
    title: String = String::from("untitled"),
    scale: f64 = 1.5,
    tags: Vec<&'static str> = vec!["a", "b"],
});

macro_rules! make_enum {
    ($name:ident : $($variant:ident = $val:expr),+ $(,)?) => {
        #[derive(Debug, Clone, Copy, PartialEq)]
        enum $name { $($variant),+ }
        impl $name {
            const ALL: &'static [$name] = &[$($name::$variant),+];
            fn value(self) -> i32 {
                match self { $($name::$variant => $val),+ }
            }
            fn name(self) -> &'static str {
                match self { $($name::$variant => stringify!($variant)),+ }
            }
            fn parse(s: &str) -> Option<$name> {
                match s { $(stringify!($variant) => Some($name::$variant),)+ _ => None }
            }
        }
    };
}

make_enum!(Color: Red = 0xff0000, Green = 0x00ff00, Blue = 0x0000ff, Gray = 0x808080);

macro_rules! ops_fns {
    ($($name:ident => $op:tt),*) => {
        $( fn $name(a: i64, b: i64) -> i64 { a $op b } )*
        const OPS: &[(&str, fn(i64, i64) -> i64)] = &[$((stringify!($name), $name)),*];
    };
}

ops_fns!(add => +, sub => -, mul => *, band => &, bor => |, bxor => ^);

trait Describe {
    fn describe(&self) -> String;
}

macro_rules! impl_describe {
    ($($t:ty),+) => {
        $( impl Describe for $t {
            fn describe(&self) -> String {
                format!("{}={:?} ({} bytes)", stringify!($t), self, std::mem::size_of::<$t>())
            }
        } )+
    };
}

impl_describe!(u8, i16, u32, i64, f32, f64, bool, char, (u8, u8), [u16; 3]);

// tt-muncher: a tiny RPN evaluator written in the macro language.
macro_rules! rpn {
    (@go [$($stack:expr),*]) => { [$($stack),*] };
    (@go [$a:expr, $b:expr $(, $rest:expr)*] + $($tail:tt)*) => { rpn!(@go [$b + $a $(, $rest)*] $($tail)*) };
    (@go [$a:expr, $b:expr $(, $rest:expr)*] * $($tail:tt)*) => { rpn!(@go [$b * $a $(, $rest)*] $($tail)*) };
    (@go [$a:expr, $b:expr $(, $rest:expr)*] - $($tail:tt)*) => { rpn!(@go [$b - $a $(, $rest)*] $($tail)*) };
    (@go [$($stack:expr),*] $num:literal $($tail:tt)*) => { rpn!(@go [$num $(, $stack)*] $($tail)*) };
    ($($t:tt)*) => { rpn!(@go [] $($t)*) };
}

// Hygiene: `tmp` inside the macro does not see or clobber the caller's `tmp`.
macro_rules! swap_add {
    ($a:ident, $b:ident) => {
        let tmp = $a;
        $a = $b;
        $b = tmp + $b;
    };
}

macro_rules! log_expr {
    ($($e:expr),+ $(,)?) => {
        $( println!("{:<28} = {:?}", stringify!($e), $e); )+
    };
}

macro_rules! check_pat {
    ($v:expr, $($p:pat),+) => {
        [$(matches!($v, $p)),+]
    };
}

macro_rules! with_block {
    ($name:ident, $body:block) => {
        fn $name() -> String {
            let mut out = String::new();
            let r = $body;
            let _ = write!(out, "{}:{}", stringify!($name), r);
            out
        }
    };
}

with_block!(blk_sum, { (1..=10).sum::<i32>() });
with_block!(blk_str, { "lit".repeat(2) });

macro_rules! opt_suffix {
    ($base:expr $(, suffix = $s:expr)?) => {{
        let mut v = String::from($base);
        $( v.push_str($s); )?
        v
    }};
}

macro_rules! matrix {
    ($([$($x:expr),*]),*) => { vec![$(vec![$($x),*]),*] };
}

macro_rules! nested_count {
    ($([$($inner:tt)*])*) => { [$(count!($($inner)*)),*] };
}

macro_rules! path_call {
    ($p:path, $($arg:expr),*) => { $p($($arg),*) };
}

fn main() {
    let n = bb(7i64);
    log_expr!(square!(n), square!(n + 1), square!(square!(2)), max_of!(3), max_of!(1, 9, 4), max_of!(n, -n, n * 2, 13));
    log_expr!(count!(), count!(a), count!(a b c d e), count!((x) [y] {z} 1 "s"));
    let m = hashmap! { "one" => 1, "two" => 2, "three" => 3, };
    println!("hashmap! {:?}", m);
    let empty: BTreeMap<i32, i32> = hashmap! {};
    println!("hashmap! empty {:?}", empty);

    let mut c = Config::new();
    println!("make_struct {:?}", c);
    println!("fields {:?}", Config::field_names());
    c.width *= 2;
    c.tags.push("c");
    println!("modified {:?} eq default {}", c, c == Config::new());

    for col in Color::ALL {
        println!("color {} = {:06x} parse back {:?}", col.name(), col.value(), Color::parse(col.name()));
    }
    println!("parse bad {:?}", Color::parse("Purple"));

    for (name, f) in OPS {
        println!("op {} -> {} {} {}", name, f(12, 10), f(-7, 3), f(bb(0x0ff0), 0x00ff));
    }

    let ds: Vec<Box<dyn Describe>> = vec![Box::new(200u8), Box::new(-3i16), Box::new(70000u32), Box::new(i64::MIN), Box::new(1.5f32), Box::new(2.25f64), Box::new(true), Box::new('m'), Box::new((1u8, 2u8)), Box::new([1u16, 2, 3])];
    for d in &ds {
        println!("describe {}", d.describe());
    }

    log_expr!(rpn!(3 4 +), rpn!(5 1 2 + 4 * + 3 -), rpn!(2 3 4 * *), rpn!(10 2 - 3 -), rpn!(1 2 3));

    let mut a = bb(5);
    let mut b = bb(11);
    let tmp = 1000;
    swap_add!(a, b);
    println!("hygiene a {} b {} tmp {}", a, b, tmp);
    swap_add!(a, b);
    println!("hygiene again a {} b {} tmp {}", a, b, tmp);

    for v in [0, 5, 42, -1] {
        println!("check_pat {} {:?}", v, check_pat!(v, 0, 1..=9, (42 | 43), i32::MIN..=-1));
    }
    println!("{} {}", blk_sum(), blk_str());
    println!("opt_suffix {:?} {:?}", opt_suffix!("base"), opt_suffix!("base", suffix = "_x"));
    let mx = matrix!([1, 2, 3], [4, 5, 6], [7, 8, 9]);
    println!("matrix! {:?} diag {:?}", mx, (0..3).map(|i| mx[i][i]).collect::<Vec<_>>());
    println!("nested_count {:?}", nested_count!([a b] [] [c d e f] [g]));
    println!("path_call {} {} {}", path_call!(i32::max, 3, 9), path_call!(std::cmp::min, 3, 9), path_call!(u8::wrapping_add, 250, 10));

    println!("concat! {}", concat!("a", 1, 2.5, true, 'c'));
    println!("stringify! {}", stringify!(x + y * (z - 1)));
    let fa = format!("{}", format_args!("{}-{:03}", "fa", 7));
    println!("format_args! {}", fa);
    let v = vec![bb(1); 4];
    let w: Vec<String> = vec![];
    assert!(v.len() == 4, "vec len");
    assert_eq!(w.len(), 0);
    debug_assert!(true);
    println!("vec! {:?} {:?} {:?}", v, w, vec![vec!['x'; 2]; 2]);
    let mut out = String::new();
    for i in 0..5 {
        let _ = write!(&mut out, "[{}{}]", i, if i % 2 == 0 { "e" } else { "o" });
    }
    println!("write! {}", out);
    println!("env-free {}", module_path!());
}
