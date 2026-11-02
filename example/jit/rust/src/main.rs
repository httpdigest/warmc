//! A Rust host of libwarmc: compiles example/jit/guest.wat, gives it a host
//! function written in Rust, calls its exports and reads its memory, through
//! the standard WebAssembly C API (wasm.h), declared here as Rust sees it.
//!
//!     cd example/jit/rust && cargo run --release -- ../guest.wat
use std::ffi::{CStr, c_char, c_void};
use std::ptr;
use std::sync::atomic::{AtomicPtr, Ordering};

// The C API's objects, opaque here
#[repr(C)]
pub struct Opaque {
    _private: [u8; 0],
}
type Engine = Opaque;
type Store = Opaque;
type Module = Opaque;
type Instance = Opaque;
type Func = Opaque;
type FuncType = Opaque;
type ValType = Opaque;
type Extern = Opaque;
type Memory = Opaque;
type Trap = Opaque;
type ExportType = Opaque;

/// wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...
#[repr(C)]
pub struct Vec<T> {
    size: usize,
    data: *mut T,
}

impl<T> Vec<T> {
    const EMPTY: Self = Vec { size: 0, data: ptr::null_mut() };
    fn of(s: &mut [T]) -> Self {
        Vec { size: s.len(), data: s.as_mut_ptr() }
    }
    unsafe fn as_slice(&self) -> &[T] {
        if self.size == 0 { &[] } else { unsafe { std::slice::from_raw_parts(self.data, self.size) } }
    }
}

/// wasm_val_t: a kind and a union of what it holds
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Val {
    kind: u8,
    of: ValOf,
}
#[repr(C)]
#[derive(Clone, Copy)]
union ValOf {
    i32: i32,
    i64: i64,
    f32: f32,
    f64: f64,
    r: *mut c_void,
}
const WASM_I32: u8 = 0;
const WASM_I64: u8 = 1;

impl Val {
    fn i32(v: i32) -> Val {
        Val { kind: WASM_I32, of: ValOf { i32: v } }
    }
    fn i64(v: i64) -> Val {
        Val { kind: WASM_I64, of: ValOf { i64: v } }
    }
    const NONE: Val = Val { kind: WASM_I32, of: ValOf { i64: 0 } };
}

type Callback = unsafe extern "C" fn(args: *const Vec<Val>, results: *mut Vec<Val>) -> *mut Trap;

unsafe extern "C" {
    fn wasm_engine_new() -> *mut Engine;
    fn wasm_engine_delete(e: *mut Engine);
    fn wasm_store_new(e: *mut Engine) -> *mut Store;
    fn wasm_store_delete(s: *mut Store);
    fn warm_wat2wasm(text: *const c_char, len: usize, out: *mut Vec<u8>, err: *mut Vec<u8>) -> bool;
    fn warm_module_new(s: *mut Store, bin: *const Vec<u8>, err: *mut Vec<u8>) -> *mut Module;
    fn wasm_module_delete(m: *mut Module);
    fn wasm_module_exports(m: *const Module, out: *mut Vec<*mut ExportType>);
    fn wasm_exporttype_vec_delete(v: *mut Vec<*mut ExportType>);
    fn wasm_exporttype_name(t: *const ExportType) -> *const Vec<u8>;
    fn wasm_valtype_new(kind: u8) -> *mut ValType;
    fn wasm_valtype_vec_new(out: *mut Vec<*mut ValType>, n: usize, data: *const *mut ValType);
    fn wasm_valtype_vec_new_empty(out: *mut Vec<*mut ValType>);
    fn wasm_functype_new(params: *mut Vec<*mut ValType>, results: *mut Vec<*mut ValType>) -> *mut FuncType;
    fn wasm_functype_delete(t: *mut FuncType);
    fn wasm_func_new(s: *mut Store, t: *const FuncType, cb: Callback) -> *mut Func;
    fn wasm_func_delete(f: *mut Func);
    fn wasm_func_as_extern(f: *mut Func) -> *mut Extern;
    fn wasm_func_call(f: *const Func, args: *const Vec<Val>, results: *mut Vec<Val>) -> *mut Trap;
    fn wasm_instance_new(s: *mut Store, m: *const Module, imports: *const Vec<*mut Extern>,
                         trap: *mut *mut Trap) -> *mut Instance;
    fn wasm_instance_delete(i: *mut Instance);
    fn wasm_instance_exports(i: *const Instance, out: *mut Vec<*mut Extern>);
    fn wasm_extern_vec_delete(v: *mut Vec<*mut Extern>);
    fn wasm_extern_as_func(e: *mut Extern) -> *mut Func;
    fn wasm_extern_as_memory(e: *mut Extern) -> *mut Memory;
    fn wasm_memory_data(m: *mut Memory) -> *mut u8;
    fn wasm_trap_message(t: *const Trap, out: *mut Vec<u8>);
    fn wasm_trap_delete(t: *mut Trap);
    fn wasm_byte_vec_delete(v: *mut Vec<u8>);
}

// the guest's memory, once it is instantiated
static MEMORY: AtomicPtr<Memory> = AtomicPtr::new(ptr::null_mut());

/// host.log(ptr, len): the bytes the guest points at, printed
unsafe extern "C" fn host_log(args: *const Vec<Val>, _results: *mut Vec<Val>) -> *mut Trap {
    unsafe {
        let args = (*args).as_slice();
        let (p, len) = (args[0].of.i32 as u32 as usize, args[1].of.i32 as u32 as usize);
        let base = wasm_memory_data(MEMORY.load(Ordering::Relaxed));
        let bytes = std::slice::from_raw_parts(base.add(p), len);
        println!("log: {}", String::from_utf8_lossy(bytes));
    }
    ptr::null_mut()
}

/// a wasm_byte_vec_t's text, up to its NUL
unsafe fn text(v: &Vec<u8>) -> String {
    unsafe { CStr::from_ptr(v.data as *const c_char).to_string_lossy().into_owned() }
}

/// the instance's export named name
unsafe fn export(module: *const Module, exports: &Vec<*mut Extern>, name: &str) -> *mut Extern {
    unsafe {
        let mut types = Vec::EMPTY;
        wasm_module_exports(module, &mut types);
        let found = types
            .as_slice()
            .iter()
            .position(|&t| (*wasm_exporttype_name(t)).as_slice() == name.as_bytes())
            .map(|i| exports.as_slice()[i]);
        wasm_exporttype_vec_delete(&mut types);
        found.unwrap_or_else(|| panic!("no export {name}"))
    }
}

/// calls f with args, results to results: Err with the trap's message
unsafe fn call(f: *const Func, args: &mut [Val], results: &mut [Val]) -> Result<(), String> {
    unsafe {
        let trap = wasm_func_call(f, &Vec::of(args), &mut Vec::of(results));
        if trap.is_null() {
            return Ok(());
        }
        let mut msg = Vec::EMPTY;
        wasm_trap_message(trap, &mut msg);
        let s = text(&msg);
        wasm_byte_vec_delete(&mut msg);
        wasm_trap_delete(trap);
        Err(s)
    }
}

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| "example/jit/guest.wat".into());
    let wat = std::fs::read(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
    unsafe {
        let engine = wasm_engine_new();
        let store = wasm_store_new(engine);

        let (mut binary, mut err) = (Vec::EMPTY, Vec::EMPTY);
        if !warm_wat2wasm(wat.as_ptr() as *const c_char, wat.len(), &mut binary, &mut err) {
            panic!("{}", text(&err));
        }
        let module = warm_module_new(store, &binary, &mut err);
        if module.is_null() {
            panic!("{}", text(&err));
        }

        // host.log, of type (i32, i32) -> (), calling host_log
        let (mut params, mut results) = (Vec::EMPTY, Vec::EMPTY);
        let kinds = [wasm_valtype_new(WASM_I32), wasm_valtype_new(WASM_I32)];
        wasm_valtype_vec_new(&mut params, kinds.len(), kinds.as_ptr());
        wasm_valtype_vec_new_empty(&mut results);
        let log_type = wasm_functype_new(&mut params, &mut results);
        let log_func = wasm_func_new(store, log_type, host_log);
        wasm_functype_delete(log_type);

        let mut imports = [wasm_func_as_extern(log_func)];
        let instance = wasm_instance_new(store, module, &Vec::of(&mut imports), ptr::null_mut());
        assert!(!instance.is_null(), "cannot instantiate");
        let mut exports = Vec::EMPTY;
        wasm_instance_exports(instance, &mut exports);
        MEMORY.store(wasm_extern_as_memory(export(module, &exports, "memory")), Ordering::Relaxed);
        let greet = wasm_extern_as_func(export(module, &exports, "greet"));
        let fib = wasm_extern_as_func(export(module, &exports, "fib"));
        let div = wasm_extern_as_func(export(module, &exports, "div"));
        let peek = wasm_extern_as_func(export(module, &exports, "peek"));

        let _ = call(greet, &mut [], &mut []);

        let mut r = [Val::NONE];
        let _ = call(fib, &mut [Val::i64(90)], &mut r);
        println!("fib(90) = {}", r[0].of.i64);

        for (a, b) in [(7, 2), (1, 0)] {
            match call(div, &mut [Val::i32(a), Val::i32(b)], &mut r) {
                Ok(()) => println!("div({a}, {b}) = {}", r[0].of.i32),
                Err(trap) => println!("div({a}, {b}) trapped: {trap}"),
            }
        }
        for addr in [16, 65536] {
            match call(peek, &mut [Val::i32(addr)], &mut r) {
                Ok(()) => println!("peek({addr}) = {}", r[0].of.i32),
                Err(trap) => println!("peek({addr}) trapped: {trap}"),
            }
        }

        wasm_extern_vec_delete(&mut exports);
        wasm_instance_delete(instance);
        wasm_func_delete(log_func);
        wasm_module_delete(module);
        wasm_byte_vec_delete(&mut binary);
        wasm_store_delete(store);
        wasm_engine_delete(engine);
    }
}
