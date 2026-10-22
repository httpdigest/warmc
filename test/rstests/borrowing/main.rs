// Lifetimes, slices and borrowing: functions returning references tied to one
// of several inputs, structs holding references (a tokenizer over a &str, a
// view over a slice), split_at_mut and chunks to hold several disjoint &mut at
// once, iter_mut writes, reborrowing through &mut &mut, mutable references
// passed down a recursion, Cow for borrow-or-own, slices of slices, sub-slice
// arithmetic with as_ptr offsets, and lifetimes in trait impls.
//
// Borrowing is a compile-time discipline, so at run time all of this is
// pointers into the same few buffers -- which is the point: aliasing-free
// code lets LLVM keep values in registers across stores through other
// pointers, and the native code has to preserve exactly the loads and stores
// the wasm still contains.

use std::borrow::Cow;
use std::hint::black_box as bb;

fn longest<'a>(a: &'a str, b: &'a str) -> &'a str {
    if a.len() >= b.len() {
        a
    } else {
        b
    }
}

fn first_word(s: &str) -> &str {
    s.split(' ').next().unwrap_or("")
}

fn pick<'a, 'b>(xs: &'a [i32], _ys: &'b [i32]) -> &'a i32 {
    xs.iter().max().unwrap_or(&0)
}

struct Tokenizer<'a> {
    src: &'a str,
    pos: usize,
}

#[derive(Debug, PartialEq)]
enum Tok<'a> {
    Ident(&'a str),
    Num(&'a str),
    Punct(char),
}

impl<'a> Tokenizer<'a> {
    fn new(src: &'a str) -> Self {
        Tokenizer { src, pos: 0 }
    }
}

impl<'a> Iterator for Tokenizer<'a> {
    type Item = Tok<'a>;
    fn next(&mut self) -> Option<Tok<'a>> {
        let bytes = self.src.as_bytes();
        while self.pos < bytes.len() && bytes[self.pos].is_ascii_whitespace() {
            self.pos += 1;
        }
        if self.pos >= bytes.len() {
            return None;
        }
        let start = self.pos;
        let c = bytes[start];
        if c.is_ascii_alphabetic() || c == b'_' {
            while self.pos < bytes.len() && (bytes[self.pos].is_ascii_alphanumeric() || bytes[self.pos] == b'_') {
                self.pos += 1;
            }
            Some(Tok::Ident(&self.src[start..self.pos]))
        } else if c.is_ascii_digit() {
            while self.pos < bytes.len() && (bytes[self.pos].is_ascii_digit() || bytes[self.pos] == b'.') {
                self.pos += 1;
            }
            Some(Tok::Num(&self.src[start..self.pos]))
        } else {
            let ch = self.src[start..].chars().next().unwrap_or('?');
            self.pos += ch.len_utf8();
            Some(Tok::Punct(ch))
        }
    }
}

struct View<'a, T> {
    data: &'a [T],
    stride: usize,
}

impl<'a, T: Copy + std::fmt::Debug> View<'a, T> {
    fn row(&self, r: usize) -> &'a [T] {
        let s = r * self.stride;
        &self.data[s..(s + self.stride).min(self.data.len())]
    }
    fn col(&self, c: usize) -> Vec<T> {
        self.data.iter().skip(c).step_by(self.stride).cloned().collect()
    }
}

fn normalize(s: &str) -> Cow<'_, str> {
    if s.contains(char::is_uppercase) || s.contains(' ') {
        Cow::Owned(s.to_lowercase().replace(' ', "_"))
    } else {
        Cow::Borrowed(s)
    }
}

fn bump_all(xs: &mut [i32], by: i32) {
    for x in xs.iter_mut() {
        *x += by;
    }
}

fn bump_through(r: &mut &mut Vec<i32>) {
    r.push(99);
    let inner: &mut Vec<i32> = r;
    inner[0] -= 1;
}

fn recurse_fill(xs: &mut [u32], depth: u32) {
    if xs.len() <= 1 {
        if let Some(x) = xs.first_mut() {
            *x = depth;
        }
        return;
    }
    let mid = xs.len() / 2;
    let (l, r) = xs.split_at_mut(mid);
    recurse_fill(l, depth + 1);
    recurse_fill(r, depth + 10);
    l[0] += r[r.len() - 1];
}

fn max_ref<'a>(xs: &'a mut [i64]) -> Option<&'a mut i64> {
    let mut best: Option<&'a mut i64> = None;
    for x in xs.iter_mut() {
        match best {
            Some(ref b) if **b >= *x => {}
            _ => best = Some(x),
        }
    }
    best
}

trait Describe {
    fn describe<'a>(&'a self, prefix: &'a str) -> String;
}
impl<'s> Describe for Tok<'s> {
    fn describe<'a>(&'a self, prefix: &'a str) -> String {
        format!("{}{:?}", prefix, self)
    }
}

fn main() {
    let a = String::from("a fairly long string");
    let res;
    {
        let b = String::from("short");
        res = longest(&a, &b).to_string();
        println!("longest {:?} first_word {:?}", longest(&a, &b), first_word(&b));
    }
    println!("res {:?} first_word {:?}", res, first_word(&a));
    println!("pick {}", pick(&[3, 9, 2], &[100]));

    let src = bb("let x_1 = 42 + foo(3.14, y) * 7; // é");
    let toks: Vec<Tok> = Tokenizer::new(src).collect();
    for t in &toks {
        println!("{}", t.describe("tok "));
    }
    let idents: Vec<&str> = toks.iter().filter_map(|t| if let Tok::Ident(s) = t { Some(*s) } else { None }).collect();
    println!("idents {:?} borrowed from src at offsets {:?}", idents, idents.iter().map(|s| s.as_ptr() as usize - src.as_ptr() as usize).collect::<Vec<_>>());

    let grid: Vec<u16> = (0..20).map(|x| x * 3).collect();
    let v = View { data: &grid, stride: 6 };
    for r in 0..4 {
        println!("row {} {:?}", r, v.row(r));
    }
    for c in 0..6 {
        println!("col {} {:?}", c, v.col(c));
    }

    for s in ["plain", "Mixed Case", "under_score", "has space"] {
        let n = normalize(s);
        let kind = match n {
            Cow::Borrowed(_) => "borrowed",
            Cow::Owned(_) => "owned",
        };
        println!("normalize {:?} -> {:?} ({})", s, n, kind);
    }
    let mut cow: Cow<[i32]> = Cow::Borrowed(&[1, 2, 3]);
    println!("cow before {:?}", cow);
    cow.to_mut().push(4);
    println!("cow after to_mut {:?} owned {}", cow, matches!(cow, Cow::Owned(_)));

    let mut data: Vec<i32> = (0..12).collect();
    {
        let (left, right) = data.split_at_mut(5);
        bump_all(left, 100);
        bump_all(&mut right[2..], -100);
        left[0] += right[0];
        right[1] = left[4] * 2;
    }
    println!("split_at_mut {:?}", data);
    for chunk in data.chunks_mut(5) {
        let s: i32 = chunk.iter().sum();
        if let Some(last) = chunk.last_mut() {
            *last = s;
        }
    }
    println!("chunks_mut sums {:?}", data);
    {
        let mut r = &mut data;
        bump_through(&mut r);
    }
    println!("through &mut &mut {:?}", data);
    let (x, rest) = data.split_first_mut().map(|(x, r)| (x, r)).unwrap_or_else(|| unreachable!());
    *x = rest.iter().filter(|v| **v < 0).count() as i32;
    rest.reverse();
    println!("split_first_mut {:?}", data);

    let mut tree = vec![0u32; 13];
    recurse_fill(&mut tree, 0);
    println!("recurse_fill {:?}", tree);

    let mut big = vec![5i64, -2, 17, 17, 3, 9];
    if let Some(m) = max_ref(&mut big) {
        *m *= -1;
    }
    println!("max_ref negated {:?}", big);

    let words = vec!["alpha".to_string(), "beta".to_string(), "gamma".to_string()];
    let refs: Vec<&String> = words.iter().filter(|w| w.len() > 4).collect();
    let lens: Vec<usize> = refs.iter().map(|w| w.len()).collect();
    println!("refs {:?} lens {:?}", refs, lens);
    let slices: Vec<&[u8]> = words.iter().map(|w| &w.as_bytes()[1..3]).collect();
    println!("byte subslices {:?}", slices);

    let mut matrix = [[0i32; 4]; 4];
    for (i, row) in matrix.iter_mut().enumerate() {
        for (j, cell) in row.iter_mut().enumerate() {
            *cell = (i as i32 - j as i32) * (i + j) as i32;
        }
    }
    let (top, bottom) = matrix.split_at_mut(2);
    for (t, b) in top.iter_mut().zip(bottom.iter_mut()) {
        std::mem::swap(t, b);
    }
    for row in &matrix {
        println!("matrix {:?}", row);
    }
    let diag: Vec<i32> = (0..4).map(|i| matrix[i][i]).collect();
    let anti: i32 = (0..4).map(|i| matrix[i][3 - i]).sum();
    println!("diag {:?} anti sum {}", diag, anti);

    let text = String::from("one two three four five six");
    let mut parts: Vec<&str> = text.split(' ').collect();
    parts.sort_by_key(|p| std::cmp::Reverse(p.len()));
    println!("sorted borrowed parts {:?}", parts);
    let longest_two: Vec<&&str> = parts.iter().take(2).collect();
    println!("longest two {:?}", longest_two);
    let mut s = String::from("grow");
    let len_before = s.len();
    {
        let r = &mut s;
        r.push_str("ing");
        let r2 = &mut *r;
        r2.push('!');
    }
    println!("reborrow {:?} {} -> {}", s, len_before, s.len());
}
