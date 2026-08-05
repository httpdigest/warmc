// A `match` over an enum with a payload: Rust's tagged union, which is a
// discriminant load, a jump table, and a payload read at an offset that depends
// on which arm was taken.
//
// This is the shape that becomes `br_table`, and the one asbench/switchtab
// measures from the AssemblyScript side -- the global-promotion work cost 3.4%
// there and the loop-close work gave 4% back, so the row is sensitive to both.
// Rust reaches it differently: the discriminant is often packed into spare bits
// rather than stored separately, so the load is a mask and a shift as well.
//
// Eight arms, so a jump table is worth building; and the sequence of tags is
// data rather than a pattern, so the indirect branch mispredicts.

enum Op {
    Add(u64),
    Sub(u64),
    Mul(u64),
    Xor(u64),
    Rotate(u32),
    Negate,
    Clamp { lo: u64, hi: u64 },
    Nop,
}

#[inline(never)]
fn apply(x: u64, op: &Op) -> u64 {
    match op {
        Op::Add(k) => x.wrapping_add(*k),
        Op::Sub(k) => x.wrapping_sub(*k),
        Op::Mul(k) => x.wrapping_mul(*k | 1),
        Op::Xor(k) => x ^ *k,
        Op::Rotate(k) => x.rotate_left(*k % 64),
        Op::Negate => !x,
        Op::Clamp { lo, hi } => x.max(*lo).min(*hi),
        Op::Nop => x,
    }
}

fn main() {
    let mut ops: Vec<Op> = Vec::new();
    for i in 0..512u32 {
        let k = (i as u64).wrapping_mul(2654435761) | 1;
        ops.push(match i % 8 {
            0 => Op::Add(k),
            1 => Op::Sub(k),
            2 => Op::Mul(k),
            3 => Op::Xor(k),
            4 => Op::Rotate(i),
            5 => Op::Negate,
            6 => Op::Clamp { lo: k >> 8, hi: k << 8 | 0xffff },
            _ => Op::Nop,
        });
    }
    // Shuffled by a stride coprime with the length, so consecutive iterations do
    // not walk the tags in order.
    let order: Vec<usize> = (0..512).map(|i| (i * 149 + 7) % 512).collect();
    let mut x: u64 = 12345;
    for _ in 0..270000u32 {
        for &i in &order {
            x = apply(x, &ops[i]);
        }
    }
    println!("x = {}", x);
}
