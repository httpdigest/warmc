// `Rc<RefCell<T>>`: reference counting and a borrow flag checked at run time.
//
// This is shared mutable state the way Rust spells it, and it is the closest
// thing in this corpus to what a garbage-collected producer does implicitly --
// except that here every count is a load, an add and a store in the module's own
// code, and every borrow is a flag test with a panic path behind it.  So the row
// is a direct measurement of what the other producers hide inside their
// collectors: gobench and asbench pay for reachability, and this pays per
// operation instead.
//
// Both halves are load-bearing.  A clone-and-drop pair is two counter updates
// that warmc cannot eliminate without knowing they cancel, and `borrow_mut` is a
// check whose failure path is cold and should be laid out as such.

use std::cell::RefCell;
use std::rc::Rc;

struct Cell {
    v: u64,
    n: u64,
}

#[inline(never)]
fn bump(c: &Rc<RefCell<Cell>>) -> u64 {
    // A clone and a drop, so the strong count goes up and back down.
    let alias = Rc::clone(c);
    {
        let mut b = alias.borrow_mut();
        b.v = b.v.wrapping_mul(2654435761).wrapping_add(1);
        b.n += 1;
    }
    // The borrow ends here; the count drops when `alias` does.
    let r = alias.borrow().v;
    r
}

#[inline(never)]
fn total(v: &[Rc<RefCell<Cell>>]) -> u64 {
    let mut s = 0u64;
    for c in v {
        let b = c.borrow();
        s = s.wrapping_add(b.v ^ b.n);
    }
    s
}

fn main() {
    let cells: Vec<Rc<RefCell<Cell>>> = (0..200)
        .map(|i| Rc::new(RefCell::new(Cell { v: i as u64 | 1, n: 0 })))
        .collect();
    let mut s: u64 = 0;
    for r in 0..480000u32 {
        for c in &cells {
            s = s.wrapping_add(bump(std::hint::black_box(c)));
        }
        if r % 16 == 0 {
            s = s.wrapping_add(total(std::hint::black_box(&cells)));
        }
    }
    println!("s = {}", s);
}
