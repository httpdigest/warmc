// Tail calls, which the corpus is built to emit: `-mcpu=...+tail_call` makes
// `@call(.always_tail, f, args)` a wasm `return_call`, and a call through a
// function pointer in tail position a `return_call_indirect`.  Without them
// the deep cases here would need a million stack frames and overflow.
//
// Covered: loops written as self tail recursion with accumulators (sum,
// factorial mod p, gcd, Collatz step counts, digit sums, fast power);
// mutual tail recursion between two and three functions (is_even / is_odd, a
// three-state divisibility automaton over a number's binary digits); a state
// machine whose states are functions in a table, each tail-calling the next
// through a run-time pointer, i.e. `return_call_indirect` with a different
// target each time; tail calls that permute their arguments between i64,
// f64, i32 and pointer parameters, which an engine's argument shuffling has
// to get right because the callee's frame replaces the caller's; calls in
// tail position between functions of different types, which may or may not
// become `return_call`; and `@call(.never_inline, ...)` / `@call(.always_inline, ...)`
// for the same functions, which must agree with the tail-call versions.
//
// LLVM turns self tail recursion into a loop whatever it is asked, so the
// mutually recursive functions are `noinline`: that keeps each one a function
// of its own and each tail call a real `return_call` between two of them.
// Depths go up to a million so that a `return_call` lowered as an ordinary
// call would run out of stack rather than pass by luck.  Inputs come through
// `w.hide`.
const std = @import("std");
const w = @import("wasi");

fn sumTo(n: u64, acc: u64) u64 {
    if (n == 0) return acc;
    return @call(.always_tail, sumTo, .{ n - 1, acc +% n });
}

fn factMod(n: u32, acc: u64, p: u64) u64 {
    if (n <= 1) return acc;
    return @call(.always_tail, factMod, .{ n - 1, acc * n % p, p });
}

noinline fn gcdT(a: u64, b: u64) u64 {
    if (b == 0) return a;
    return @call(.always_tail, gcdT, .{ b, a % b });
}

noinline fn collatz(n: u64, steps: u32) u32 {
    if (n == 1) return steps;
    if (n % 2 == 0) return @call(.always_tail, collatz, .{ n / 2, steps + 1 });
    return @call(.always_tail, collatz, .{ 3 * n + 1, steps + 1 });
}

fn digitSum(n: u64, acc: u32) u32 {
    if (n == 0) return acc;
    return @call(.always_tail, digitSum, .{ n / 10, acc + @as(u32, @intCast(n % 10)) });
}

fn powMod(base: u64, exp: u64, m: u64, acc: u64) u64 {
    if (exp == 0) return acc;
    const nacc = if (exp & 1 == 1) acc * base % m else acc;
    return @call(.always_tail, powMod, .{ base * base % m, exp >> 1, m, nacc });
}

noinline fn isEven(n: u32) bool {
    if (n == 0) return true;
    return @call(.always_tail, isOdd, .{n - 1});
}

noinline fn isOdd(n: u32) bool {
    if (n == 0) return false;
    return @call(.always_tail, isEven, .{n - 1});
}

// Divisibility by 3 of a number's binary digits, read high bit first: three
// states, each a function, each consuming one bit and tail-calling the next
// state.
noinline fn mod3s0(x: u64, bit: u7) u2 {
    if (bit == 0) return 0;
    const b = (x >> @intCast(bit - 1)) & 1;
    return if (b == 0) @call(.always_tail, mod3s0, .{ x, bit - 1 }) else @call(.always_tail, mod3s1, .{ x, bit - 1 });
}
noinline fn mod3s1(x: u64, bit: u7) u2 {
    if (bit == 0) return 1;
    const b = (x >> @intCast(bit - 1)) & 1;
    return if (b == 0) @call(.always_tail, mod3s2, .{ x, bit - 1 }) else @call(.always_tail, mod3s0, .{ x, bit - 1 });
}
noinline fn mod3s2(x: u64, bit: u7) u2 {
    if (bit == 0) return 2;
    const b = (x >> @intCast(bit - 1)) & 1;
    return if (b == 0) @call(.always_tail, mod3s1, .{ x, bit - 1 }) else @call(.always_tail, mod3s2, .{ x, bit - 1 });
}

// A table-driven machine: every state has the same signature and tail-calls
// the next through the table, so each step is a `return_call_indirect`.
const Vm = struct {
    code: []const u8,
    pc: usize,
    acc: i64,
    steps: u64,
    log: [64]u8,
    log_len: usize,
};

const Handler = *const fn (*Vm) i64;

const handlers = [_]Handler{ opHalt, opInc, opDbl, opNeg, opLoop, opMark, opDec };

fn dispatch(vm: *Vm) i64 {
    vm.steps += 1;
    const op = vm.code[vm.pc];
    vm.pc += 1;
    return @call(.always_tail, handlers[op], .{vm});
}
fn opHalt(vm: *Vm) i64 {
    return vm.acc;
}
fn opInc(vm: *Vm) i64 {
    vm.acc +%= 1;
    return @call(.always_tail, dispatch, .{vm});
}
fn opDbl(vm: *Vm) i64 {
    vm.acc *%= 2;
    return @call(.always_tail, dispatch, .{vm});
}
fn opNeg(vm: *Vm) i64 {
    vm.acc = -%vm.acc;
    return @call(.always_tail, dispatch, .{vm});
}
// Jump back to the start while acc is below a bound (code[pc] is the bound).
fn opLoop(vm: *Vm) i64 {
    const bound: i64 = vm.code[vm.pc];
    vm.pc += 1;
    if (vm.acc < bound * 1000) vm.pc = 0;
    return @call(.always_tail, dispatch, .{vm});
}
fn opMark(vm: *Vm) i64 {
    if (vm.log_len < vm.log.len) {
        vm.log[vm.log_len] = 'a' + @as(u8, @intCast(@mod(vm.acc, 26)));
        vm.log_len += 1;
    }
    return @call(.always_tail, dispatch, .{vm});
}
fn opDec(vm: *Vm) i64 {
    vm.acc -%= 3;
    return @call(.always_tail, dispatch, .{vm});
}

fn runVm(code: []const u8) void {
    var vm = Vm{ .code = code, .pc = 0, .acc = 0, .steps = 0, .log = undefined, .log_len = 0 };
    const r = dispatch(&vm);
    w.printf("vm result={d} steps={d} pc={d} log={s}\n", .{ r, vm.steps, vm.pc, vm.log[0..vm.log_len] });
}

// Tail calls that move arguments between parameter slots.  `.always_tail`
// needs the callee's type to match the caller's, so the three share one
// signature, but each passes its parameters on in a different arrangement
// (an i64 becomes the float, the float feeds the integer), so the outgoing
// arguments overlap the incoming ones they are computed from.
const Acc = struct { lo: u32, hi: u32, f: f64 };

noinline fn mixA(h: f64, x: i64, n: u32, acc: *Acc) f64 {
    if (n == 0) return acc.f + h + @as(f64, @floatFromInt(x));
    acc.lo +%= n;
    return @call(.always_tail, mixB, .{ @as(f64, @floatFromInt(n)) * 0.5, x ^ n, n - 1, acc });
}
noinline fn mixB(h: f64, x: i64, n: u32, acc: *Acc) f64 {
    acc.f = acc.f * 0.999 + h;
    return @call(.always_tail, mixC, .{ @as(f64, @floatFromInt(@rem(x, 1000))), x *% 3 +% 1, n, acc });
}
noinline fn mixC(h: f64, x: i64, n: u32, acc: *Acc) f64 {
    acc.hi = acc.hi *% 31 +% @as(u32, @truncate(@as(u64, @bitCast(x))));
    return @call(.always_tail, mixA, .{ h * 0.25, @rem(x, 1_000_003) + @as(i64, @intFromFloat(h)), n, acc });
}

// Calls in tail position between functions of different types, left to the
// compiler: `return_call` where it chooses to, a plain call where not.  Kept
// shallow so either is correct.
noinline fn shapeA(n: u32, x: i64) f64 {
    if (n == 0) return @floatFromInt(x);
    return shapeB(@as(f64, @floatFromInt(x)) / 3.0, n - 1);
}
noinline fn shapeB(f: f64, n: u32) f64 {
    if (n == 0) return f;
    return shapeC(n - 1, @intFromFloat(f * 7.0), @as(u8, @truncate(n)));
}
noinline fn shapeC(n: u32, x: i64, k: u8) f64 {
    return shapeA(n, x + k);
}

// The same sum by plain recursion, kept shallow, and called both never- and
// always-inlined.
fn sumRec(n: u64) u64 {
    if (n == 0) return 0;
    return n + sumRec(n - 1);
}

fn sq(x: u64) u64 {
    return x * x;
}

pub fn main() void {
    for ([_]u64{ 0, 1, 10, 1000, 1_000_000 }) |n| {
        w.printf("sumTo({d}) = {d}\n", .{ n, sumTo(w.hide(n), 0) });
    }
    for ([_]u32{ 1, 5, 20, 1000, 100_000 }) |n| {
        w.printf("factMod({d}) = {d}\n", .{ n, factMod(w.hide(n), 1, 1_000_000_007) });
    }
    const pairs = [_][2]u64{ .{ 48, 18 }, .{ 17, 5 }, .{ 1071, 462 }, .{ 0, 9 }, .{ 832040, 514229 }, .{ 1 << 40, 3 << 20 } };
    for (pairs) |p| {
        w.printf("gcd({d},{d}) = {d}\n", .{ p[0], p[1], gcdT(w.hide(p[0]), w.hide(p[1])) });
    }
    for ([_]u64{ 1, 2, 3, 7, 27, 97, 871, 6171, 77031, 837799 }) |n| {
        w.printf("collatz({d}) = {d}\n", .{ n, collatz(w.hide(n), 0) });
    }
    for ([_]u64{ 0, 9, 12345, 999999999, std.math.maxInt(u64) }) |n| {
        w.printf("digitSum({d}) = {d}\n", .{ n, digitSum(w.hide(n), 0) });
    }
    for ([_][2]u64{ .{ 2, 10 }, .{ 3, 200 }, .{ 7, 1_000_000_006 }, .{ 12345, 67890 } }) |p| {
        w.printf("powMod({d},{d}) = {d}\n", .{ p[0], p[1], powMod(w.hide(p[0]), w.hide(p[1]), 1_000_000_007, 1) });
    }
    for ([_]u32{ 0, 1, 2, 7, 100, 999_999, 1_000_000 }) |n| {
        w.printf("isEven({d}) = {} isOdd = {}\n", .{ n, isEven(w.hide(n)), isOdd(w.hide(n)) });
    }
    var x: u64 = w.hide(@as(u64, 1));
    for (0..24) |i| {
        const m = mod3s0(x, 64);
        w.printf("mod3({d}) = {d} (check {d})\n", .{ x, m, x % 3 });
        x = x *% 6364136223846793005 +% 1442695040888963407 +% i;
    }
    // inc inc dbl mark loop(5) dbl neg mark halt  /  a long loop
    runVm(w.hide(@as([]const u8, &.{ 1, 1, 2, 5, 4, 5, 2, 3, 5, 0 })));
    runVm(w.hide(@as([]const u8, &.{ 1, 1, 2, 6, 5, 4, 200, 3, 5, 0 })));
    runVm(w.hide(@as([]const u8, &.{ 1, 4, 255, 5, 0 })));
    for ([_]u32{ 0, 1, 3, 10, 1000, 300_000 }) |n| {
        var acc = Acc{ .lo = 0, .hi = 1, .f = 0 };
        const r = mixA(0.5, 12345, w.hide(n), &acc);
        w.printf("mix({d}) = {d:.6} lo={d} hi={d} f={d:.6}\n", .{ n, r, acc.lo, acc.hi, acc.f });
    }
    for ([_]u32{ 0, 1, 2, 5, 30, 61 }) |n| {
        w.printf("shape({d}) = {d:.6}\n", .{ n, shapeA(w.hide(n), 1000) });
    }
    for ([_]u64{ 0, 1, 100, 5000 }) |n| {
        const a = @call(.never_inline, sumRec, .{w.hide(n)});
        const b = @call(.always_inline, sq, .{w.hide(n)});
        const c = @call(.never_inline, sq, .{w.hide(n)});
        w.printf("sumRec({d})={d} tail={d} sq inline={d} outline={d}\n", .{ n, a, sumTo(n, 0), b, c });
    }
    w.puts("done\n");
}
