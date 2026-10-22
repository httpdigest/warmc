// A ledger replayed from a stream of tagged unions.
//
// `Op` is a `union(enum)` with six variants of different shapes, from a bare tag
// to a three-field struct, and every op goes through one `switch` that binds
// the payload.  Zig lays each one out in twelve bytes, payload first and the
// tag byte at offset 8.  The loop loads the payload's first word and halfword
// before it knows the variant, dispatches on the tag with a six-way
// `br_table`, and each arm then cuts its own fields out of those two values
// with `i32.and 65535` and `i32.shr_u 16`.  So this is the interp kernel's
// indirect branch without its calls: one unpredictable jump a record (the
// variants come at random, 5:4:4:1:1:1), into arms of a few loads and stores
// each.
const w = @import("wasi");

const Acct = u16;
const N_ACCTS = 1024;

const Op = union(enum) {
    deposit: struct { acct: Acct, amount: u32 },
    withdraw: struct { acct: Acct, amount: u32 },
    transfer: struct { from: Acct, to: Acct, amount: u32 },
    interest: struct { acct: Acct, bps: u16 },
    freeze: Acct,
    audit,
};

var ops: [32768]Op = undefined;
var balance: [N_ACCTS]u64 = undefined;
var frozen: [N_ACCTS]bool = undefined;

const Stats = struct {
    bounced: u64 = 0,
    blocked: u64 = 0,
    audits: u64 = 0,
    audit_sum: u64 = 0,
};

fn apply(op: Op, st: *Stats) void {
    switch (op) {
        .deposit => |d| {
            if (frozen[d.acct]) {
                st.blocked += 1;
            } else balance[d.acct] += d.amount;
        },
        .withdraw => |d| {
            if (frozen[d.acct]) {
                st.blocked += 1;
            } else if (balance[d.acct] >= d.amount) {
                balance[d.acct] -= d.amount;
            } else st.bounced += 1;
        },
        .transfer => |t| {
            if (frozen[t.from] or frozen[t.to]) {
                st.blocked += 1;
            } else if (balance[t.from] >= t.amount) {
                balance[t.from] -= t.amount;
                balance[t.to] += t.amount;
            } else st.bounced += 1;
        },
        .interest => |i| balance[i.acct] += balance[i.acct] * i.bps / 10000,
        .freeze => |a| frozen[a] = !frozen[a],
        .audit => {
            st.audits += 1;
            st.audit_sum +%= balance[@intCast(st.audits % N_ACCTS)];
        },
    }
}

fn gen(x: u32) Op {
    const a: Acct = @intCast((x >> 8) % N_ACCTS);
    const b: Acct = @intCast((x >> 18) % N_ACCTS);
    const amount = (x >> 4) & 0x3ff;
    return switch (x % 16) {
        0...4 => .{ .deposit = .{ .acct = a, .amount = amount * 3 } },
        5...8 => .{ .withdraw = .{ .acct = a, .amount = amount } },
        9...12 => .{ .transfer = .{ .from = a, .to = b, .amount = amount } },
        13 => .{ .interest = .{ .acct = a, .bps = @intCast(amount % 50) } },
        14 => .{ .freeze = a },
        else => .audit,
    };
}

pub fn main() void {
    var x: u32 = 31337;
    for (&ops) |*op| {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        op.* = gen(x);
    }
    @memset(&balance, 1000);
    @memset(&frozen, false);
    var st: Stats = .{};
    var r: u32 = 0;
    while (r < 450) : (r += 1) {
        for (&ops) |op| apply(op, &st);
        // One op rewritten per pass, so no pass is the one before it.
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        ops[x % ops.len] = gen(x);
    }
    var sum: u64 = 0;
    for (&balance, 0..) |b, i| sum +%= b *% (i + 1);
    w.print("total = ", sum +% st.bounced *% 7 +% st.blocked *% 13 +% st.audits *% 17 +% st.audit_sum);
}
