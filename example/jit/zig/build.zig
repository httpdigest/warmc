// Builds host.zig against libwarmc: wasm.h and warm.h translated to Zig as
// the module `c`, the library linked from the tree's build/.
//
//   cd example/jit/zig && zig build -Doptimize=ReleaseSafe
//   zig-out/bin/host ../guest.wat
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const include = b.path("../../../include");
    const lib_dir = b.path("../../../build");

    const c = b.addTranslateC(.{
        .root_source_file = b.path("c.h"),
        .target = target,
        .optimize = optimize,
    });
    c.addIncludePath(include);

    const exe = b.addExecutable(.{
        .name = "host",
        .root_module = b.createModule(.{
            .root_source_file = b.path("host.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .imports = &.{.{ .name = "c", .module = c.createModule() }},
        }),
    });
    exe.root_module.addLibraryPath(lib_dir);
    exe.root_module.linkSystemLibrary("warmc", .{});
    // the run-time path absolute, as a relative one would be the working
    // directory's when the host runs (zig build runs here, in example/jit/zig)
    if (target.result.os.tag != .windows) {
        const abs = std.Io.Dir.cwd().realPathFileAlloc(b.graph.io, "../../../build", b.graph.arena) catch
            @panic("no ../../../build: make jit first");
        exe.root_module.addRPathSpecial(abs);
    }
    b.installArtifact(exe);
}
