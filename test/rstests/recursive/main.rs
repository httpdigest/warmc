// Recursion and recursive data structures: a singly linked list of
// Option<Box<Node>> with push/pop/reverse/iter, a persistent cons list with
// Rc sharing tails, a binary search tree with insert/delete/height/
// traversals, an expression tree evaluated and simplified recursively, an
// arena-allocated graph with u32 indices (DFS, cycle detection, topological
// sort), plus classic recursive functions: Ackermann, Hanoi, permutations,
// N-queens, power set, flood fill, mutual recursion.
//
// Recursion is where the wasm shadow stack and the native stack both grow:
// every frame that has an address-taken local gets a slot on Rust's shadow
// stack in linear memory, and the wasm call itself becomes a native call with
// its own prologue.  Depths here reach a few thousand.

use std::hint::black_box as bb;
use std::rc::Rc;

// ---- singly linked list
struct Node {
    val: i32,
    next: Option<Box<Node>>,
}
struct List {
    head: Option<Box<Node>>,
    len: usize,
}
impl List {
    fn new() -> Self {
        List { head: None, len: 0 }
    }
    fn push(&mut self, v: i32) {
        let old = self.head.take();
        self.head = Some(Box::new(Node { val: v, next: old }));
        self.len += 1;
    }
    fn pop(&mut self) -> Option<i32> {
        self.head.take().map(|n| {
            self.head = n.next;
            self.len -= 1;
            n.val
        })
    }
    fn reverse(&mut self) {
        let mut prev: Option<Box<Node>> = None;
        let mut cur = self.head.take();
        while let Some(mut n) = cur {
            cur = n.next.take();
            n.next = prev;
            prev = Some(n);
        }
        self.head = prev;
    }
    fn iter(&self) -> ListIter<'_> {
        ListIter { cur: self.head.as_deref() }
    }
    fn sum_rec(n: &Option<Box<Node>>) -> i64 {
        match n {
            None => 0,
            Some(b) => b.val as i64 + List::sum_rec(&b.next),
        }
    }
    fn insert_sorted(&mut self, v: i32) {
        let mut cur = &mut self.head;
        while cur.as_ref().map_or(false, |n| n.val < v) {
            cur = &mut cur.as_mut().unwrap_or_else(|| unreachable!()).next;
        }
        let rest = cur.take();
        *cur = Some(Box::new(Node { val: v, next: rest }));
        self.len += 1;
    }
}
impl Drop for List {
    // Iterative drop so a long list does not recurse.
    fn drop(&mut self) {
        let mut cur = self.head.take();
        while let Some(mut n) = cur {
            cur = n.next.take();
        }
    }
}
struct ListIter<'a> {
    cur: Option<&'a Node>,
}
impl<'a> Iterator for ListIter<'a> {
    type Item = i32;
    fn next(&mut self) -> Option<i32> {
        self.cur.map(|n| {
            self.cur = n.next.as_deref();
            n.val
        })
    }
}

// ---- persistent cons list
enum Cons {
    Nil,
    Cell(i32, Rc<Cons>),
}
fn cons(v: i32, t: &Rc<Cons>) -> Rc<Cons> {
    Rc::new(Cons::Cell(v, t.clone()))
}
fn cons_vec(l: &Cons) -> Vec<i32> {
    match l {
        Cons::Nil => vec![],
        Cons::Cell(v, t) => {
            let mut r = vec![*v];
            r.extend(cons_vec(t));
            r
        }
    }
}

// ---- BST
#[derive(Debug)]
enum Bst {
    Empty,
    Node(Box<Bst>, i32, Box<Bst>),
}
impl Bst {
    fn insert(self, v: i32) -> Bst {
        match self {
            Bst::Empty => Bst::Node(Box::new(Bst::Empty), v, Box::new(Bst::Empty)),
            Bst::Node(l, x, r) => {
                if v < x {
                    Bst::Node(Box::new(l.insert(v)), x, r)
                } else if v > x {
                    Bst::Node(l, x, Box::new(r.insert(v)))
                } else {
                    Bst::Node(l, x, r)
                }
            }
        }
    }
    fn min(&self) -> Option<i32> {
        match self {
            Bst::Empty => None,
            Bst::Node(l, x, _) => l.min().or(Some(*x)),
        }
    }
    fn delete(self, v: i32) -> Bst {
        match self {
            Bst::Empty => Bst::Empty,
            Bst::Node(l, x, r) => {
                if v < x {
                    Bst::Node(Box::new(l.delete(v)), x, r)
                } else if v > x {
                    Bst::Node(l, x, Box::new(r.delete(v)))
                } else {
                    match (*l, *r) {
                        (Bst::Empty, rr) => rr,
                        (ll, Bst::Empty) => ll,
                        (ll, rr) => {
                            let m = rr.min().unwrap_or(x);
                            Bst::Node(Box::new(ll), m, Box::new(rr.delete(m)))
                        }
                    }
                }
            }
        }
    }
    fn height(&self) -> usize {
        match self {
            Bst::Empty => 0,
            Bst::Node(l, _, r) => 1 + l.height().max(r.height()),
        }
    }
    fn inorder(&self, out: &mut Vec<i32>) {
        if let Bst::Node(l, x, r) = self {
            l.inorder(out);
            out.push(*x);
            r.inorder(out);
        }
    }
    fn preorder(&self, out: &mut Vec<i32>) {
        if let Bst::Node(l, x, r) = self {
            out.push(*x);
            l.preorder(out);
            r.preorder(out);
        }
    }
    fn contains(&self, v: i32) -> bool {
        match self {
            Bst::Empty => false,
            Bst::Node(l, x, r) => v == *x || if v < *x { l.contains(v) } else { r.contains(v) },
        }
    }
}

// ---- expression tree with simplification
#[derive(Clone, Debug, PartialEq)]
enum E {
    N(i64),
    V(char),
    Add(Box<E>, Box<E>),
    Mul(Box<E>, Box<E>),
    Neg(Box<E>),
}
fn simplify(e: &E) -> E {
    match e {
        E::Add(a, b) => match (simplify(a), simplify(b)) {
            (E::N(0), x) | (x, E::N(0)) => x,
            (E::N(x), E::N(y)) => E::N(x + y),
            (x, y) => E::Add(Box::new(x), Box::new(y)),
        },
        E::Mul(a, b) => match (simplify(a), simplify(b)) {
            (E::N(0), _) | (_, E::N(0)) => E::N(0),
            (E::N(1), x) | (x, E::N(1)) => x,
            (E::N(x), E::N(y)) => E::N(x * y),
            (x, y) => E::Mul(Box::new(x), Box::new(y)),
        },
        E::Neg(a) => match simplify(a) {
            E::N(x) => E::N(-x),
            E::Neg(inner) => *inner,
            x => E::Neg(Box::new(x)),
        },
        other => other.clone(),
    }
}
fn show(e: &E) -> String {
    match e {
        E::N(n) => n.to_string(),
        E::V(c) => c.to_string(),
        E::Add(a, b) => format!("({} + {})", show(a), show(b)),
        E::Mul(a, b) => format!("{} * {}", show(a), show(b)),
        E::Neg(a) => format!("-{}", show(a)),
    }
}
fn eval(e: &E, x: i64) -> i64 {
    match e {
        E::N(n) => *n,
        E::V(_) => x,
        E::Add(a, b) => eval(a, x) + eval(b, x),
        E::Mul(a, b) => eval(a, x) * eval(b, x),
        E::Neg(a) => -eval(a, x),
    }
}
fn derive(e: &E) -> E {
    match e {
        E::N(_) => E::N(0),
        E::V(_) => E::N(1),
        E::Add(a, b) => E::Add(Box::new(derive(a)), Box::new(derive(b))),
        E::Mul(a, b) => E::Add(Box::new(E::Mul(Box::new(derive(a)), b.clone())), Box::new(E::Mul(a.clone(), Box::new(derive(b))))),
        E::Neg(a) => E::Neg(Box::new(derive(a))),
    }
}

// ---- arena graph
struct Graph {
    names: Vec<&'static str>,
    adj: Vec<Vec<u32>>,
}
impl Graph {
    fn add(&mut self, n: &'static str) -> u32 {
        self.names.push(n);
        self.adj.push(vec![]);
        (self.names.len() - 1) as u32
    }
    fn edge(&mut self, a: u32, b: u32) {
        self.adj[a as usize].push(b);
    }
    fn dfs(&self, u: u32, seen: &mut Vec<bool>, order: &mut Vec<u32>) {
        if seen[u as usize] {
            return;
        }
        seen[u as usize] = true;
        for &v in &self.adj[u as usize] {
            self.dfs(v, seen, order);
        }
        order.push(u);
    }
    fn topo(&self) -> Vec<&'static str> {
        let mut seen = vec![false; self.names.len()];
        let mut order = vec![];
        for u in 0..self.names.len() as u32 {
            self.dfs(u, &mut seen, &mut order);
        }
        order.iter().rev().map(|&i| self.names[i as usize]).collect()
    }
    fn has_cycle(&self) -> bool {
        fn visit(g: &Graph, u: usize, state: &mut Vec<u8>) -> bool {
            state[u] = 1;
            for &v in &g.adj[u] {
                let v = v as usize;
                if state[v] == 1 || (state[v] == 0 && visit(g, v, state)) {
                    return true;
                }
            }
            state[u] = 2;
            false
        }
        let mut state = vec![0u8; self.names.len()];
        (0..self.names.len()).any(|u| state[u] == 0 && visit(self, u, &mut state))
    }
}

// ---- classic recursive functions
fn ackermann(m: u64, n: u64) -> u64 {
    match (m, n) {
        (0, n) => n + 1,
        (m, 0) => ackermann(m - 1, 1),
        (m, n) => ackermann(m - 1, ackermann(m, n - 1)),
    }
}
fn hanoi(n: u32, from: char, to: char, via: char, moves: &mut Vec<(u32, char, char)>) {
    if n == 0 {
        return;
    }
    hanoi(n - 1, from, via, to, moves);
    moves.push((n, from, to));
    hanoi(n - 1, via, to, from, moves);
}
fn permutations(items: &mut Vec<char>, k: usize, out: &mut Vec<String>) {
    if k == items.len() {
        out.push(items.iter().collect());
        return;
    }
    for i in k..items.len() {
        items.swap(k, i);
        permutations(items, k + 1, out);
        items.swap(k, i);
    }
}
fn queens(n: usize, row: usize, cols: &mut Vec<usize>, count: &mut u32, first: &mut Option<Vec<usize>>) {
    if row == n {
        *count += 1;
        if first.is_none() {
            *first = Some(cols.clone());
        }
        return;
    }
    for c in 0..n {
        if cols.iter().enumerate().all(|(r, &cc)| cc != c && (row - r) != if c > cc { c - cc } else { cc - c }) {
            cols.push(c);
            queens(n, row + 1, cols, count, first);
            cols.pop();
        }
    }
}
fn power_set(xs: &[i32]) -> Vec<Vec<i32>> {
    match xs.split_first() {
        None => vec![vec![]],
        Some((&h, t)) => {
            let rest = power_set(t);
            let mut with: Vec<Vec<i32>> = rest.iter().map(|s| {
                let mut v = vec![h];
                v.extend(s);
                v
            }).collect();
            with.extend(rest);
            with
        }
    }
}
fn flood(g: &mut Vec<Vec<u8>>, r: usize, c: usize, from: u8, to: u8) -> usize {
    if r >= g.len() || c >= g[0].len() || g[r][c] != from {
        return 0;
    }
    g[r][c] = to;
    1 + flood(g, r + 1, c, from, to) + flood(g, r, c + 1, from, to) + if r > 0 { flood(g, r - 1, c, from, to) } else { 0 } + if c > 0 { flood(g, r, c - 1, from, to) } else { 0 }
}
fn is_even(n: u32) -> bool {
    if n == 0 {
        true
    } else {
        is_odd(n - 1)
    }
}
fn is_odd(n: u32) -> bool {
    if n == 0 {
        false
    } else {
        is_even(n - 1)
    }
}
fn depth_sum(n: u32) -> u64 {
    // Deep, non-tail recursion with a live value across the call.
    if n == 0 {
        0
    } else {
        let k = (n as u64 * 2654435761) % 1000;
        k + depth_sum(n - 1)
    }
}

fn main() {
    let mut l = List::new();
    for i in 0..10 {
        l.push(bb(i * i - 20));
    }
    println!("list {:?} len {} sum_rec {}", l.iter().collect::<Vec<_>>(), l.len, List::sum_rec(&l.head));
    l.reverse();
    println!("reversed {:?}", l.iter().collect::<Vec<_>>());
    println!("pop {:?} {:?} len {}", l.pop(), l.pop(), l.len);
    let mut s = List::new();
    for v in [5, 1, 9, 3, 7, 3, 0, 10] {
        s.insert_sorted(v);
    }
    println!("insert_sorted {:?}", s.iter().collect::<Vec<_>>());
    let mut long = List::new();
    for i in 0..5000 {
        long.push(i);
    }
    println!("long list sum {} max {:?}", long.iter().map(|x| x as i64).sum::<i64>(), long.iter().max());

    let nil = Rc::new(Cons::Nil);
    let base = cons(3, &cons(2, &cons(1, &nil)));
    let a = cons(10, &base);
    let b = cons(20, &cons(21, &base));
    println!("cons a {:?} b {:?} base shared by {}", cons_vec(&a), cons_vec(&b), Rc::strong_count(&base));

    let mut t = Bst::Empty;
    for v in [50, 30, 70, 20, 40, 60, 80, 35, 45, 65, 85, 10, 25, 42] {
        t = t.insert(bb(v));
    }
    let mut io = vec![];
    t.inorder(&mut io);
    let mut po = vec![];
    t.preorder(&mut po);
    println!("bst inorder {:?}", io);
    println!("bst preorder {:?} height {} min {:?}", po, t.height(), t.min());
    for d in [20, 30, 50, 99, 10] {
        t = t.delete(d);
        let mut v = vec![];
        t.inorder(&mut v);
        let mut p = vec![];
        t.preorder(&mut p);
        println!("delete {} -> inorder {:?} preorder {:?} height {}", d, v, p, t.height());
    }
    println!("contains {:?}", [35, 50, 85, 1].iter().map(|&x| t.contains(x)).collect::<Vec<_>>());
    let mut skew = Bst::Empty;
    for i in 0..300 {
        skew = skew.insert(i);
    }
    println!("skewed height {}", skew.height());

    let x = || Box::new(E::V('x'));
    let n = |k| Box::new(E::N(k));
    let exprs = vec![
        E::Add(n(0), Box::new(E::Mul(n(1), x()))),
        E::Mul(Box::new(E::Add(n(2), n(3))), Box::new(E::Add(x(), n(0)))),
        E::Neg(Box::new(E::Neg(Box::new(E::Mul(x(), x()))))),
        E::Add(Box::new(E::Mul(n(3), Box::new(E::Mul(x(), x())))), Box::new(E::Add(Box::new(E::Mul(n(-2), x())), n(7)))),
        E::Mul(n(0), Box::new(E::Add(x(), n(99)))),
    ];
    for e in &exprs {
        let s = simplify(e);
        let d = simplify(&derive(e));
        println!("expr {} => {} ; d/dx => {} ; at 3: {} {} {}", show(e), show(&s), show(&d), eval(e, 3), eval(&s, 3), eval(&d, 3));
    }

    let mut g = Graph { names: vec![], adj: vec![] };
    let names = ["shirt", "tie", "jacket", "belt", "pants", "shoes", "socks", "undershorts", "watch"];
    let ids: Vec<u32> = names.iter().map(|n| g.add(n)).collect();
    for (a, b) in [(7, 4), (7, 5), (4, 5), (4, 3), (0, 3), (0, 1), (1, 2), (3, 2), (6, 5)] {
        g.edge(ids[a], ids[b]);
    }
    println!("topo {:?} cycle {}", g.topo(), g.has_cycle());
    g.edge(ids[2], ids[0]);
    println!("after back edge cycle {}", g.has_cycle());

    for (m, nn) in [(0, 0), (1, 5), (2, 3), (2, 10), (3, 3), (3, 6)] {
        println!("ackermann({}, {}) = {}", m, nn, ackermann(bb(m), bb(nn)));
    }
    let mut moves = vec![];
    hanoi(4, 'A', 'C', 'B', &mut moves);
    println!("hanoi 4: {} moves {:?}", moves.len(), &moves[..7]);
    let mut big_moves = vec![];
    hanoi(12, 'A', 'C', 'B', &mut big_moves);
    println!("hanoi 12: {} moves, disk-1 moves {}", big_moves.len(), big_moves.iter().filter(|m| m.0 == 1).count());
    let mut items = vec!['a', 'b', 'c', 'd'];
    let mut perms = vec![];
    permutations(&mut items, 0, &mut perms);
    println!("perms {} {:?}", perms.len(), perms);
    for nq in 1..=8 {
        let mut count = 0;
        let mut first = None;
        queens(nq, 0, &mut vec![], &mut count, &mut first);
        println!("queens {} -> {} first {:?}", nq, count, first);
    }
    let ps = power_set(&[1, 2, 3, 4]);
    println!("power set {} {:?}", ps.len(), ps);
    let mut grid: Vec<Vec<u8>> = ["..##....", ".#..#.#.", ".#..#...", "..##..##", "#......."].iter().map(|r| r.bytes().collect()).collect();
    let filled = flood(&mut grid, 0, 0, b'.', b'o');
    println!("flood filled {}", filled);
    for row in &grid {
        println!("  {}", String::from_utf8_lossy(row));
    }
    println!("even/odd {:?}", [0, 1, 10, 777, 1000].iter().map(|&k| (is_even(k), is_odd(k))).collect::<Vec<_>>());
    println!("depth_sum 3000 {}", depth_sum(bb(3000)));
}
