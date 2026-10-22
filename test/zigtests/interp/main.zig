// A small language, run two ways: a tree-walking evaluator and a bytecode VM.
//
// The source goes through a tokenizer (a switch over bytes, keywords looked up
// with std.StaticStringMap), a recursive-descent parser with precedence
// climbing that builds an AST of tagged-union nodes in an ArenaAllocator, and
// then either an evaluator that recurses over the tree or a compiler to a
// stack bytecode with patched forward jumps, run by a `switch` dispatch loop.
// Both print what the program prints and the two transcripts are compared.
// Each sample program -- factorial, Fibonacci, gcd, a prime count, Collatz
// lengths, nested loops, short-circuit logic -- prints its own results, and a
// handful of malformed programs check the parser's error reports.
//
// This is the shape of most interpreters a compiler sees: a hot loop around a
// many-armed switch (a `br_table`), operands on a software stack in memory,
// recursion over pointers to union payloads, and error unions threaded through
// every parse function.
const std = @import("std");
const w = @import("wasi");

// ---- tokens ----

const Tag = enum { int, ident, kw_let, kw_while, kw_if, kw_else, kw_print, lparen, rparen, lbrace, rbrace, semi, assign, plus, minus, star, slash, percent, bang, eq, ne, lt, le, gt, ge, andand, oror, eof };

const Token = struct { tag: Tag, start: u32, end: u32, value: i64 = 0 };

const keywords = std.StaticStringMap(Tag).initComptime(.{
    .{ "let", .kw_let },
    .{ "while", .kw_while },
    .{ "if", .kw_if },
    .{ "else", .kw_else },
    .{ "print", .kw_print },
});

const Error = error{ UnexpectedChar, UnexpectedToken, UndefinedVariable, TooManyVariables, OutOfMemory };

fn tokenize(src: []const u8, out: *std.ArrayList(Token), gpa: std.mem.Allocator) Error!void {
    var i: u32 = 0;
    while (true) {
        while (i < src.len and (src[i] == ' ' or src[i] == '\n' or src[i] == '\t')) i += 1;
        if (i >= src.len) break;
        const start = i;
        const c = src[i];
        var tag: Tag = undefined;
        var value: i64 = 0;
        switch (c) {
            '0'...'9' => {
                while (i < src.len and std.ascii.isDigit(src[i])) : (i += 1) value = value *% 10 +% (src[i] - '0');
                tag = .int;
            },
            'a'...'z', 'A'...'Z', '_' => {
                while (i < src.len and (std.ascii.isAlphanumeric(src[i]) or src[i] == '_')) i += 1;
                tag = keywords.get(src[start..i]) orelse .ident;
            },
            else => {
                const two = if (i + 1 < src.len) src[i .. i + 2] else src[i .. i + 1];
                const pairs = [_]struct { []const u8, Tag }{ .{ "==", .eq }, .{ "!=", .ne }, .{ "<=", .le }, .{ ">=", .ge }, .{ "&&", .andand }, .{ "||", .oror } };
                var found = false;
                for (pairs) |p| {
                    if (std.mem.eql(u8, two, p[0])) {
                        tag = p[1];
                        i += 2;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    tag = switch (c) {
                        '(' => .lparen,
                        ')' => .rparen,
                        '{' => .lbrace,
                        '}' => .rbrace,
                        ';' => .semi,
                        '=' => .assign,
                        '+' => .plus,
                        '-' => .minus,
                        '*' => .star,
                        '/' => .slash,
                        '%' => .percent,
                        '!' => .bang,
                        '<' => .lt,
                        '>' => .gt,
                        else => return error.UnexpectedChar,
                    };
                    i += 1;
                }
            },
        }
        try out.append(gpa, .{ .tag = tag, .start = start, .end = i, .value = value });
    }
    try out.append(gpa, .{ .tag = .eof, .start = i, .end = i });
}

// ---- AST ----

const BinOp = enum { add, sub, mul, div, mod, eq, ne, lt, le, gt, ge, land, lor };

const Expr = union(enum) {
    int: i64,
    variable: u8,
    neg: *Expr,
    not: *Expr,
    bin: struct { op: BinOp, l: *Expr, r: *Expr },
};

const Stmt = union(enum) {
    assign: struct { slot: u8, e: *Expr },
    print: *Expr,
    @"while": struct { cond: *Expr, body: []Stmt },
    @"if": struct { cond: *Expr, then: []Stmt, els: []Stmt },
};

const Parser = struct {
    toks: []const Token,
    src: []const u8,
    pos: usize = 0,
    gpa: std.mem.Allocator,
    names: [32][]const u8 = undefined,
    nvars: u8 = 0,
    err_pos: u32 = 0,

    fn peek(p: *Parser) Tag {
        return p.toks[p.pos].tag;
    }
    fn take(p: *Parser) Token {
        const t = p.toks[p.pos];
        if (t.tag != .eof) p.pos += 1;
        return t;
    }
    fn expect(p: *Parser, tag: Tag) Error!Token {
        if (p.peek() != tag) {
            p.err_pos = p.toks[p.pos].start;
            return error.UnexpectedToken;
        }
        return p.take();
    }
    fn slot(p: *Parser, name: []const u8, create: bool) Error!u8 {
        for (p.names[0..p.nvars], 0..) |n, i| {
            if (std.mem.eql(u8, n, name)) return @intCast(i);
        }
        if (!create) return error.UndefinedVariable;
        if (p.nvars == p.names.len) return error.TooManyVariables;
        p.names[p.nvars] = name;
        p.nvars += 1;
        return p.nvars - 1;
    }
    fn node(p: *Parser, e: Expr) Error!*Expr {
        const n = try p.gpa.create(Expr);
        n.* = e;
        return n;
    }

    fn block(p: *Parser) Error![]Stmt {
        _ = try p.expect(.lbrace);
        var list: std.ArrayList(Stmt) = .empty;
        while (p.peek() != .rbrace) {
            if (p.peek() == .eof) {
                p.err_pos = p.toks[p.pos].start;
                return error.UnexpectedToken;
            }
            try list.append(p.gpa, try p.stmt());
        }
        _ = p.take();
        return list.toOwnedSlice(p.gpa);
    }

    fn stmt(p: *Parser) Error!Stmt {
        const t = p.take();
        switch (t.tag) {
            .kw_let, .ident => {
                const name_tok = if (t.tag == .kw_let) try p.expect(.ident) else t;
                const s = try p.slot(p.src[name_tok.start..name_tok.end], t.tag == .kw_let);
                _ = try p.expect(.assign);
                const e = try p.expr(0);
                _ = try p.expect(.semi);
                return .{ .assign = .{ .slot = s, .e = e } };
            },
            .kw_print => {
                const e = try p.expr(0);
                _ = try p.expect(.semi);
                return .{ .print = e };
            },
            .kw_while => {
                const c = try p.expr(0);
                return .{ .@"while" = .{ .cond = c, .body = try p.block() } };
            },
            .kw_if => {
                const c = try p.expr(0);
                const then = try p.block();
                var els: []Stmt = &.{};
                if (p.peek() == .kw_else) {
                    _ = p.take();
                    els = try p.block();
                }
                return .{ .@"if" = .{ .cond = c, .then = then, .els = els } };
            },
            else => {
                p.err_pos = t.start;
                return error.UnexpectedToken;
            },
        }
    }

    fn binop(tag: Tag) ?struct { BinOp, u8 } {
        return switch (tag) {
            .oror => .{ .lor, 1 },
            .andand => .{ .land, 2 },
            .eq => .{ .eq, 3 },
            .ne => .{ .ne, 3 },
            .lt => .{ .lt, 4 },
            .le => .{ .le, 4 },
            .gt => .{ .gt, 4 },
            .ge => .{ .ge, 4 },
            .plus => .{ .add, 5 },
            .minus => .{ .sub, 5 },
            .star => .{ .mul, 6 },
            .slash => .{ .div, 6 },
            .percent => .{ .mod, 6 },
            else => null,
        };
    }

    fn expr(p: *Parser, min_prec: u8) Error!*Expr {
        var lhs = try p.unary();
        while (binop(p.peek())) |b| {
            if (b[1] <= min_prec) break;
            _ = p.take();
            const rhs = try p.expr(b[1]);
            lhs = try p.node(.{ .bin = .{ .op = b[0], .l = lhs, .r = rhs } });
        }
        return lhs;
    }

    fn unary(p: *Parser) Error!*Expr {
        const t = p.take();
        return switch (t.tag) {
            .int => p.node(.{ .int = t.value }),
            .ident => p.node(.{ .variable = p.slot(p.src[t.start..t.end], false) catch |e| {
                p.err_pos = t.start;
                return e;
            } }),
            .minus => p.node(.{ .neg = try p.unary() }),
            .bang => p.node(.{ .not = try p.unary() }),
            .lparen => {
                const e = try p.expr(0);
                _ = try p.expect(.rparen);
                return e;
            },
            else => {
                p.err_pos = t.start;
                return error.UnexpectedToken;
            },
        };
    }
};

// ---- tree walker ----

const Output = struct {
    buf: [4096]u8 = undefined,
    len: usize = 0,
    fn emit(o: *Output, v: i64) void {
        const s = std.mem.print(o.buf[o.len..], "{d} ", .{v}) catch return;
        o.len += s.len;
    }
};

fn arith(op: BinOp, a: i64, b: i64) i64 {
    return switch (op) {
        .add => a +% b,
        .sub => a -% b,
        .mul => a *% b,
        .div => if (b == 0) 0 else if (b == -1) -%a else @divTrunc(a, b),
        .mod => if (b == 0 or b == -1) 0 else @rem(a, b),
        .eq => @intFromBool(a == b),
        .ne => @intFromBool(a != b),
        .lt => @intFromBool(a < b),
        .le => @intFromBool(a <= b),
        .gt => @intFromBool(a > b),
        .ge => @intFromBool(a >= b),
        .land, .lor => unreachable,
    };
}

fn eval(e: *const Expr, vars: *[32]i64) i64 {
    return switch (e.*) {
        .int => |v| v,
        .variable => |s| vars[s],
        .neg => |x| -%eval(x, vars),
        .not => |x| @intFromBool(eval(x, vars) == 0),
        .bin => |b| switch (b.op) {
            .land => if (eval(b.l, vars) != 0) @intFromBool(eval(b.r, vars) != 0) else 0,
            .lor => if (eval(b.l, vars) != 0) 1 else @intFromBool(eval(b.r, vars) != 0),
            else => arith(b.op, eval(b.l, vars), eval(b.r, vars)),
        },
    };
}

fn exec(stmts: []const Stmt, vars: *[32]i64, out: *Output, steps: *u32) void {
    for (stmts) |s| {
        steps.* += 1;
        switch (s) {
            .assign => |a| vars[a.slot] = eval(a.e, vars),
            .print => |e| out.emit(eval(e, vars)),
            .@"while" => |wl| while (eval(wl.cond, vars) != 0) exec(wl.body, vars, out, steps),
            .@"if" => |f| if (eval(f.cond, vars) != 0) exec(f.then, vars, out, steps) else exec(f.els, vars, out, steps),
        }
    }
}

// ---- bytecode ----

const Op = enum(u8) { push, load, store, add, sub, mul, div, mod, eq, ne, lt, le, gt, ge, neg, not, jmp, jz, jnz_keep, jz_keep, print, pop, truthy, halt };

const Insn = struct { op: Op, arg: i64 = 0 };

const Compiler = struct {
    code: std.ArrayList(Insn) = .empty,
    gpa: std.mem.Allocator,

    fn emit(c: *Compiler, op: Op, arg: i64) Error!usize {
        try c.code.append(c.gpa, .{ .op = op, .arg = arg });
        return c.code.items.len - 1;
    }
    fn patch(c: *Compiler, at: usize) void {
        c.code.items[at].arg = @intCast(c.code.items.len);
    }
    fn expr(c: *Compiler, e: *const Expr) Error!void {
        switch (e.*) {
            .int => |v| _ = try c.emit(.push, v),
            .variable => |s| _ = try c.emit(.load, s),
            .neg => |x| {
                try c.expr(x);
                _ = try c.emit(.neg, 0);
            },
            .not => |x| {
                try c.expr(x);
                _ = try c.emit(.not, 0);
            },
            .bin => |b| switch (b.op) {
                // Short-circuit: leave the left value as the answer when it
                // decides, otherwise drop it and normalise the right one.
                .land, .lor => {
                    try c.expr(b.l);
                    _ = try c.emit(.truthy, 0);
                    const j = try c.emit(if (b.op == .land) .jz_keep else .jnz_keep, 0);
                    _ = try c.emit(.pop, 0);
                    try c.expr(b.r);
                    _ = try c.emit(.truthy, 0);
                    c.patch(j);
                },
                else => {
                    try c.expr(b.l);
                    try c.expr(b.r);
                    _ = try c.emit(switch (b.op) {
                        .add => .add,
                        .sub => .sub,
                        .mul => .mul,
                        .div => .div,
                        .mod => .mod,
                        .eq => .eq,
                        .ne => .ne,
                        .lt => .lt,
                        .le => .le,
                        .gt => .gt,
                        .ge => .ge,
                        .land, .lor => unreachable,
                    }, 0);
                },
            },
        }
    }
    fn stmts(c: *Compiler, ss: []const Stmt) Error!void {
        for (ss) |s| switch (s) {
            .assign => |a| {
                try c.expr(a.e);
                _ = try c.emit(.store, a.slot);
            },
            .print => |e| {
                try c.expr(e);
                _ = try c.emit(.print, 0);
            },
            .@"while" => |wl| {
                const top = c.code.items.len;
                try c.expr(wl.cond);
                const exit = try c.emit(.jz, 0);
                try c.stmts(wl.body);
                _ = try c.emit(.jmp, @intCast(top));
                c.patch(exit);
            },
            .@"if" => |f| {
                try c.expr(f.cond);
                const skip = try c.emit(.jz, 0);
                try c.stmts(f.then);
                if (f.els.len > 0) {
                    const over = try c.emit(.jmp, 0);
                    c.patch(skip);
                    try c.stmts(f.els);
                    c.patch(over);
                } else c.patch(skip);
            },
        };
    }
};

fn run(code: []const Insn, out: *Output) u32 {
    var stack: [64]i64 = undefined;
    var sp: usize = 0;
    var vars: [32]i64 = @splat(0);
    var pc: usize = 0;
    var executed: u32 = 0;
    while (true) {
        const in = code[pc];
        pc += 1;
        executed += 1;
        switch (in.op) {
            .push => {
                stack[sp] = in.arg;
                sp += 1;
            },
            .load => {
                stack[sp] = vars[@intCast(in.arg)];
                sp += 1;
            },
            .store => {
                sp -= 1;
                vars[@intCast(in.arg)] = stack[sp];
            },
            .add, .sub, .mul, .div, .mod, .eq, .ne, .lt, .le, .gt, .ge => {
                sp -= 1;
                const b = stack[sp];
                const a = stack[sp - 1];
                const op: BinOp = switch (in.op) {
                    .add => .add,
                    .sub => .sub,
                    .mul => .mul,
                    .div => .div,
                    .mod => .mod,
                    .eq => .eq,
                    .ne => .ne,
                    .lt => .lt,
                    .le => .le,
                    .gt => .gt,
                    .ge => .ge,
                    else => unreachable,
                };
                stack[sp - 1] = arith(op, a, b);
            },
            .neg => stack[sp - 1] = -%stack[sp - 1],
            .not => stack[sp - 1] = @intFromBool(stack[sp - 1] == 0),
            .truthy => stack[sp - 1] = @intFromBool(stack[sp - 1] != 0),
            .jmp => pc = @intCast(in.arg),
            .jz => {
                sp -= 1;
                if (stack[sp] == 0) pc = @intCast(in.arg);
            },
            .jz_keep => if (stack[sp - 1] == 0) {
                pc = @intCast(in.arg);
            },
            .jnz_keep => if (stack[sp - 1] != 0) {
                pc = @intCast(in.arg);
            },
            .pop => sp -= 1,
            .print => {
                sp -= 1;
                out.emit(stack[sp]);
            },
            .halt => return executed,
        }
    }
}

const programs = [_]struct { name: []const u8, src: []const u8 }{
    .{ .name = "factorial", .src =
    \\let n = 1; let f = 1;
    \\while n <= 20 { f = f * n; print f; n = n + 1; }
    },
    .{ .name = "fibonacci", .src =
    \\let a = 0; let b = 1; let i = 0;
    \\while i < 40 { print a; let t = a + b; a = b; b = t; i = i + 1; }
    },
    .{ .name = "gcd", .src =
    \\let x = 1071; let y = 462;
    \\while y != 0 { let t = x % y; x = y; y = t; }
    \\print x;
    \\let p = 2 * 3 * 5 * 7 * 11 * 13; let q = 7 * 11 * 17 * 19 * 23;
    \\while q { let t = p % q; p = q; q = t; } print p;
    },
    .{ .name = "primes", .src =
    \\let n = 2; let count = 0;
    \\while n < 300 {
    \\  let d = 2; let prime = 1;
    \\  while d * d <= n && prime { if n % d == 0 { prime = 0; } d = d + 1; }
    \\  if prime { count = count + 1; if count % 5 == 0 { print n; } }
    \\  n = n + 1;
    \\}
    \\print count;
    },
    .{ .name = "collatz", .src =
    \\let s = 1;
    \\while s <= 30 {
    \\  let n = s; let steps = 0;
    \\  while n != 1 { if n % 2 == 0 { n = n / 2; } else { n = 3 * n + 1; } steps = steps + 1; }
    \\  print steps; s = s + 1;
    \\}
    },
    .{ .name = "arith", .src =
    \\print 7 / 2; print -7 / 2; print 7 / -2; print -7 % 3; print 7 % -3;
    \\print 2 + 3 * 4 - 5; print (2 + 3) * (4 - 5); print -(-(3)); print !0 + !5;
    \\print 1 < 2 == 1; print 3 >= 3; print 4 > 5 || 6 > 5 && 1; print 0 && 1 / 0;
    \\print 9223372036854775807 + 1; print 1 / 0; print 5 % 0;
    },
    .{ .name = "nested", .src =
    \\let i = 0; let total = 0;
    \\while i < 12 {
    \\  let j = 0;
    \\  while j < i { if (i + j) % 3 == 0 || j == 1 { total = total + i * j; } else { total = total - 1; } j = j + 1; }
    \\  print total; i = i + 1;
    \\}
    },
    .{ .name = "bad char", .src = "let x = 5 $ 3;" },
    .{ .name = "missing semi", .src = "let x = 5 print x;" },
    .{ .name = "undefined var", .src = "let x = 5; print x + y;" },
    .{ .name = "unclosed block", .src = "let x = 1; while x { x = 0;" },
    .{ .name = "bad expr", .src = "print (1 + );" },
};

var heap: [256 * 1024]u8 = undefined;

pub fn main() void {
    var fba = std.heap.FixedBufferAllocator.init(&heap);
    for (programs) |prog| {
        var arena = std.heap.ArenaAllocator.init(fba.allocator());
        defer arena.deinit();
        const gpa = arena.allocator();
        const src = w.hide(prog.src);
        w.printf("== {s}\n", .{prog.name});
        var toks: std.ArrayList(Token) = .empty;
        tokenize(src, &toks, gpa) catch |e| {
            w.printf("  tokenize error: {s} after {d} tokens\n", .{ @errorName(e), toks.items.len });
            continue;
        };
        w.printf("  {d} tokens, first tags:", .{toks.items.len});
        for (toks.items[0..@min(8, toks.items.len)]) |t| w.printf(" {s}", .{@tagName(t.tag)});
        w.puts("\n");
        var p: Parser = .{ .toks = toks.items, .src = src, .gpa = gpa };
        var list: std.ArrayList(Stmt) = .empty;
        const ok = blk: {
            while (p.peek() != .eof) {
                const s = p.stmt() catch |e| {
                    w.printf("  parse error: {s} at byte {d} (\"{s}\")\n", .{ @errorName(e), p.err_pos, src[p.err_pos..@min(src.len, p.err_pos + 6)] });
                    break :blk false;
                };
                list.append(gpa, s) catch return;
            }
            break :blk true;
        };
        if (!ok) continue;
        w.printf("  {d} statements, {d} variables\n", .{ list.items.len, p.nvars });

        var vars: [32]i64 = @splat(0);
        var out1: Output = .{};
        var steps: u32 = 0;
        exec(list.items, &vars, &out1, &steps);
        w.printf("  tree ({d} stmts run): {s}\n", .{ steps, out1.buf[0..out1.len] });

        var c: Compiler = .{ .gpa = gpa };
        c.stmts(list.items) catch return;
        _ = c.emit(.halt, 0) catch return;
        var out2: Output = .{};
        const executed = run(c.code.items, &out2);
        w.printf("  vm ({d} insns, {d} run): {s}\n", .{ c.code.items.len, executed, out2.buf[0..out2.len] });
        w.printf("  agree={}\n", .{std.mem.eql(u8, out1.buf[0..out1.len], out2.buf[0..out2.len])});
        var listing: [512]u8 = undefined;
        var n: usize = 0;
        for (c.code.items[0..@min(c.code.items.len, 24)]) |in| {
            const s = std.mem.print(listing[n..], "{s}{s}", .{ @tagName(in.op), if (in.op == .push or in.op == .load or in.op == .store or in.op == .jmp or in.op == .jz or in.op == .jz_keep or in.op == .jnz_keep) "*" else "" }) catch break;
            n += s.len;
            if (n < listing.len) {
                listing[n] = ' ';
                n += 1;
            }
        }
        w.printf("  code: {s}\n", .{listing[0..n]});
    }
    w.puts("done\n");
}
