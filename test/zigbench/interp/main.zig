// A bytecode interpreter dispatched by tail call.
//
// This is the kernel the corpus exists for.  `@call(.always_tail, ...)` through
// a table of handlers is what LLVM cannot turn back into a loop, so it comes out
// as `return_call_indirect` -- and across the other 133 modules in the six
// corpora there is not one tail call of any kind.  warmc implements the
// proposal; before this, nothing measured it.
const w = @import("wasi");

const Handler = *const fn (pc: u32, acc: u64, fuel: u32) void;
var result: u64 = 0;

const code = [16]u8{ 0, 1, 2, 3, 1, 0, 4, 2, 3, 1, 4, 0, 2, 1, 3, 4 };

fn opAdd(pc: u32, acc: u64, fuel: u32) void {
    return @call(.always_tail, step, .{ pc + 1, acc +% 0x9e3779b9, fuel });
}
fn opMul(pc: u32, acc: u64, fuel: u32) void {
    return @call(.always_tail, step, .{ pc + 1, acc *% 0x2545f491, fuel });
}
fn opXor(pc: u32, acc: u64, fuel: u32) void {
    return @call(.always_tail, step, .{ pc + 1, acc ^ (acc >> 29), fuel });
}
fn opRot(pc: u32, acc: u64, fuel: u32) void {
    return @call(.always_tail, step, .{ pc + 1, (acc << 7) | (acc >> 57), fuel });
}
fn opMix(pc: u32, acc: u64, fuel: u32) void {
    return @call(.always_tail, step, .{ pc + 1, acc ^ (acc << 13), fuel });
}

const table = [5]Handler{ opAdd, opMul, opXor, opRot, opMix };

fn step(pc: u32, acc: u64, fuel: u32) void {
    if (fuel == 0) {
        result = acc;
        return;
    }
    const op = code[pc & 15];
    return @call(.always_tail, table[op], .{ pc, acc, fuel - 1 });
}

pub fn main() void {
    var total: u64 = 0;
    var r: u32 = 0;
    while (r < 480) : (r += 1) {
        step(r, r, 300000);
        total +%= result;
    }
    w.print("total = ", total);
}
