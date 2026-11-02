// A Java host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in Java, calls its exports and reads its memory, through
// the standard WebAssembly C API (wasm.h) and the JDK's foreign function API
// (java.lang.foreign, Java 22 or later).
//
//   java --enable-native-access=ALL-UNNAMED example/jit/java/Host.java build/libwarmc.dylib example/jit/guest.wat
//
// (build/libwarmc.so on Linux, build/libwarmc.dll on Windows)
import java.lang.foreign.AddressLayout;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class Host {
    static final Linker LINKER = Linker.nativeLinker();
    static final ValueLayout.OfLong SIZE_T = ValueLayout.JAVA_LONG;
    static final MemorySegment NULL = MemorySegment.NULL;
    static final AddressLayout P = ValueLayout.ADDRESS;

    // wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...: { size_t size; T *data; }
    static final long VEC_SIZE = 16, VEC_DATA = 8;
    // wasm_val_t: { uint8_t kind; union { i32, i64, f32, f64, ref } of; }
    static final long VAL_SIZE = 16, VAL_OF = 8;
    static final byte WASM_I32 = 0, WASM_I64 = 1;

    static SymbolLookup lib;

    static MethodHandle fn(String name, MemoryLayout result, MemoryLayout... args) {
        FunctionDescriptor d = result == null ? FunctionDescriptor.ofVoid(args) : FunctionDescriptor.of(result, args);
        return LINKER.downcallHandle(lib.find(name).orElseThrow(), d);
    }

    static MethodHandle wasm_engine_new, wasm_store_new, warm_wat2wasm, warm_module_new,
        wasm_valtype_new, wasm_valtype_vec_new, wasm_valtype_vec_new_empty, wasm_functype_new,
        wasm_functype_delete, wasm_func_new, wasm_func_as_extern, wasm_instance_new,
        wasm_instance_exports, wasm_module_exports, wasm_exporttype_name, wasm_extern_as_func,
        wasm_extern_as_memory, wasm_memory_data, wasm_func_call, wasm_trap_message,
        wasm_trap_delete, wasm_byte_vec_delete, wasm_extern_vec_delete, wasm_exporttype_vec_delete,
        wasm_instance_delete, wasm_func_delete, wasm_module_delete, wasm_store_delete,
        wasm_engine_delete;

    static void bind(Path library) {
        lib = SymbolLookup.libraryLookup(library, Arena.global());
        wasm_engine_new = fn("wasm_engine_new", P);
        wasm_store_new = fn("wasm_store_new", P, P);
        warm_wat2wasm = fn("warm_wat2wasm", ValueLayout.JAVA_BOOLEAN, P, SIZE_T, P, P);
        warm_module_new = fn("warm_module_new", P, P, P, P);
        wasm_valtype_new = fn("wasm_valtype_new", P, ValueLayout.JAVA_BYTE);
        wasm_valtype_vec_new = fn("wasm_valtype_vec_new", null, P, SIZE_T, P);
        wasm_valtype_vec_new_empty = fn("wasm_valtype_vec_new_empty", null, P);
        wasm_functype_new = fn("wasm_functype_new", P, P, P);
        wasm_functype_delete = fn("wasm_functype_delete", null, P);
        wasm_func_new = fn("wasm_func_new", P, P, P, P);
        wasm_func_as_extern = fn("wasm_func_as_extern", P, P);
        wasm_instance_new = fn("wasm_instance_new", P, P, P, P, P);
        wasm_instance_exports = fn("wasm_instance_exports", null, P, P);
        wasm_module_exports = fn("wasm_module_exports", null, P, P);
        wasm_exporttype_name = fn("wasm_exporttype_name", P, P);
        wasm_extern_as_func = fn("wasm_extern_as_func", P, P);
        wasm_extern_as_memory = fn("wasm_extern_as_memory", P, P);
        wasm_memory_data = fn("wasm_memory_data", P, P);
        wasm_func_call = fn("wasm_func_call", P, P, P, P);
        wasm_trap_message = fn("wasm_trap_message", null, P, P);
        wasm_trap_delete = fn("wasm_trap_delete", null, P);
        wasm_byte_vec_delete = fn("wasm_byte_vec_delete", null, P);
        wasm_extern_vec_delete = fn("wasm_extern_vec_delete", null, P);
        wasm_exporttype_vec_delete = fn("wasm_exporttype_vec_delete", null, P);
        wasm_instance_delete = fn("wasm_instance_delete", null, P);
        wasm_func_delete = fn("wasm_func_delete", null, P);
        wasm_module_delete = fn("wasm_module_delete", null, P);
        wasm_store_delete = fn("wasm_store_delete", null, P);
        wasm_engine_delete = fn("wasm_engine_delete", null, P);
    }

    static MemorySegment memory;    // the guest's, once it is instantiated

    // host.log(ptr, len): wasm_func_callback_t, called with the arguments'
    // vector and the results' to fill; NULL: no trap
    // (an upcall may not throw: nothing on the native side would catch it)
    static MemorySegment hostLog(MemorySegment args, MemorySegment results) {
        try {
            MemorySegment vals = args.reinterpret(VEC_SIZE).get(P, VEC_DATA).reinterpret(2 * VAL_SIZE);
            int ptr = vals.get(ValueLayout.JAVA_INT, VAL_OF);
            int len = vals.get(ValueLayout.JAVA_INT, VAL_SIZE + VAL_OF);
            MemorySegment base = ((MemorySegment) wasm_memory_data.invokeExact(memory))
                .reinterpret(Integer.toUnsignedLong(ptr) + len);
            byte[] bytes = base.asSlice(Integer.toUnsignedLong(ptr), len).toArray(ValueLayout.JAVA_BYTE);
            System.out.println("log: " + new String(bytes, StandardCharsets.UTF_8));
        } catch (Throwable t) {
            t.printStackTrace();
        }
        return NULL;
    }

    // a wasm_byte_vec_t's text, up to its NUL
    static String text(MemorySegment vec) {
        long n = vec.get(SIZE_T, 0);
        MemorySegment data = vec.get(P, VEC_DATA).reinterpret(n);
        return data.getString(0);
    }

    // the instance's export named name
    static MemorySegment export(Arena arena, MemorySegment module, MemorySegment exports, String name) throws Throwable {
        MemorySegment types = arena.allocate(VEC_SIZE);
        wasm_module_exports.invokeExact(module, types);
        long n = types.get(SIZE_T, 0);
        MemorySegment typeArray = types.get(P, VEC_DATA).reinterpret(n * 8);
        MemorySegment externs = exports.get(P, VEC_DATA).reinterpret(n * 8);
        MemorySegment found = NULL;
        for (long i = 0; i < n && found.equals(NULL); i++) {
            MemorySegment nm = ((MemorySegment) wasm_exporttype_name.invokeExact(typeArray.getAtIndex(P, i)))
                .reinterpret(VEC_SIZE);
            long len = nm.get(SIZE_T, 0);
            byte[] b = nm.get(P, VEC_DATA).reinterpret(len).toArray(ValueLayout.JAVA_BYTE);
            if (new String(b, StandardCharsets.UTF_8).equals(name)) found = externs.getAtIndex(P, i);
        }
        wasm_exporttype_vec_delete.invokeExact(types);
        return found;
    }

    // calls func with args, results going to results: the trap's message, or null
    static String call(Arena arena, MemorySegment func, MemorySegment args, long nargs,
                       MemorySegment results, long nresults) throws Throwable {
        MemorySegment av = arena.allocate(VEC_SIZE), rv = arena.allocate(VEC_SIZE);
        av.set(SIZE_T, 0, nargs);
        av.set(P, VEC_DATA, args);
        rv.set(SIZE_T, 0, nresults);
        rv.set(P, VEC_DATA, results);
        MemorySegment trap = (MemorySegment) wasm_func_call.invokeExact(func, av, rv);
        if (trap.equals(NULL)) return null;
        MemorySegment msg = arena.allocate(VEC_SIZE);
        wasm_trap_message.invokeExact(trap, msg);
        String s = text(msg);
        wasm_byte_vec_delete.invokeExact(msg);
        wasm_trap_delete.invokeExact(trap);
        return s;
    }

    public static void main(String[] argv) throws Throwable {
        bind(Path.of(argv[0]));
        byte[] wat = Files.readAllBytes(Path.of(argv.length > 1 ? argv[1] : "example/jit/guest.wat"));
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment engine = (MemorySegment) wasm_engine_new.invokeExact();
            MemorySegment store = (MemorySegment) wasm_store_new.invokeExact(engine);

            MemorySegment watText = arena.allocate(wat.length);
            watText.copyFrom(MemorySegment.ofArray(wat));
            MemorySegment binary = arena.allocate(VEC_SIZE), error = arena.allocate(VEC_SIZE);
            if (!(boolean) warm_wat2wasm.invokeExact(watText, (long) wat.length, binary, error))
                throw new IllegalStateException(text(error));
            MemorySegment module = (MemorySegment) warm_module_new.invokeExact(store, binary, error);
            if (module.equals(NULL)) throw new IllegalStateException(text(error));

            // host.log, of type (i32, i32) -> (), calling hostLog
            MemorySegment params = arena.allocate(VEC_SIZE), results = arena.allocate(VEC_SIZE);
            MemorySegment kinds = arena.allocate(16);
            kinds.setAtIndex(P, 0, (MemorySegment) wasm_valtype_new.invokeExact(WASM_I32));
            kinds.setAtIndex(P, 1, (MemorySegment) wasm_valtype_new.invokeExact(WASM_I32));
            wasm_valtype_vec_new.invokeExact(params, 2L, kinds);
            wasm_valtype_vec_new_empty.invokeExact(results);
            MemorySegment logType = (MemorySegment) wasm_functype_new.invokeExact(params, results);
            MemorySegment callback = LINKER.upcallStub(
                MethodHandles.lookup().findStatic(Host.class, "hostLog",
                    MethodType.methodType(MemorySegment.class, MemorySegment.class, MemorySegment.class)),
                FunctionDescriptor.of(P, P, P), arena);
            MemorySegment logFunc = (MemorySegment) wasm_func_new.invokeExact(store, logType, callback);
            wasm_functype_delete.invokeExact(logType);

            MemorySegment imports = arena.allocate(VEC_SIZE), importArray = arena.allocate(8);
            importArray.set(P, 0, (MemorySegment) wasm_func_as_extern.invokeExact(logFunc));
            imports.set(SIZE_T, 0, 1L);
            imports.set(P, VEC_DATA, importArray);
            MemorySegment instance = (MemorySegment) wasm_instance_new.invokeExact(store, module, imports, NULL);
            if (instance.equals(NULL)) throw new IllegalStateException("cannot instantiate");
            MemorySegment exports = arena.allocate(VEC_SIZE);
            wasm_instance_exports.invokeExact(instance, exports);
            memory = (MemorySegment) wasm_extern_as_memory.invokeExact(export(arena, module, exports, "memory"));
            MemorySegment greet = (MemorySegment) wasm_extern_as_func.invokeExact(export(arena, module, exports, "greet"));
            MemorySegment fib = (MemorySegment) wasm_extern_as_func.invokeExact(export(arena, module, exports, "fib"));
            MemorySegment div = (MemorySegment) wasm_extern_as_func.invokeExact(export(arena, module, exports, "div"));
            MemorySegment peek = (MemorySegment) wasm_extern_as_func.invokeExact(export(arena, module, exports, "peek"));

            call(arena, greet, NULL, 0, NULL, 0);

            MemorySegment args = arena.allocate(2 * VAL_SIZE), res = arena.allocate(VAL_SIZE);
            args.set(ValueLayout.JAVA_BYTE, 0, WASM_I64);
            args.set(ValueLayout.JAVA_LONG, VAL_OF, 90L);
            call(arena, fib, args, 1, res, 1);
            System.out.println("fib(90) = " + res.get(ValueLayout.JAVA_LONG, VAL_OF));

            int[][] pairs = { { 7, 2 }, { 1, 0 } };
            for (int[] p : pairs) {
                args.set(ValueLayout.JAVA_BYTE, 0, WASM_I32);
                args.set(ValueLayout.JAVA_INT, VAL_OF, p[0]);
                args.set(ValueLayout.JAVA_BYTE, VAL_SIZE, WASM_I32);
                args.set(ValueLayout.JAVA_INT, VAL_SIZE + VAL_OF, p[1]);
                String trap = call(arena, div, args, 2, res, 1);
                if (trap == null) System.out.println("div(" + p[0] + ", " + p[1] + ") = " + res.get(ValueLayout.JAVA_INT, VAL_OF));
                else System.out.println("div(" + p[0] + ", " + p[1] + ") trapped: " + trap);
            }
            for (int addr : new int[] { 16, 65536 }) {
                args.set(ValueLayout.JAVA_BYTE, 0, WASM_I32);
                args.set(ValueLayout.JAVA_INT, VAL_OF, addr);
                String trap = call(arena, peek, args, 1, res, 1);
                if (trap == null) System.out.println("peek(" + addr + ") = " + res.get(ValueLayout.JAVA_INT, VAL_OF));
                else System.out.println("peek(" + addr + ") trapped: " + trap);
            }

            wasm_extern_vec_delete.invokeExact(exports);
            wasm_instance_delete.invokeExact(instance);
            wasm_func_delete.invokeExact(logFunc);
            wasm_module_delete.invokeExact(module);
            wasm_byte_vec_delete.invokeExact(binary);
            wasm_store_delete.invokeExact(store);
            wasm_engine_delete.invokeExact(engine);
        }
    }
}
