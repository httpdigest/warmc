// A Kotlin host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in Kotlin, calls its exports and reads its memory,
// through the standard WebAssembly C API (wasm.h) and the JDK's foreign
// function API (java.lang.foreign, Java 22 or later).
//
//   kotlinc example/jit/kotlin/Host.kt -include-runtime -d host.jar
//   java --enable-native-access=ALL-UNNAMED -jar host.jar build/libwarmc.dylib example/jit/guest.wat
//
// (build/libwarmc.so on Linux, build/libwarmc.dll on Windows)
import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.Linker
import java.lang.foreign.MemoryLayout
import java.lang.foreign.MemorySegment
import java.lang.foreign.SymbolLookup
import java.lang.foreign.ValueLayout
import java.lang.invoke.MethodHandle
import java.lang.invoke.MethodHandles
import java.lang.invoke.MethodType
import java.nio.file.Files
import java.nio.file.Path

private val linker: Linker = Linker.nativeLinker()
private val P = ValueLayout.ADDRESS
private val SIZE_T = ValueLayout.JAVA_LONG
private val NULL: MemorySegment = MemorySegment.NULL

// wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...: { size_t size; T *data; }
private const val VEC_SIZE = 16L
private const val VEC_DATA = 8L
// wasm_val_t: { uint8_t kind; union { i32, i64, f32, f64, ref } of; }
private const val VAL_SIZE = 16L
private const val VAL_OF = 8L
private const val WASM_I32: Byte = 0
private const val WASM_I64: Byte = 1

// The C API's functions this host calls, by name, each with its C signature
class Wasm(library: Path) {
    private val lib = SymbolLookup.libraryLookup(library, Arena.global())
    private fun fn(name: String, result: MemoryLayout?, vararg args: MemoryLayout): MethodHandle =
        linker.downcallHandle(lib.find(name).orElseThrow(),
            if (result == null) FunctionDescriptor.ofVoid(*args) else FunctionDescriptor.of(result, *args))

    val engineNew = fn("wasm_engine_new", P)
    val storeNew = fn("wasm_store_new", P, P)
    val wat2wasm = fn("warm_wat2wasm", ValueLayout.JAVA_BOOLEAN, P, SIZE_T, P, P)
    val moduleNew = fn("warm_module_new", P, P, P, P)
    val valtypeNew = fn("wasm_valtype_new", P, ValueLayout.JAVA_BYTE)
    val valtypeVecNew = fn("wasm_valtype_vec_new", null, P, SIZE_T, P)
    val valtypeVecNewEmpty = fn("wasm_valtype_vec_new_empty", null, P)
    val functypeNew = fn("wasm_functype_new", P, P, P)
    val functypeDelete = fn("wasm_functype_delete", null, P)
    val funcNew = fn("wasm_func_new", P, P, P, P)
    val funcAsExtern = fn("wasm_func_as_extern", P, P)
    val instanceNew = fn("wasm_instance_new", P, P, P, P, P)
    val instanceExports = fn("wasm_instance_exports", null, P, P)
    val moduleExports = fn("wasm_module_exports", null, P, P)
    val exporttypeName = fn("wasm_exporttype_name", P, P)
    val externAsFunc = fn("wasm_extern_as_func", P, P)
    val externAsMemory = fn("wasm_extern_as_memory", P, P)
    val memoryData = fn("wasm_memory_data", P, P)
    val funcCall = fn("wasm_func_call", P, P, P, P)
    val trapMessage = fn("wasm_trap_message", null, P, P)
    val trapDelete = fn("wasm_trap_delete", null, P)
    val byteVecDelete = fn("wasm_byte_vec_delete", null, P)
    val externVecDelete = fn("wasm_extern_vec_delete", null, P)
    val exporttypeVecDelete = fn("wasm_exporttype_vec_delete", null, P)
    val instanceDelete = fn("wasm_instance_delete", null, P)
    val funcDelete = fn("wasm_func_delete", null, P)
    val moduleDelete = fn("wasm_module_delete", null, P)
    val storeDelete = fn("wasm_store_delete", null, P)
    val engineDelete = fn("wasm_engine_delete", null, P)
}

private fun MethodHandle.ptr(vararg args: Any): MemorySegment = invokeWithArguments(*args) as MemorySegment
private fun MethodHandle.void(vararg args: Any) { invokeWithArguments(*args) }

object Callbacks {
    lateinit var wasm: Wasm
    var memory: MemorySegment = NULL      // the guest's, once it is instantiated

    // host.log(ptr, len): wasm_func_callback_t, called with the arguments'
    // vector and the results' to fill; NULL: no trap.  (An upcall may not
    // throw: nothing on the native side would catch it.)
    @JvmStatic
    fun hostLog(args: MemorySegment, results: MemorySegment): MemorySegment {
        try {
            val vals = args.reinterpret(VEC_SIZE).get(P, VEC_DATA).reinterpret(2 * VAL_SIZE)
            val ptr = Integer.toUnsignedLong(vals.get(ValueLayout.JAVA_INT, VAL_OF))
            val len = vals.get(ValueLayout.JAVA_INT, VAL_SIZE + VAL_OF).toLong()
            val base = wasm.memoryData.ptr(memory).reinterpret(ptr + len)
            println("log: " + String(base.asSlice(ptr, len).toArray(ValueLayout.JAVA_BYTE)))
        } catch (t: Throwable) {
            t.printStackTrace()
        }
        return NULL
    }
}

fun main(argv: Array<String>) {
    val wasm = Wasm(Path.of(argv[0]))
    Callbacks.wasm = wasm
    val wat = Files.readAllBytes(Path.of(if (argv.size > 1) argv[1] else "example/jit/guest.wat"))
    Arena.ofConfined().use { arena ->
        // a wasm_byte_vec_t's text, up to its NUL
        fun text(vec: MemorySegment): String =
            vec.get(P, VEC_DATA).reinterpret(vec.get(SIZE_T, 0)).getString(0)

        val engine = wasm.engineNew.ptr()
        val store = wasm.storeNew.ptr(engine)

        val watText = arena.allocate(wat.size.toLong()).copyFrom(MemorySegment.ofArray(wat))
        val binary = arena.allocate(VEC_SIZE)
        val error = arena.allocate(VEC_SIZE)
        check(wasm.wat2wasm.invokeWithArguments(watText, wat.size.toLong(), binary, error) as Boolean) { text(error) }
        val module = wasm.moduleNew.ptr(store, binary, error)
        check(module != NULL) { text(error) }

        // host.log, of type (i32, i32) -> (), calling Callbacks.hostLog
        val params = arena.allocate(VEC_SIZE)
        val results = arena.allocate(VEC_SIZE)
        val kinds = arena.allocate(16)
        kinds.setAtIndex(P, 0, wasm.valtypeNew.ptr(WASM_I32))
        kinds.setAtIndex(P, 1, wasm.valtypeNew.ptr(WASM_I32))
        wasm.valtypeVecNew.void(params, 2L, kinds)
        wasm.valtypeVecNewEmpty.void(results)
        val logType = wasm.functypeNew.ptr(params, results)
        val callback = linker.upcallStub(
            MethodHandles.lookup().findStatic(Callbacks::class.java, "hostLog",
                MethodType.methodType(MemorySegment::class.java, MemorySegment::class.java, MemorySegment::class.java)),
            FunctionDescriptor.of(P, P, P), arena)
        val logFunc = wasm.funcNew.ptr(store, logType, callback)
        wasm.functypeDelete.void(logType)

        val imports = arena.allocate(VEC_SIZE)
        val importArray = arena.allocate(8)
        importArray.set(P, 0, wasm.funcAsExtern.ptr(logFunc))
        imports.set(SIZE_T, 0, 1L)
        imports.set(P, VEC_DATA, importArray)
        val instance = wasm.instanceNew.ptr(store, module, imports, NULL)
        check(instance != NULL) { "cannot instantiate" }
        val exports = arena.allocate(VEC_SIZE)
        wasm.instanceExports.void(instance, exports)

        // the instance's export named name
        fun export(name: String): MemorySegment {
            val types = arena.allocate(VEC_SIZE)
            wasm.moduleExports.void(module, types)
            val n = types.get(SIZE_T, 0)
            val typeArray = types.get(P, VEC_DATA).reinterpret(n * 8)
            val externs = exports.get(P, VEC_DATA).reinterpret(n * 8)
            var found = NULL
            for (i in 0 until n) {
                val nm = wasm.exporttypeName.ptr(typeArray.getAtIndex(P, i)).reinterpret(VEC_SIZE)
                val bytes = nm.get(P, VEC_DATA).reinterpret(nm.get(SIZE_T, 0)).toArray(ValueLayout.JAVA_BYTE)
                if (String(bytes) == name) { found = externs.getAtIndex(P, i); break }
            }
            wasm.exporttypeVecDelete.void(types)
            return found
        }

        // calls func with nargs values from args, results to res: the trap's message, or null
        fun call(func: MemorySegment, args: MemorySegment, nargs: Long, res: MemorySegment, nres: Long): String? {
            val av = arena.allocate(VEC_SIZE)
            val rv = arena.allocate(VEC_SIZE)
            av.set(SIZE_T, 0, nargs); av.set(P, VEC_DATA, args)
            rv.set(SIZE_T, 0, nres); rv.set(P, VEC_DATA, res)
            val trap = wasm.funcCall.ptr(func, av, rv)
            if (trap == NULL) return null
            val msg = arena.allocate(VEC_SIZE)
            wasm.trapMessage.void(trap, msg)
            val s = text(msg)
            wasm.byteVecDelete.void(msg)
            wasm.trapDelete.void(trap)
            return s
        }

        Callbacks.memory = wasm.externAsMemory.ptr(export("memory"))
        val greet = wasm.externAsFunc.ptr(export("greet"))
        val fib = wasm.externAsFunc.ptr(export("fib"))
        val div = wasm.externAsFunc.ptr(export("div"))
        val peek = wasm.externAsFunc.ptr(export("peek"))

        call(greet, NULL, 0, NULL, 0)

        val args = arena.allocate(2 * VAL_SIZE)
        val res = arena.allocate(VAL_SIZE)
        args.set(ValueLayout.JAVA_BYTE, 0, WASM_I64)
        args.set(ValueLayout.JAVA_LONG, VAL_OF, 90L)
        call(fib, args, 1, res, 1)
        println("fib(90) = " + res.get(ValueLayout.JAVA_LONG, VAL_OF))

        for ((a, b) in listOf(7 to 2, 1 to 0)) {
            args.set(ValueLayout.JAVA_BYTE, 0, WASM_I32)
            args.set(ValueLayout.JAVA_INT, VAL_OF, a)
            args.set(ValueLayout.JAVA_BYTE, VAL_SIZE, WASM_I32)
            args.set(ValueLayout.JAVA_INT, VAL_SIZE + VAL_OF, b)
            val trap = call(div, args, 2, res, 1)
            if (trap == null) println("div($a, $b) = " + res.get(ValueLayout.JAVA_INT, VAL_OF))
            else println("div($a, $b) trapped: $trap")
        }
        for (addr in listOf(16, 65536)) {
            args.set(ValueLayout.JAVA_BYTE, 0, WASM_I32)
            args.set(ValueLayout.JAVA_INT, VAL_OF, addr)
            val trap = call(peek, args, 1, res, 1)
            if (trap == null) println("peek($addr) = " + res.get(ValueLayout.JAVA_INT, VAL_OF))
            else println("peek($addr) trapped: $trap")
        }

        wasm.externVecDelete.void(exports)
        wasm.instanceDelete.void(instance)
        wasm.funcDelete.void(logFunc)
        wasm.moduleDelete.void(module)
        wasm.byteVecDelete.void(binary)
        wasm.storeDelete.void(store)
        wasm.engineDelete.void(engine)
    }
}
