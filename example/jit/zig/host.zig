// A Zig host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in Zig, calls its exports and reads its memory, through
// the standard WebAssembly C API (wasm.h), which build.zig translates to Zig
// as it is.
//
//   cd example/jit/zig && zig build -Doptimize=ReleaseSafe
//   zig-out/bin/host ../guest.wat
const std = @import("std");
const c = @import("c");

var memory: ?*c.wasm_memory_t = null; // the guest's, once it is instantiated

// host.log(ptr, len): the bytes the guest points at, printed
fn hostLog(args: [*c]const c.wasm_val_vec_t, results: [*c]c.wasm_val_vec_t) callconv(.c) ?*c.wasm_trap_t {
    _ = results;
    const ptr: u32 = @bitCast(args.*.data[0].of.i32);
    const len: u32 = @bitCast(args.*.data[1].of.i32);
    const base: [*]const u8 = @ptrCast(c.wasm_memory_data(memory));
    print("log: {s}\n", .{base[ptr .. ptr + len]});
    return null;
}

fn print(comptime fmt: []const u8, args: anytype) void {
    var buf: [256]u8 = undefined;
    const s = std.fmt.bufPrint(&buf, fmt, args) catch return;
    _ = std.c.write(1, s.ptr, s.len);
}

// the instance's export named name
fn exportNamed(module: ?*const c.wasm_module_t, exports: *const c.wasm_extern_vec_t, name: []const u8) ?*c.wasm_extern_t {
    var types: c.wasm_exporttype_vec_t = undefined;
    c.wasm_module_exports(module, &types);
    defer c.wasm_exporttype_vec_delete(&types);
    for (0..types.size) |i| {
        const n = c.wasm_exporttype_name(types.data[i]);
        if (std.mem.eql(u8, n.*.data[0..n.*.size], name)) return exports.data[i];
    }
    return null;
}

// calls f with args, results to results: the trap's message, or null
fn call(f: ?*c.wasm_func_t, args: []c.wasm_val_t, results: []c.wasm_val_t, msg: *c.wasm_message_t) bool {
    var av = c.wasm_val_vec_t{ .size = args.len, .data = args.ptr };
    var rv = c.wasm_val_vec_t{ .size = results.len, .data = results.ptr };
    const trap = c.wasm_func_call(f, &av, &rv) orelse return false;
    c.wasm_trap_message(trap, msg);
    c.wasm_trap_delete(trap);
    return true;
}

fn i32Val(v: i32) c.wasm_val_t {
    return .{ .kind = c.WASM_I32, .of = .{ .i32 = v } };
}

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    const argv = try init.minimal.args.toSlice(init.arena.allocator());
    const path = if (argv.len > 1) argv[1] else "example/jit/guest.wat";
    const wat = try std.Io.Dir.cwd().readFileAlloc(init.io, path, allocator, .unlimited);
    defer allocator.free(wat);

    const engine = c.wasm_engine_new();
    defer c.wasm_engine_delete(engine);
    const store = c.wasm_store_new(engine);
    defer c.wasm_store_delete(store);

    var binary: c.wasm_byte_vec_t = undefined;
    var err: c.wasm_message_t = undefined;
    if (!c.warm_wat2wasm(wat.ptr, wat.len, &binary, &err)) {
        print("{s}\n", .{err.data[0..err.size]});
        return error.Assemble;
    }
    defer c.wasm_byte_vec_delete(&binary);
    const module = c.warm_module_new(store, &binary, &err) orelse {
        print("{s}\n", .{err.data[0..err.size]});
        return error.Compile;
    };
    defer c.wasm_module_delete(module);

    // host.log, of type (i32, i32) -> (), calling hostLog
    var params: c.wasm_valtype_vec_t = undefined;
    var results: c.wasm_valtype_vec_t = undefined;
    var kinds = [_]?*c.wasm_valtype_t{ c.wasm_valtype_new(c.WASM_I32), c.wasm_valtype_new(c.WASM_I32) };
    c.wasm_valtype_vec_new(&params, 2, &kinds);
    c.wasm_valtype_vec_new_empty(&results);
    const log_type = c.wasm_functype_new(&params, &results);
    const log_func = c.wasm_func_new(store, log_type, hostLog);
    c.wasm_functype_delete(log_type);
    defer c.wasm_func_delete(log_func);

    var imports = [_]?*c.wasm_extern_t{c.wasm_func_as_extern(log_func)};
    var import_vec = c.wasm_extern_vec_t{ .size = imports.len, .data = &imports };
    const instance = c.wasm_instance_new(store, module, &import_vec, null) orelse return error.Instantiate;
    defer c.wasm_instance_delete(instance);
    var exports: c.wasm_extern_vec_t = undefined;
    c.wasm_instance_exports(instance, &exports);
    defer c.wasm_extern_vec_delete(&exports);
    memory = c.wasm_extern_as_memory(exportNamed(module, &exports, "memory"));
    const greet = c.wasm_extern_as_func(exportNamed(module, &exports, "greet"));
    const fib = c.wasm_extern_as_func(exportNamed(module, &exports, "fib"));
    const div = c.wasm_extern_as_func(exportNamed(module, &exports, "div"));
    const peek = c.wasm_extern_as_func(exportNamed(module, &exports, "peek"));

    var msg: c.wasm_message_t = undefined;
    _ = call(greet, &.{}, &.{}, &msg);

    var r = [_]c.wasm_val_t{undefined};
    var fa = [_]c.wasm_val_t{.{ .kind = c.WASM_I64, .of = .{ .i64 = 90 } }};
    _ = call(fib, &fa, &r, &msg);
    print("fib(90) = {d}\n", .{r[0].of.i64});

    for ([_][2]i32{ .{ 7, 2 }, .{ 1, 0 } }) |p| {
        var da = [_]c.wasm_val_t{ i32Val(p[0]), i32Val(p[1]) };
        if (call(div, &da, &r, &msg)) {
            print("div({d}, {d}) trapped: {s}\n", .{ p[0], p[1], std.mem.sliceTo(msg.data, 0) });
            c.wasm_byte_vec_delete(&msg);
        } else print("div({d}, {d}) = {d}\n", .{ p[0], p[1], r[0].of.i32 });
    }
    for ([_]i32{ 16, 65536 }) |addr| {
        var pa = [_]c.wasm_val_t{i32Val(addr)};
        if (call(peek, &pa, &r, &msg)) {
            print("peek({d}) trapped: {s}\n", .{ addr, std.mem.sliceTo(msg.data, 0) });
            c.wasm_byte_vec_delete(&msg);
        } else print("peek({d}) = {d}\n", .{ addr, r[0].of.i32 });
    }
}
