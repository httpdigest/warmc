// Virtual dispatch through `Box<dyn Trait>`: a vtable load, then an indirect
// call that lands somewhere different from one element to the next.
//
// The counterpart of gobench/iface and asbench/virtual.  Rust's is the leanest
// of the three -- a trait object is two words and the vtable is static, with no
// interface-table search of the kind Go does -- so this row is close to the cost
// of `call_indirect` itself: the type check against the signature, and a branch
// the predictor cannot learn because the concrete type alternates.
//
// The shapes are interleaved rather than grouped, which is the point.  Grouped,
// the indirect branch predicts perfectly and the row measures the call overhead
// alone; interleaved, it mispredicts and the row measures what a real
// polymorphic call site costs.

trait Shape {
    fn area(&self) -> f64;
    fn sides(&self) -> u32;
}

struct Circle {
    r: f64,
}
struct Rect {
    w: f64,
    h: f64,
}
struct Tri {
    b: f64,
    h: f64,
}

impl Shape for Circle {
    fn area(&self) -> f64 {
        3.14159265358979 * self.r * self.r
    }
    fn sides(&self) -> u32 {
        0
    }
}
impl Shape for Rect {
    fn area(&self) -> f64 {
        self.w * self.h
    }
    fn sides(&self) -> u32 {
        4
    }
}
impl Shape for Tri {
    fn area(&self) -> f64 {
        0.5 * self.b * self.h
    }
    fn sides(&self) -> u32 {
        3
    }
}

#[inline(never)]
fn total(v: &[Box<dyn Shape>]) -> (f64, u32) {
    let mut a = 0.0f64;
    let mut n = 0u32;
    for s in v {
        a += s.area();
        n = n.wrapping_add(s.sides());
    }
    (a, n)
}

fn main() {
    let mut v: Vec<Box<dyn Shape>> = Vec::new();
    for i in 0..300u32 {
        let x = (i % 7) as f64 + 1.0;
        match i % 3 {
            0 => v.push(Box::new(Circle { r: x })),
            1 => v.push(Box::new(Rect { w: x, h: x + 1.0 })),
            _ => v.push(Box::new(Tri { b: x, h: x + 2.0 })),
        }
    }
    let mut a = 0.0f64;
    let mut n = 0u32;
    for _ in 0..320000u32 {
        let (da, dn) = total(std::hint::black_box(&v));
        a += da;
        n = n.wrapping_add(dn);
    }
    println!("a = {} n = {}", a.to_bits(), n);
}
