// Enums with data and pattern matching: tuple, struct and unit variants;
// exhaustive match with guards, `@` bindings, ranges (inclusive and char),
// or-patterns, nested enums inside structs inside tuples, slice patterns with
// rest (`[first, .., last]`, `[x, rest @ ..]`), ref and ref mut bindings,
// `if let` / `while let` / `let else` / `matches!`, and C-like enums with
// explicit discriminants cast to integers.
//
// Rust picks a layout per enum: a separate tag, a niche in a pointer or a bool,
// or a tag packed into spare bits of the payload, and match lowers to a
// `br_table` or a compare chain depending on how dense the tags are.  Each enum
// below is shaped to land on a different one of those layouts, and the
// mem::size_of line records which layout was chosen.

use std::hint::black_box as bb;

#[derive(Debug, Clone, PartialEq)]
enum Shape {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Tri(f64, f64, f64),
    Dot,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum Suit {
    Clubs,
    Diamonds,
    Hearts,
    Spades,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Card {
    Num(u8, Suit),
    Face(char, Suit),
    Joker,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u16)]
enum Status {
    Ok = 200,
    Created = 201,
    Moved = 301,
    NotFound = 404,
    Teapot = 418,
    Error = 500,
}

#[derive(Debug)]
enum Json {
    Null,
    Bool(bool),
    Num(f64),
    Str(String),
    Arr(Vec<Json>),
    Obj(Vec<(String, Json)>),
}

#[derive(Debug, Clone, Copy)]
enum Cmd {
    Move { dx: i32, dy: i32 },
    Turn(i8),
    Say(&'static str),
    Stop,
}

fn area(s: &Shape) -> f64 {
    match *s {
        Shape::Circle { r } => std::f64::consts::PI * r * r,
        Shape::Rect { w, h } if w == h => w * w,
        Shape::Rect { w, h } => w * h,
        Shape::Tri(a, b, c) => {
            let p = (a + b + c) / 2.0;
            (p * (p - a) * (p - b) * (p - c)).sqrt()
        }
        Shape::Dot => 0.0,
    }
}

fn card_name(c: Card) -> String {
    match c {
        Card::Num(1, s) => format!("ace of {:?}", s),
        Card::Num(n @ 2..=10, Suit::Hearts | Suit::Diamonds) => format!("red {}", n),
        Card::Num(n @ 2..=10, s) => format!("black {} of {:?}", n, s),
        Card::Num(n, _) => format!("bad number {}", n),
        Card::Face(f @ ('J' | 'Q' | 'K'), s) if s >= Suit::Hearts => format!("high face {} of {:?}", f, s),
        Card::Face(f, s) => format!("face {} of {:?}", f, s),
        Card::Joker => "joker".to_string(),
    }
}

fn classify_char(c: char) -> &'static str {
    match c {
        'a' | 'e' | 'i' | 'o' | 'u' => "vowel",
        'a'..='z' => "consonant",
        'A'..='Z' => "upper",
        '0'..='9' => "digit",
        ' ' | '\t' | '\n' => "space",
        '\u{80}'..=char::MAX => "non-ascii",
        _ => "other",
    }
}

fn classify_num(n: i64) -> String {
    match n {
        i64::MIN..=-1000 => "very negative".into(),
        -999..=-1 => "negative".into(),
        0 => "zero".into(),
        x @ 1..=9 if x % 2 == 0 => format!("small even {}", x),
        x @ 1..=9 => format!("small odd {}", x),
        10 | 20 | 30 => "round".into(),
        x if x.count_ones() == 1 => format!("power of two {}", x),
        _ => "large".into(),
    }
}

fn slices(v: &[i32]) -> String {
    match v {
        [] => "empty".into(),
        [x] => format!("one {}", x),
        [a, b] => format!("two {} {}", a, b),
        [first, .., last] if first == last => format!("bookends {} len {}", first, v.len()),
        [first, mid @ .., last] if mid.len() > 3 => format!("long {}..{} mid sum {}", first, last, mid.iter().sum::<i32>()),
        [0, rest @ ..] => format!("zero-led rest {:?}", rest),
        [.., 0] => "zero-ended".into(),
        [a, b, rest @ ..] => format!("starts {} {} then {} more", a, b, rest.len()),
    }
}

fn sum_rec(v: &[i32]) -> i32 {
    match v {
        [] => 0,
        [head, tail @ ..] => head + sum_rec(tail),
    }
}

fn json_show(j: &Json) -> String {
    match j {
        Json::Null => "null".into(),
        Json::Bool(b) => b.to_string(),
        Json::Num(n) if n.fract() == 0.0 => format!("{}", *n as i64),
        Json::Num(n) => format!("{}", n),
        Json::Str(s) => format!("{:?}", s),
        Json::Arr(items) => format!("[{}]", items.iter().map(json_show).collect::<Vec<_>>().join(",")),
        Json::Obj(kv) => format!("{{{}}}", kv.iter().map(|(k, v)| format!("{:?}:{}", k, json_show(v))).collect::<Vec<_>>().join(",")),
    }
}

fn json_depth(j: &Json) -> usize {
    match j {
        Json::Arr(v) => 1 + v.iter().map(json_depth).max().unwrap_or(0),
        Json::Obj(kv) => 1 + kv.iter().map(|(_, v)| json_depth(v)).max().unwrap_or(0),
        _ => 0,
    }
}

fn run(cmds: &[Cmd]) {
    let (mut x, mut y, mut heading) = (0i32, 0i32, 0i32);
    let mut iter = cmds.iter();
    while let Some(&cmd) = iter.next() {
        match cmd {
            Cmd::Move { dx: 0, dy } => y += dy,
            Cmd::Move { dx, dy: 0 } => x += dx,
            Cmd::Move { dx, dy } => {
                x += dx;
                y += dy;
            }
            Cmd::Turn(d @ -90..=90) => heading = (heading + d as i32).rem_euclid(360),
            Cmd::Turn(d) => heading = (heading - d as i32).rem_euclid(360),
            Cmd::Say(s) => println!("  say {:?} at ({}, {}) heading {}", s, x, y, heading),
            Cmd::Stop => {
                println!("  stop with {} commands left", iter.len());
                break;
            }
        }
    }
    println!("  final ({}, {}) heading {}", x, y, heading);
}

fn parse_kv(s: &str) -> Option<(&str, i32)> {
    let Some((k, v)) = s.split_once('=') else {
        return None;
    };
    let Ok(n) = v.trim().parse::<i32>() else {
        return None;
    };
    Some((k.trim(), n))
}

fn tuples(t: (i32, (char, bool), [u8; 3])) -> String {
    match t {
        (0, _, _) => "zero head".into(),
        (n, ('x', true), [a, ..]) => format!("x-true {} {}", n, a),
        (n, (c, false), [_, mid, _]) if n < 0 => format!("neg {} {} {}", n, c, mid),
        (_, (c, _), [1, 2, 3]) => format!("one-two-three {}", c),
        (n, (c, b), arr) => format!("other {} {} {} {:?}", n, c, b, arr),
    }
}

fn main() {
    let shapes = vec![
        Shape::Circle { r: 1.5 },
        Shape::Rect { w: 2.0, h: 2.0 },
        Shape::Rect { w: 3.0, h: 0.5 },
        Shape::Tri(3.0, 4.0, 5.0),
        Shape::Dot,
    ];
    for s in &shapes {
        println!("{:?} area {:.4} is_rect {} ", s, area(bb(s)), matches!(s, Shape::Rect { .. }));
    }
    let big = shapes.iter().filter(|s| matches!(s, Shape::Circle { r } if *r > 1.0) || area(s) > 5.0).count();
    println!("big shapes {}", big);

    let suits = [Suit::Clubs, Suit::Diamonds, Suit::Hearts, Suit::Spades];
    let mut cards = Vec::new();
    for (i, &s) in suits.iter().enumerate() {
        cards.push(Card::Num(1, s));
        cards.push(Card::Num((i * 3 + 2) as u8, s));
        cards.push(Card::Num(12, s));
        cards.push(Card::Face(['J', 'Q', 'K', 'A'][i], s));
        cards.push(Card::Face('K', s));
    }
    cards.push(Card::Joker);
    for c in &cards {
        println!("{:?} -> {}", c, card_name(bb(*c)));
    }

    for st in [Status::Ok, Status::Created, Status::Moved, Status::NotFound, Status::Teapot, Status::Error] {
        let code = st as u16;
        let class = match code / 100 {
            2 => "success",
            3 => "redirect",
            4 => "client error",
            _ => "server error",
        };
        println!("{:?} = {} ({}) as u8 {}", st, code, class, st as u8);
    }

    let text = "Hi 2u\tÜber ñ!~";
    for c in text.chars() {
        println!("{:?}: {}", c, classify_char(bb(c)));
    }
    for n in [i64::MIN, -5000, -7, 0, 4, 7, 20, 64, 65, 1 << 40] {
        println!("{}: {}", n, classify_num(bb(n)));
    }
    let tests: [&[i32]; 9] = [&[], &[5], &[1, 2], &[4, 9, 4], &[1, 2, 3, 4, 5, 6], &[0, 7, 8], &[3, 8, 0], &[3, 8, 9], &[9, 1, 2, 3, 9]];
    for t in tests.iter() {
        println!("{:?}: {} sum {}", t, slices(t), sum_rec(t));
    }

    let doc = Json::Obj(vec![
        ("name".into(), Json::Str("warm \"c\"".into())),
        ("ok".into(), Json::Bool(true)),
        ("n".into(), Json::Num(42.0)),
        ("pi".into(), Json::Num(3.25)),
        ("list".into(), Json::Arr(vec![Json::Num(1.0), Json::Null, Json::Arr(vec![Json::Bool(false), Json::Arr(vec![])])])),
        ("empty".into(), Json::Obj(vec![])),
    ]);
    println!("json {}", json_show(&doc));
    println!("json depth {}", json_depth(&doc));
    if let Json::Obj(ref kv) = doc {
        for (k, v) in kv {
            let kind = match v {
                Json::Null => "null",
                Json::Bool(_) => "bool",
                Json::Num(_) => "num",
                Json::Str(_) => "str",
                Json::Arr(a) if a.is_empty() => "empty arr",
                Json::Arr(_) => "arr",
                Json::Obj(_) => "obj",
            };
            println!("  key {} is {}", k, kind);
        }
    }

    let cmds = [
        Cmd::Move { dx: 3, dy: 0 },
        Cmd::Move { dx: 0, dy: -2 },
        Cmd::Turn(45),
        Cmd::Say("hello"),
        Cmd::Move { dx: -1, dy: 5 },
        Cmd::Turn(-120),
        Cmd::Turn(100),
        Cmd::Say("there"),
        Cmd::Stop,
        Cmd::Say("never"),
        Cmd::Move { dx: 100, dy: 100 },
    ];
    run(&cmds);
    run(&cmds[..4]);

    for s in ["a=1", " width = 640 ", "noeq", "x=abc", "=5", "k=-17"] {
        println!("parse_kv {:?} -> {:?}", s, parse_kv(s));
    }

    let ts = [
        (0, ('a', true), [0, 0, 0]),
        (5, ('x', true), [9, 8, 7]),
        (-3, ('q', false), [1, 2, 3]),
        (3, ('q', false), [1, 2, 3]),
        (8, ('z', true), [4, 4, 4]),
    ];
    for t in ts {
        println!("{:?} -> {}", t, tuples(t));
    }

    // ref / ref mut bindings and binding modes.
    let mut pair = (String::from("left"), vec![1, 2, 3]);
    {
        let (ref l, ref mut r) = pair;
        r.push(l.len() as i32);
    }
    match pair {
        (ref mut s, ref v) if v.len() > 3 => s.push_str("!"),
        _ => {}
    }
    println!("pair after ref muts {:?}", pair);
    let mut opts = vec![Some(3), None, Some(-4), Some(10), None];
    for o in opts.iter_mut() {
        match o {
            Some(x) if *x < 0 => *x = -*x,
            Some(x) => *x *= 10,
            None => *o = Some(0),
        }
    }
    println!("opts after match-mut {:?}", opts);
    let mut stack = vec![1, 2, 3, 4, 5];
    while let Some(top) = stack.pop() {
        if let 2 | 4 = top {
            println!("popped even {}", top);
        } else if let x @ 3..=5 = top {
            println!("popped mid {}", x);
        } else {
            println!("popped other {}", top);
        }
    }
    let nested = Some(Ok::<(i32, &str), String>((7, "seven")));
    if let Some(Ok((n, name))) = nested {
        println!("nested {} {}", n, name);
    }
    let points = [(0, 0), (3, 0), (0, -2), (1, 1), (-4, 4)];
    for &(x, y) in &points {
        let where_ = match (x, y) {
            (0, 0) => "origin".to_string(),
            (x, 0) | (0, x) => format!("axis at {}", x),
            (x, y) if x == y => "diagonal".to_string(),
            (x, y) if x == -y => "anti-diagonal".to_string(),
            _ => "elsewhere".to_string(),
        };
        println!("point ({}, {}) {}", x, y, where_);
    }
    println!(
        "sizes Shape {} Card {} Suit {} Status {} Option<Box<i32>> {} Option<u32> {} Option<bool> {} Cmd {} Json {}",
        std::mem::size_of::<Shape>(),
        std::mem::size_of::<Card>(),
        std::mem::size_of::<Suit>(),
        std::mem::size_of::<Status>(),
        std::mem::size_of::<Option<Box<i32>>>(),
        std::mem::size_of::<Option<u32>>(),
        std::mem::size_of::<Option<bool>>(),
        std::mem::size_of::<Cmd>(),
        std::mem::size_of::<Json>()
    );
}
