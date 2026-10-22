// Iterators written by hand: struct impls of Iterator with state machines,
// DoubleEndedIterator and ExactSizeIterator impls, IntoIterator for a custom
// collection (by value, by & and by &mut), an iterator over a tree using an
// explicit stack, a lending-style windowed iterator over a slice with
// lifetimes, iter::from_fn, iter::successors, iter::repeat_with, iter::once,
// iter::empty, and a generic adapter (a custom `Interleave<A, B>`) composed
// with std's adapters.
//
// Each custom `next` is a small function whose state lives in a struct the
// optimiser may scalar-replace into locals once the loop is inlined -- or not,
// at -O0 -- so the same iteration is computed through memory in one variant and
// through registers in another.

use std::hint::black_box as bb;
use std::iter;

// Collatz sequence as an iterator.
struct Collatz {
    cur: u64,
    done: bool,
}
impl Iterator for Collatz {
    type Item = u64;
    fn next(&mut self) -> Option<u64> {
        if self.done {
            return None;
        }
        let v = self.cur;
        if v == 1 {
            self.done = true;
        } else if v % 2 == 0 {
            self.cur = v / 2;
        } else {
            self.cur = 3 * v + 1;
        }
        Some(v)
    }
}

// A counting range that knows its length and can run backwards.
#[derive(Clone)]
struct Span {
    lo: i32,
    hi: i32,
    step: i32,
}
impl Iterator for Span {
    type Item = i32;
    fn next(&mut self) -> Option<i32> {
        if self.lo >= self.hi {
            None
        } else {
            let v = self.lo;
            self.lo += self.step;
            Some(v)
        }
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        let n = if self.lo >= self.hi { 0 } else { ((self.hi - self.lo + self.step - 1) / self.step) as usize };
        (n, Some(n))
    }
}
impl DoubleEndedIterator for Span {
    fn next_back(&mut self) -> Option<i32> {
        let n = self.len();
        if n == 0 {
            None
        } else {
            let last = self.lo + (n as i32 - 1) * self.step;
            self.hi = last;
            Some(last)
        }
    }
}
impl ExactSizeIterator for Span {}

// A ring of fixed capacity, iterable three ways.
struct Ring {
    buf: Vec<u32>,
    start: usize,
}
impl Ring {
    fn iter(&self) -> RingIter<'_> {
        RingIter { ring: self, i: 0 }
    }
}
struct RingIter<'a> {
    ring: &'a Ring,
    i: usize,
}
impl<'a> Iterator for RingIter<'a> {
    type Item = &'a u32;
    fn next(&mut self) -> Option<&'a u32> {
        if self.i >= self.ring.buf.len() {
            return None;
        }
        let k = (self.ring.start + self.i) % self.ring.buf.len();
        self.i += 1;
        Some(&self.ring.buf[k])
    }
}
impl<'a> IntoIterator for &'a Ring {
    type Item = &'a u32;
    type IntoIter = RingIter<'a>;
    fn into_iter(self) -> RingIter<'a> {
        self.iter()
    }
}
impl<'a> IntoIterator for &'a mut Ring {
    type Item = &'a mut u32;
    type IntoIter = std::iter::Chain<std::slice::IterMut<'a, u32>, std::slice::IterMut<'a, u32>>;
    fn into_iter(self) -> Self::IntoIter {
        let (a, b) = self.buf.split_at_mut(self.start);
        b.iter_mut().chain(a.iter_mut())
    }
}
impl IntoIterator for Ring {
    type Item = u32;
    type IntoIter = std::vec::IntoIter<u32>;
    fn into_iter(mut self) -> std::vec::IntoIter<u32> {
        self.buf.rotate_left(self.start);
        self.buf.into_iter()
    }
}

// In-order traversal of a binary tree with an explicit stack.
enum Tree {
    Leaf,
    Node(Box<Tree>, i32, Box<Tree>),
}
fn insert(t: Tree, v: i32) -> Tree {
    match t {
        Tree::Leaf => Tree::Node(Box::new(Tree::Leaf), v, Box::new(Tree::Leaf)),
        Tree::Node(l, x, r) => {
            if v < x {
                Tree::Node(Box::new(insert(*l, v)), x, r)
            } else {
                Tree::Node(l, x, Box::new(insert(*r, v)))
            }
        }
    }
}
struct InOrder<'a> {
    stack: Vec<&'a Tree>,
    cur: &'a Tree,
}
impl<'a> Iterator for InOrder<'a> {
    type Item = i32;
    fn next(&mut self) -> Option<i32> {
        loop {
            match self.cur {
                Tree::Node(l, _, _) => {
                    self.stack.push(self.cur);
                    self.cur = l;
                }
                Tree::Leaf => {
                    let n = self.stack.pop()?;
                    if let Tree::Node(_, v, r) = n {
                        self.cur = r;
                        return Some(*v);
                    }
                }
            }
        }
    }
}

// A generic adapter: alternate between two iterators until both are done.
struct Interleave<A, B> {
    a: A,
    b: B,
    turn: bool,
}
impl<T, A: Iterator<Item = T>, B: Iterator<Item = T>> Iterator for Interleave<A, B> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        self.turn = !self.turn;
        if self.turn {
            self.a.next().or_else(|| self.b.next())
        } else {
            self.b.next().or_else(|| self.a.next())
        }
    }
}
trait InterleaveExt: Iterator + Sized {
    fn interleave<B: Iterator<Item = Self::Item>>(self, b: B) -> Interleave<Self, B> {
        Interleave { a: self, b, turn: false }
    }
}
impl<I: Iterator> InterleaveExt for I {}

// Pairs of adjacent elements borrowed from a slice.
struct Pairs<'a, T> {
    s: &'a [T],
}
impl<'a, T> Iterator for Pairs<'a, T> {
    type Item = (&'a T, &'a T);
    fn next(&mut self) -> Option<Self::Item> {
        match self.s {
            [a, b, ..] => {
                self.s = &self.s[1..];
                Some((a, b))
            }
            _ => None,
        }
    }
}

// Prime sieve as an iterator that grows its table on demand.
struct Primes {
    found: Vec<u64>,
    next: u64,
}
impl Iterator for Primes {
    type Item = u64;
    fn next(&mut self) -> Option<u64> {
        loop {
            let n = self.next;
            self.next += 1;
            if self.found.iter().take_while(|&&p| p * p <= n).all(|&p| n % p != 0) {
                self.found.push(n);
                return Some(n);
            }
        }
    }
}

fn main() {
    for start in [1u64, 6, 7, 27, 97] {
        let seq: Vec<u64> = Collatz { cur: bb(start), done: false }.collect();
        println!("collatz {} len {} max {:?} head {:?}", start, seq.len(), seq.iter().max(), &seq[..seq.len().min(12)]);
    }
    let c = Collatz { cur: 871, done: false };
    println!("collatz 871 steps {} sum {}", c.count() - 1, Collatz { cur: 871, done: false }.sum::<u64>());

    let sp = Span { lo: bb(3), hi: bb(40), step: bb(4) };
    println!("span {:?} len {}", sp.clone().collect::<Vec<_>>(), sp.len());
    println!("span rev {:?}", sp.clone().rev().collect::<Vec<_>>());
    let mut sp2 = sp.clone();
    println!("span ends {:?} {:?} {:?} {:?} left {}", sp2.next(), sp2.next_back(), sp2.next(), sp2.next_back(), sp2.len());
    println!("span zip rev {:?}", sp.clone().zip(sp.clone().rev()).collect::<Vec<_>>());
    println!("span empty {:?} {}", Span { lo: 5, hi: 5, step: 1 }.collect::<Vec<_>>(), Span { lo: 9, hi: 2, step: 3 }.len());

    let mut ring = Ring { buf: (10..18).collect(), start: 5 };
    println!("ring by ref {:?}", ring.iter().collect::<Vec<_>>());
    let mut total = 0;
    for x in &ring {
        total += x;
    }
    println!("ring for-loop total {}", total);
    for (i, x) in (&mut ring).into_iter().enumerate() {
        *x = *x * 100 + i as u32;
    }
    println!("ring after mut {:?}", ring.iter().collect::<Vec<_>>());
    println!("ring by value {:?}", ring.into_iter().map(|x| x % 1000).collect::<Vec<_>>());

    let mut t = Tree::Leaf;
    for v in [50, 30, 70, 20, 40, 60, 80, 35, 45, 65, 10, 90, 55] {
        t = insert(t, bb(v));
    }
    let io = InOrder { stack: Vec::new(), cur: &t };
    println!("inorder {:?}", io.collect::<Vec<_>>());
    let io2 = InOrder { stack: Vec::new(), cur: &t };
    println!("inorder filtered {:?}", io2.filter(|x| x % 20 != 0).map(|x| x / 5).collect::<Vec<_>>());

    println!("interleave {:?}", (1..4).interleave(100..106).collect::<Vec<_>>());
    println!("interleave chars {}", "abc".chars().interleave("XYZW".chars()).collect::<String>());
    println!("interleave nested {:?}", (0..3).interleave(10..12).interleave(vec![-1, -2, -3, -4].into_iter()).collect::<Vec<_>>());

    let data = [3, 8, 2, 9, 9, 1];
    println!("pairs {:?}", Pairs { s: &data }.map(|(a, b)| b - a).collect::<Vec<_>>());
    println!("pairs of one {:?}", Pairs { s: &data[..1] }.count());

    let ps = Primes { found: vec![], next: 2 };
    println!("primes {:?}", ps.skip(10).step_by(3).take(15).collect::<Vec<_>>());
    let twin: Vec<(u64, u64)> = Primes { found: vec![], next: 2 }
        .take(60)
        .collect::<Vec<_>>()
        .windows(2)
        .filter(|w| w[1] - w[0] == 2)
        .map(|w| (w[0], w[1]))
        .collect();
    println!("twin primes {:?}", twin);

    let mut state = (0u64, 1u64);
    let fib: Vec<u64> = iter::from_fn(|| {
        let r = state.0;
        state = (state.1, state.0 + state.1);
        if r > 5000 {
            None
        } else {
            Some(r)
        }
    })
    .collect();
    println!("from_fn fib {:?}", fib);
    let pow3: Vec<u64> = iter::successors(Some(1u64), |&x| x.checked_mul(3)).collect();
    println!("successors pow3 count {} last {:?}", pow3.len(), pow3.last());
    let halves: Vec<f64> = iter::successors(Some(bb(1000.0f64)), |&x| if x > 1.0 { Some(x / 2.5) } else { None }).collect();
    println!("successors halves {:?}", halves);
    let digits: Vec<u32> = iter::successors(Some(bb(9_081_726_354u64)), |&n| if n >= 10 { Some(n / 10) } else { None }).map(|n| (n % 10) as u32).collect();
    println!("digits reversed {:?}", digits);
    let mut k = 1;
    let squares: Vec<i32> = iter::repeat_with(|| {
        k += 1;
        k * k
    })
    .take(8)
    .collect();
    println!("repeat_with {:?}", squares);
    let framed: Vec<i32> = iter::once(-1).chain(squares.iter().cloned().filter(|x| x % 2 == 0)).chain(iter::once(-2)).chain(iter::empty()).collect();
    println!("once/empty {:?}", framed);
    println!("repeat {:?}", iter::repeat("ab").take(4).collect::<String>());
    let zipped: Vec<(char, i32)> = "wxyz".chars().zip(iter::repeat(7).enumerate().map(|(i, x)| x * i as i32)).collect();
    println!("zip repeat {:?}", zipped);
    // An iterator over a struct field mutated while being iterated via indices.
    let mut grid = vec![vec![0u32; 6]; 4];
    for (r, row) in grid.iter_mut().enumerate() {
        for (c, cell) in row.iter_mut().enumerate().rev() {
            *cell = (r * 10 + c) as u32 ^ (c as u32).pow(2);
        }
    }
    for row in &grid {
        println!("grid {:?}", row);
    }
    let col_sums: Vec<u32> = (0..6).map(|c| grid.iter().map(|row| row[c]).sum()).collect();
    println!("col sums {:?}", col_sums);
    let transposed: Vec<Vec<u32>> = (0..6).map(|c| grid.iter().map(|r| r[c]).collect()).collect();
    println!("transposed {:?}", transposed);
}
