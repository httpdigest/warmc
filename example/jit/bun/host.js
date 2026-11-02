// A Bun host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in JavaScript, calls its exports and reads its memory,
// through the standard WebAssembly C API (wasm.h) and bun:ffi.
//
//   bun example/jit/bun/host.js build/libwarmc.dylib example/jit/guest.wat
//
// (build/libwarmc.so on Linux, build/libwarmc.dll on Windows)
import { readFileSync } from 'node:fs';
import { CString, FFIType as T, JSCallback, dlopen, ptr, read, toArrayBuffer } from 'bun:ffi';

const [libPath, guestPath = 'example/jit/guest.wat'] = process.argv.slice(2);

// The C API's functions this host calls, each with its C signature
const P = T.ptr;
const sig = (returns, ...args) => ({ returns, args });
const { symbols: C } = dlopen(libPath, {
  wasm_engine_new: sig(P),
  wasm_store_new: sig(P, P),
  warm_wat2wasm: sig(T.bool, P, T.u64, P, P),
  warm_module_new: sig(P, P, P, P),
  wasm_valtype_new: sig(P, T.u8),
  wasm_valtype_vec_new: sig(T.void, P, T.u64, P),
  wasm_valtype_vec_new_empty: sig(T.void, P),
  wasm_functype_new: sig(P, P, P),
  wasm_functype_delete: sig(T.void, P),
  wasm_func_new: sig(P, P, P, P),
  wasm_func_as_extern: sig(P, P),
  wasm_instance_new: sig(P, P, P, P, P),
  wasm_instance_exports: sig(T.void, P, P),
  wasm_module_exports: sig(T.void, P, P),
  wasm_exporttype_name: sig(P, P),
  wasm_extern_as_func: sig(P, P),
  wasm_extern_as_memory: sig(P, P),
  wasm_memory_data: sig(P, P),
  wasm_func_call: sig(P, P, P, P),
  wasm_trap_message: sig(T.void, P, P),
  wasm_trap_delete: sig(T.void, P),
  wasm_byte_vec_delete: sig(T.void, P),
  wasm_extern_vec_delete: sig(T.void, P),
  wasm_exporttype_vec_delete: sig(T.void, P),
  wasm_instance_delete: sig(T.void, P),
  wasm_func_delete: sig(T.void, P),
  wasm_module_delete: sig(T.void, P),
  wasm_store_delete: sig(T.void, P),
  wasm_engine_delete: sig(T.void, P),
});

// wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...: { size_t size; T *data; }
const VEC_SIZE = 16;
// wasm_val_t: { uint8_t kind; union { i32, i64, f32, f64, ref } of; }
const VAL_SIZE = 16, VAL_OF = 8;
const WASM_I32 = 0, WASM_I64 = 1;

// a vector over the memory of buf, holding n elements
function vec(n, buf) {
  const v = new BigUint64Array(2);
  v[0] = BigInt(n);
  v[1] = buf ? BigInt(ptr(buf)) : 0n;
  return v;
}

// a wasm_byte_vec_t's text, up to its NUL
const text = (v) => new CString(Number(v[1])).toString();

let memory = 0;   // the guest's, once it is instantiated

// host.log(ptr, len): wasm_func_callback_t, called with the arguments'
// vector and the results' to fill; null: no trap
const hostLog = new JSCallback((args, _results) => {
  const vals = read.ptr(args, 8);
  const p = read.u32(vals, VAL_OF), len = read.u32(vals, VAL_SIZE + VAL_OF);
  const bytes = new Uint8Array(toArrayBuffer(C.wasm_memory_data(memory), p, len));
  console.log('log: ' + new TextDecoder().decode(bytes));
  return null;
}, { returns: P, args: [P, P] });

const engine = C.wasm_engine_new();
const store = C.wasm_store_new(engine);

const wat = readFileSync(guestPath);
const binary = new BigUint64Array(2), error = new BigUint64Array(2);
if (!C.warm_wat2wasm(wat, wat.length, binary, error)) throw new Error(text(error));
const module = C.warm_module_new(store, binary, error);
if (!module) throw new Error(text(error));

// host.log, of type (i32, i32) -> (), calling hostLog
const kinds = new BigUint64Array([BigInt(C.wasm_valtype_new(WASM_I32)), BigInt(C.wasm_valtype_new(WASM_I32))]);
const params = new BigUint64Array(2), results = new BigUint64Array(2);
C.wasm_valtype_vec_new(params, 2, kinds);
C.wasm_valtype_vec_new_empty(results);
const logType = C.wasm_functype_new(params, results);
const logFunc = C.wasm_func_new(store, logType, hostLog.ptr);
C.wasm_functype_delete(logType);

const importArray = new BigUint64Array([BigInt(C.wasm_func_as_extern(logFunc))]);
const instance = C.wasm_instance_new(store, module, vec(1, importArray), null);
if (!instance) throw new Error('cannot instantiate');
const exports = new BigUint64Array(2);
C.wasm_instance_exports(instance, exports);

// the instance's export named name
function exportNamed(name) {
  const types = new BigUint64Array(2);
  C.wasm_module_exports(module, types);
  const n = Number(types[0]), typeArray = Number(types[1]);
  let found = 0;
  for (let i = 0; i < n && !found; i++) {
    const nm = C.wasm_exporttype_name(read.ptr(typeArray, 8 * i));
    const bytes = new Uint8Array(toArrayBuffer(read.ptr(nm, 8), 0, Number(read.u64(nm, 0))));
    if (new TextDecoder().decode(bytes) === name) found = read.ptr(Number(exports[1]), 8 * i);
  }
  C.wasm_exporttype_vec_delete(types);
  return found;
}

// calls func with args ([kind, value]...), nres results to res: the trap's message, or null
function call(func, args, res, nres) {
  const argv = new DataView(new ArrayBuffer(Math.max(1, args.length) * VAL_SIZE));
  args.forEach(([kind, v], i) => {
    argv.setUint8(i * VAL_SIZE, kind);
    if (kind === WASM_I64) argv.setBigInt64(i * VAL_SIZE + VAL_OF, BigInt(v), true);
    else argv.setInt32(i * VAL_SIZE + VAL_OF, v, true);
  });
  const trap = C.wasm_func_call(func, vec(args.length, argv), vec(nres, res));
  if (!trap) return null;
  const msg = new BigUint64Array(2);
  C.wasm_trap_message(trap, msg);
  const s = text(msg);
  C.wasm_byte_vec_delete(msg);
  C.wasm_trap_delete(trap);
  return s;
}

memory = C.wasm_extern_as_memory(exportNamed('memory'));
const greet = C.wasm_extern_as_func(exportNamed('greet'));
const fib = C.wasm_extern_as_func(exportNamed('fib'));
const div = C.wasm_extern_as_func(exportNamed('div'));
const peek = C.wasm_extern_as_func(exportNamed('peek'));

call(greet, [], null, 0);

const resBuf = new ArrayBuffer(VAL_SIZE), res = new DataView(resBuf);
call(fib, [[WASM_I64, 90]], res, 1);
console.log(`fib(90) = ${res.getBigInt64(VAL_OF, true)}`);

for (const [a, b] of [[7, 2], [1, 0]]) {
  const trap = call(div, [[WASM_I32, a], [WASM_I32, b]], res, 1);
  console.log(trap === null ? `div(${a}, ${b}) = ${res.getInt32(VAL_OF, true)}` : `div(${a}, ${b}) trapped: ${trap}`);
}
for (const addr of [16, 65536]) {
  const trap = call(peek, [[WASM_I32, addr]], res, 1);
  console.log(trap === null ? `peek(${addr}) = ${res.getInt32(VAL_OF, true)}` : `peek(${addr}) trapped: ${trap}`);
}

C.wasm_extern_vec_delete(exports);
C.wasm_instance_delete(instance);
C.wasm_func_delete(logFunc);
C.wasm_module_delete(module);
C.wasm_byte_vec_delete(binary);
C.wasm_store_delete(store);
C.wasm_engine_delete(engine);
hostLog.close();
