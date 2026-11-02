// A Node.js host of libwarmc: compiles example/jit/guest.wat, gives it a host
// function written in JavaScript, calls its exports and reads its memory,
// through the standard WebAssembly C API (wasm.h) and Node's node:ffi
// (experimental in Node 26).
//
//   node --no-warnings example/jit/node/host.mjs build/libwarmc.dylib example/jit/guest.wat
//
// (build/libwarmc.so on Linux, build/libwarmc.dll on Windows)
import { readFileSync } from 'node:fs';
import { DynamicLibrary, getInt32, getRawPointer, getUint64, toBuffer, toString } from 'node:ffi';

const [libPath, guestPath = 'example/jit/guest.wat'] = process.argv.slice(2);
const lib = new DynamicLibrary(libPath);

// The C API's functions this host calls, each with its C signature
const P = 'pointer';
const sigs = {
  wasm_engine_new: [P, []],
  wasm_store_new: [P, [P]],
  warm_wat2wasm: ['bool', [P, 'uint64', P, P]],
  warm_module_new: [P, [P, P, P]],
  wasm_valtype_new: [P, ['uint8']],
  wasm_valtype_vec_new: ['void', [P, 'uint64', P]],
  wasm_valtype_vec_new_empty: ['void', [P]],
  wasm_functype_new: [P, [P, P]],
  wasm_functype_delete: ['void', [P]],
  wasm_func_new: [P, [P, P, P]],
  wasm_func_as_extern: [P, [P]],
  wasm_instance_new: [P, [P, P, P, P]],
  wasm_instance_exports: ['void', [P, P]],
  wasm_module_exports: ['void', [P, P]],
  wasm_exporttype_name: [P, [P]],
  wasm_extern_as_func: [P, [P]],
  wasm_extern_as_memory: [P, [P]],
  wasm_memory_data: [P, [P]],
  wasm_func_call: [P, [P, P, P]],
  wasm_trap_message: ['void', [P, P]],
  wasm_trap_delete: ['void', [P]],
  wasm_byte_vec_delete: ['void', [P]],
  wasm_extern_vec_delete: ['void', [P]],
  wasm_exporttype_vec_delete: ['void', [P]],
  wasm_instance_delete: ['void', [P]],
  wasm_func_delete: ['void', [P]],
  wasm_module_delete: ['void', [P]],
  wasm_store_delete: ['void', [P]],
  wasm_engine_delete: ['void', [P]],
};
const C = {};
for (const [name, [ret, args]] of Object.entries(sigs))
  C[name] = lib.getFunction(name, { return: ret, arguments: args });

// wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...: { size_t size; T *data; }
const VEC_SIZE = 16;
// wasm_val_t: { uint8_t kind; union { i32, i64, f32, f64, ref } of; }
const VAL_SIZE = 16, VAL_OF = 8;
const WASM_I32 = 0, WASM_I64 = 1;

// a vector over the memory of buf, holding n elements
function vec(n, buf) {
  const v = Buffer.alloc(VEC_SIZE);
  v.writeBigUInt64LE(BigInt(n), 0);
  v.writeBigUInt64LE(buf ? getRawPointer(buf) : 0n, 8);
  return v;
}

// a wasm_byte_vec_t's text, up to its NUL
const text = (v) => toString(v.readBigUInt64LE(8));

let memory = 0n;   // the guest's, once it is instantiated

// host.log(ptr, len): wasm_func_callback_t, called with the arguments'
// vector and the results' to fill; 0n: no trap
const hostLog = lib.registerCallback({ return: P, arguments: [P, P] }, (args, _results) => {
  const vals = getUint64(args, 8);
  const ptr = getInt32(vals, VAL_OF) >>> 0, len = getInt32(vals, VAL_SIZE + VAL_OF) >>> 0;
  const base = C.wasm_memory_data(memory);
  console.log('log: ' + toBuffer(base + BigInt(ptr), len).toString('utf8'));
  return 0n;
});

const engine = C.wasm_engine_new();
const store = C.wasm_store_new(engine);

const wat = readFileSync(guestPath);
const binary = Buffer.alloc(VEC_SIZE), error = Buffer.alloc(VEC_SIZE);
if (!C.warm_wat2wasm(wat, wat.length, binary, error)) throw new Error(text(error));
const module = C.warm_module_new(store, binary, error);
if (!module) throw new Error(text(error));

// host.log, of type (i32, i32) -> (), calling hostLog
const kinds = Buffer.alloc(16);
kinds.writeBigUInt64LE(C.wasm_valtype_new(WASM_I32), 0);
kinds.writeBigUInt64LE(C.wasm_valtype_new(WASM_I32), 8);
const params = Buffer.alloc(VEC_SIZE), results = Buffer.alloc(VEC_SIZE);
C.wasm_valtype_vec_new(params, 2, kinds);
C.wasm_valtype_vec_new_empty(results);
const logType = C.wasm_functype_new(params, results);
const logFunc = C.wasm_func_new(store, logType, hostLog);
C.wasm_functype_delete(logType);

const importArray = Buffer.alloc(8);
importArray.writeBigUInt64LE(C.wasm_func_as_extern(logFunc), 0);
const instance = C.wasm_instance_new(store, module, vec(1, importArray), null);
if (!instance) throw new Error('cannot instantiate');
const exports = Buffer.alloc(VEC_SIZE);
C.wasm_instance_exports(instance, exports);

// the instance's export named name
function exportNamed(name) {
  const types = Buffer.alloc(VEC_SIZE);
  C.wasm_module_exports(module, types);
  const n = Number(types.readBigUInt64LE(0)), typeArray = types.readBigUInt64LE(8);
  let found = 0n;
  for (let i = 0; i < n && !found; i++) {
    const nm = C.wasm_exporttype_name(getUint64(typeArray, 8 * i));
    const s = toBuffer(getUint64(nm, 8), Number(getUint64(nm, 0))).toString('utf8');
    if (s === name) found = getUint64(exports.readBigUInt64LE(8), 8 * i);
  }
  C.wasm_exporttype_vec_delete(types);
  return found;
}

// calls func with args ([kind, value]...), nres results to res: the trap's message, or null
function call(func, args, res, nres) {
  const argv = Buffer.alloc(Math.max(1, args.length) * VAL_SIZE);
  args.forEach(([kind, v], i) => {
    argv.writeUInt8(kind, i * VAL_SIZE);
    if (kind === WASM_I64) argv.writeBigInt64LE(BigInt(v), i * VAL_SIZE + VAL_OF);
    else argv.writeInt32LE(v, i * VAL_SIZE + VAL_OF);
  });
  const trap = C.wasm_func_call(func, vec(args.length, argv), vec(nres, res));
  if (!trap) return null;
  const msg = Buffer.alloc(VEC_SIZE);
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

const res = Buffer.alloc(VAL_SIZE);
call(fib, [[WASM_I64, 90]], res, 1);
console.log(`fib(90) = ${res.readBigInt64LE(VAL_OF)}`);

for (const [a, b] of [[7, 2], [1, 0]]) {
  const trap = call(div, [[WASM_I32, a], [WASM_I32, b]], res, 1);
  console.log(trap === null ? `div(${a}, ${b}) = ${res.readInt32LE(VAL_OF)}` : `div(${a}, ${b}) trapped: ${trap}`);
}
for (const addr of [16, 65536]) {
  const trap = call(peek, [[WASM_I32, addr]], res, 1);
  console.log(trap === null ? `peek(${addr}) = ${res.readInt32LE(VAL_OF)}` : `peek(${addr}) trapped: ${trap}`);
}

C.wasm_extern_vec_delete(exports);
C.wasm_instance_delete(instance);
C.wasm_func_delete(logFunc);
C.wasm_module_delete(module);
C.wasm_byte_vec_delete(binary);
C.wasm_store_delete(store);
C.wasm_engine_delete(engine);
lib.unregisterCallback(hostLog);
