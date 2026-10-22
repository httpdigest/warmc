// Box, Rc, Weak, RefCell, Cell and their interplay: Rc::clone and the
// strong/weak counts at every step, Weak::upgrade before and after the last
// strong reference goes, Rc::try_unwrap / get_mut / make_mut (clone-on-write),
// Rc<RefCell<T>> shared mutation, RefCell::try_borrow_mut reporting a live
// borrow instead of panicking, Cell get/set/replace/take, a doubly linked list
// and a parent-pointer tree built from Rc + Weak, and Box<[T]> / Box<str>.
//
// Reference counts live in the allocation next to the value, and every clone
// and drop is an increment or decrement there; RefCell keeps a borrow flag the
// same way.  A miscompiled load/store ordering around those fields shows up as
// a wrong count or a wrong try_borrow answer long before it corrupts anything.

use std::cell::{Cell, RefCell};
use std::collections::BTreeMap;
use std::hint::black_box as bb;
use std::rc::{Rc, Weak};

#[derive(Debug)]
struct Node {
    name: String,
    parent: RefCell<Weak<Node>>,
    children: RefCell<Vec<Rc<Node>>>,
    visits: Cell<u32>,
}

impl Node {
    fn new(name: &str) -> Rc<Node> {
        Rc::new(Node { name: name.to_string(), parent: RefCell::new(Weak::new()), children: RefCell::new(vec![]), visits: Cell::new(0) })
    }
    fn add_child(parent: &Rc<Node>, child: Rc<Node>) {
        *child.parent.borrow_mut() = Rc::downgrade(parent);
        parent.children.borrow_mut().push(child);
    }
    fn path(&self) -> String {
        self.visits.set(self.visits.get() + 1);
        match self.parent.borrow().upgrade() {
            Some(p) => format!("{}/{}", p.path(), self.name),
            None => self.name.clone(),
        }
    }
    fn count(&self) -> usize {
        1 + self.children.borrow().iter().map(|c| c.count()).sum::<usize>()
    }
}

impl Drop for Node {
    fn drop(&mut self) {
        println!("drop node {}", self.name);
    }
}

// Doubly linked list: strong pointers forward, weak pointers back.
struct DNode {
    val: i32,
    next: Option<Rc<RefCell<DNode>>>,
    prev: Option<Weak<RefCell<DNode>>>,
}
struct DList {
    head: Option<Rc<RefCell<DNode>>>,
    tail: Option<Rc<RefCell<DNode>>>,
    len: usize,
}
impl DList {
    fn new() -> DList {
        DList { head: None, tail: None, len: 0 }
    }
    fn push_back(&mut self, v: i32) {
        let n = Rc::new(RefCell::new(DNode { val: v, next: None, prev: None }));
        match self.tail.take() {
            Some(t) => {
                n.borrow_mut().prev = Some(Rc::downgrade(&t));
                t.borrow_mut().next = Some(n.clone());
                self.tail = Some(n);
            }
            None => {
                self.head = Some(n.clone());
                self.tail = Some(n);
            }
        }
        self.len += 1;
    }
    fn push_front(&mut self, v: i32) {
        let n = Rc::new(RefCell::new(DNode { val: v, next: None, prev: None }));
        match self.head.take() {
            Some(h) => {
                h.borrow_mut().prev = Some(Rc::downgrade(&n));
                n.borrow_mut().next = Some(h);
                self.head = Some(n);
            }
            None => {
                self.tail = Some(n.clone());
                self.head = Some(n);
            }
        }
        self.len += 1;
    }
    fn pop_front(&mut self) -> Option<i32> {
        self.head.take().map(|h| {
            match h.borrow_mut().next.take() {
                Some(nx) => {
                    nx.borrow_mut().prev = None;
                    self.head = Some(nx);
                }
                None => {
                    self.tail = None;
                }
            }
            self.len -= 1;
            let v = h.borrow().val;
            v
        })
    }
    fn pop_back(&mut self) -> Option<i32> {
        self.tail.take().map(|t| {
            let prev = t.borrow_mut().prev.take().and_then(|w| w.upgrade());
            match prev {
                Some(p) => {
                    p.borrow_mut().next = None;
                    self.tail = Some(p);
                }
                None => {
                    self.head = None;
                }
            }
            self.len -= 1;
            let v = t.borrow().val;
            v
        })
    }
    fn forward(&self) -> Vec<i32> {
        let mut out = vec![];
        let mut cur = self.head.clone();
        while let Some(n) = cur {
            out.push(n.borrow().val);
            cur = n.borrow().next.clone();
        }
        out
    }
    fn backward(&self) -> Vec<i32> {
        let mut out = vec![];
        let mut cur = self.tail.clone();
        while let Some(n) = cur {
            out.push(n.borrow().val);
            cur = n.borrow().prev.as_ref().and_then(|w| w.upgrade());
        }
        out
    }
}

fn rc_counts() {
    let a = Rc::new(String::from("shared"));
    println!("new: strong {} weak {}", Rc::strong_count(&a), Rc::weak_count(&a));
    let b = Rc::clone(&a);
    let c = a.clone();
    let w = Rc::downgrade(&a);
    println!("3 clones + weak: strong {} weak {} ptr_eq {}", Rc::strong_count(&a), Rc::weak_count(&a), Rc::ptr_eq(&b, &c));
    drop(b);
    println!("drop b: strong {} upgrade {:?}", Rc::strong_count(&a), w.upgrade());
    drop(c);
    match Rc::try_unwrap(a) {
        Ok(s) => println!("try_unwrap ok {:?} weak now {:?}", s, w.upgrade()),
        Err(_) => println!("try_unwrap failed"),
    }
    let mut x = Rc::new(vec![1, 2, 3]);
    let y = x.clone();
    println!("get_mut with 2 owners: {:?}", Rc::get_mut(&mut x).is_some());
    Rc::make_mut(&mut x).push(4);
    println!("make_mut cloned: x {:?} y {:?} counts {} {}", x, y, Rc::strong_count(&x), Rc::strong_count(&y));
    drop(y);
    Rc::get_mut(&mut x).map(|v| v.push(5));
    println!("get_mut sole owner: {:?}", x);
    let before = Rc::as_ptr(&x);
    Rc::make_mut(&mut x).push(6);
    println!("make_mut in place same alloc {} {:?}", before == Rc::as_ptr(&x), x);
    let empty: Weak<i32> = Weak::new();
    println!("dangling weak upgrade {:?} strong {}", empty.upgrade(), empty.strong_count());
}

fn refcells() {
    let shared = Rc::new(RefCell::new(BTreeMap::new()));
    let writers: Vec<Rc<RefCell<BTreeMap<String, i32>>>> = (0..3).map(|_| shared.clone()).collect();
    for (i, wr) in writers.iter().enumerate() {
        for j in 0..4 {
            *wr.borrow_mut().entry(format!("k{}", (i + j) % 5)).or_insert(0) += (i * 10 + j) as i32;
        }
    }
    println!("shared map {:?} owners {}", shared.borrow(), Rc::strong_count(&shared));
    let cell = RefCell::new(vec![1, 2, 3]);
    {
        let r1 = cell.borrow();
        let r2 = cell.borrow();
        println!("two shared borrows {} {} try_mut ok {}", r1.len(), r2[0], cell.try_borrow_mut().is_ok());
    }
    {
        let mut m = cell.borrow_mut();
        m.push(4);
        println!("while mut borrowed: try_borrow ok {} try_mut ok {}", cell.try_borrow().is_ok(), cell.try_borrow_mut().is_ok());
    }
    println!("after: try_borrow ok {} value {:?}", cell.try_borrow().is_ok(), cell.borrow());
    let old = cell.replace(vec![9]);
    println!("replace old {:?} new {:?}", old, cell.borrow());
    cell.replace_with(|v| v.iter().map(|x| x * 2).chain(std::iter::once(7)).collect());
    let now = cell.borrow().clone();
    let taken = cell.take();
    println!("replace_with {:?} take {:?} after take {:?}", now, taken, cell.borrow());
    let swapped_a = RefCell::new(1);
    let swapped_b = RefCell::new(2);
    swapped_a.swap(&swapped_b);
    println!("refcell swap {} {}", swapped_a.borrow(), swapped_b.borrow());
    println!("into_inner {}", swapped_a.into_inner());
}

fn cells() {
    let c = Cell::new(bb(10));
    c.set(c.get() * 3);
    let old = c.replace(7);
    println!("cell old {} now {} take {} after {}", old, c.get(), c.take(), c.get());
    let counters: Vec<Cell<u32>> = (0..5).map(Cell::new).collect();
    for i in 0..20 {
        let k = (i * 7) % 5;
        counters[k].set(counters[k].get() + i as u32);
    }
    println!("cells {:?}", counters.iter().map(|c| c.get()).collect::<Vec<_>>());
    let a = Cell::new(1);
    let b = Cell::new(2);
    a.swap(&b);
    println!("swap {} {}", a.get(), b.get());
    let slice: &mut [i32] = &mut [1, 2, 3, 4];
    let cs = Cell::from_mut(slice).as_slice_of_cells();
    for w in cs.windows(2) {
        w[1].set(w[1].get() + w[0].get());
    }
    println!("prefix sums via cells {:?}", cs.iter().map(|c| c.get()).collect::<Vec<_>>());
}

fn boxes() {
    let b: Box<[u32]> = (1..=6).map(|x| x * x).collect();
    let s: Box<str> = "boxed str".into();
    println!("box slice {:?} len {} box str {} len {}", b, b.len(), s, s.len());
    let v = b.into_vec();
    println!("into_vec {:?}", v);
    let big = Box::new([7u64; 64]);
    println!("big box sum {}", big.iter().sum::<u64>());
    let mut bx = Box::new(5);
    *bx += 10;
    let raw = Box::into_raw(bx);
    let back = unsafe { Box::from_raw(raw) };
    println!("box roundtrip {}", back);
    let nested: Box<Box<Box<i32>>> = Box::new(Box::new(Box::new(-3)));
    println!("nested box {}", ***nested);
    let leaked: &'static mut Vec<i32> = Box::leak(Box::new(vec![1, 2]));
    leaked.push(3);
    println!("leaked {:?}", leaked);
}

fn main() {
    rc_counts();
    refcells();
    cells();
    boxes();

    let root = Node::new("root");
    {
        let usr = Node::new("usr");
        let bin = Node::new("bin");
        let local = Node::new("local");
        Node::add_child(&usr, local.clone());
        Node::add_child(&local, Node::new("share"));
        Node::add_child(&root, usr.clone());
        Node::add_child(&root, bin);
        println!("usr strong {} weak {}", Rc::strong_count(&usr), Rc::weak_count(&usr));
        println!("local path {}", local.path());
        let share = local.children.borrow()[0].clone();
        println!("share path {}", share.path());
        println!("visits root {} usr {} local {}", root.visits.get(), usr.visits.get(), local.visits.get());
    }
    println!("tree count {}", root.count());
    let detached = root.children.borrow_mut().remove(0);
    println!("detached {} has parent {}", detached.name, detached.parent.borrow().upgrade().is_some());
    drop(root);
    println!("after dropping root: detached parent alive {}", detached.parent.borrow().upgrade().is_some());
    println!("detached subtree count {}", detached.count());
    drop(detached);

    let mut dl = DList::new();
    for i in 0..5 {
        dl.push_back(i * 10);
        dl.push_front(-i);
    }
    println!("dlist fwd {:?} back {:?} len {}", dl.forward(), dl.backward(), dl.len);
    println!("pop {:?} {:?} {:?} {:?}", dl.pop_front(), dl.pop_back(), dl.pop_back(), dl.pop_front());
    println!("dlist fwd {:?} back {:?} len {}", dl.forward(), dl.backward(), dl.len);
    while let Some(v) = dl.pop_back() {
        print!("{} ", v);
    }
    println!("| empty fwd {:?} len {}", dl.forward(), dl.len);
}
