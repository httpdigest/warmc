/* test/shared/basic.wat as a shared library, called from Java through the
   foreign function and memory API (java.lang.foreign, final since Java 22):
   downcalls of each scalar type, several results through pointers, a trap
   the call survives, linear memory as a MemorySegment, the import as an
   upcall stub with its ctx, and recursion stopped at the stack limit.

   The JVM keeps SIGSEGV handlers of its own -- implicit null checks and
   safepoint polls are faults it expects -- so this also shows that the
   library's runtime passes on the faults that are not its own.

   Usage: java --enable-native-access=ALL-UNNAMED Basic.java LIBRARY */

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_DOUBLE;
import static java.lang.foreign.ValueLayout.JAVA_FLOAT;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

public class Basic {
  static final List<String> failures = new ArrayList<>();
  static final List<Long> seen = new ArrayList<>();
  static final Linker LINKER = Linker.nativeLinker();
  static SymbolLookup lib;

  static void check(String what, Object got, Object want) {
    if (!Objects.equals(got, want))
      failures.add(what + ": got " + got + ", want " + want);
  }

  static MethodHandle fn(String name, FunctionDescriptor d) {
    return LINKER.downcallHandle(lib.find(name).orElseThrow(), d);
  }

  static long twice(MemorySegment ctx, long v) {
    seen.add(ctx.address());
    return 2 * v;
  }

  /* An object the JIT can be pushed to null-check implicitly, by a fault. */
  static int length(String s) { return s.length(); }

  public static void main(String[] args) throws Throwable {
    try (Arena arena = Arena.ofConfined()) {
      lib = SymbolLookup.libraryLookup(Path.of(args[0]), arena);

      MethodHandle twiceMh = MethodHandles.lookup().findStatic(Basic.class, "twice",
          MethodType.methodType(long.class, MemorySegment.class, long.class));
      MemorySegment twiceStub = LINKER.upcallStub(twiceMh,
          FunctionDescriptor.of(JAVA_LONG, ADDRESS, JAVA_LONG), arena);
      MethodHandle bind = fn("basic_bind_import",
          FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS));
      MemorySegment ctx = MemorySegment.ofAddress(0x5eed);
      check("bind env.twice", (int) bind.invokeExact(arena.allocateFrom("env"),
          arena.allocateFrom("twice"), twiceStub, ctx), 0);
      check("bind an import the module lacks", (int) bind.invokeExact(
          arena.allocateFrom("env"), arena.allocateFrom("nope"), MemorySegment.NULL,
          MemorySegment.NULL), 1);

      MethodHandle last = fn("basic_last_trap", FunctionDescriptor.of(JAVA_INT));
      MethodHandle name = fn("basic_trap_name", FunctionDescriptor.of(ADDRESS, JAVA_INT));

      MethodHandle add = fn("add", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT));
      check("add", (int) add.invokeExact(40, 2), 42);
      check("add wraps", (int) add.invokeExact(Integer.MAX_VALUE, 1), Integer.MIN_VALUE);
      check("fib", (long) fn("fib", FunctionDescriptor.of(JAVA_LONG, JAVA_INT))
          .invokeExact(30), 832040L);
      check("mix", (double) fn("mix", FunctionDescriptor.of(JAVA_DOUBLE, JAVA_DOUBLE,
          JAVA_INT, JAVA_FLOAT, JAVA_LONG)).invokeExact(2.5, 4, 0.25f, 3L), 12.75);
      check("half", (float) fn("half", FunctionDescriptor.of(JAVA_FLOAT, JAVA_FLOAT))
          .invokeExact(3.0f), 1.5f);

      MethodHandle quot = fn("quot", FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT));
      check("quot", (int) quot.invokeExact(7, 2), 3);
      check("no trap", (int) last.invokeExact(), 0);
      check("quot by zero", (int) quot.invokeExact(1, 0), 0);
      check("the trap", (int) last.invokeExact(), 3);
      MemorySegment nm = (MemorySegment) name.invokeExact(3);
      check("its name", nm.reinterpret(64).getString(0), "integer divide by zero");
      check("the next call", (int) quot.invokeExact(-9, 3), -3);
      check("clears it", (int) last.invokeExact(), 0);

      MemorySegment r0 = arena.allocate(JAVA_INT), r1 = arena.allocate(JAVA_LONG);
      fn("divmod", FunctionDescriptor.ofVoid(JAVA_INT, JAVA_INT, ADDRESS, ADDRESS))
          .invokeExact(17, 5, r0, r1);
      check("divmod", List.of(r0.get(JAVA_INT, 0), r1.get(JAVA_LONG, 0)), List.of(3, 2L));

      check("seven", (long) fn("seven", FunctionDescriptor.of(JAVA_LONG, JAVA_LONG,
          JAVA_LONG, JAVA_LONG, JAVA_LONG, JAVA_LONG, JAVA_LONG, JAVA_LONG))
          .invokeExact(1L, 2L, 3L, 4L, 5L, 6L, 7L), 7654321L);
      check("twice_plus_one", (long) fn("twice_plus_one",
          FunctionDescriptor.of(JAVA_LONG, JAVA_LONG)).invokeExact(20L), 41L);
      check("the import's ctx", seen, List.of(0x5eedL));

      MemorySegment sizep = arena.allocate(JAVA_LONG);
      MemorySegment mem = (MemorySegment) fn("basic_memory",
          FunctionDescriptor.of(ADDRESS, ADDRESS)).invokeExact(sizep);
      long size = sizep.get(JAVA_LONG, 0);
      check("memory size", size, 65536L);
      mem = mem.reinterpret(size);
      for (int i = 0; i < 10; i++) mem.set(JAVA_BYTE, 100 + i, (byte) (i + 1));
      check("sum of what Java wrote", (int) fn("sum",
          FunctionDescriptor.of(JAVA_INT, JAVA_INT, JAVA_INT)).invokeExact(100, 10), 55);

      MethodHandle deep = fn("deep", FunctionDescriptor.of(JAVA_INT, JAVA_INT));
      check("deep", (int) deep.invokeExact(1000), 1000);
      check("too deep", (int) deep.invokeExact(100000000), 0);
      check("stack exhausted", (int) last.invokeExact(), 11);
      check("and after it", (int) deep.invokeExact(10), 10);

      check("add-one", (int) fn("add_one", FunctionDescriptor.of(JAVA_INT, JAVA_INT))
          .invokeExact(41), 42);
      check("int", (int) fn("basic_int", FunctionDescriptor.of(JAVA_INT)).invokeExact(), 42);
      check("no symbol for v128", lib.find("splat").isPresent(), false);

      /* The JVM's own faults, after the library's handler went in front of
         its: a hot method meeting null, compiled with an implicit check. */
      int npe = 0;
      String[] ss = { "a", "bb", null };
      for (int i = 0; i < 300000; i++) {
        try { length(ss[i % 3]); } catch (NullPointerException e) { npe++; }
      }
      check("the JVM's null pointer exceptions", npe, 100000);
      check("calls", (int) fn("calls", FunctionDescriptor.of(JAVA_INT)).invokeExact(), 2);
    }

    for (String f : failures) System.out.println("FAIL[shared/java] " + f);
    if (!failures.isEmpty()) System.exit(1);
    System.out.println("shared/java: downcalls of each type, results through pointers, "
        + "traps, memory, an upcall import, the stack limit and the JVM's own faults");
  }
}
