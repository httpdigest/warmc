/* warmc's additions to the WebAssembly C API (wasm.h): what the standard
   header has no word for -- how a module is compiled, why one does not
   compile or link, WASI preview 1, a module's custom sections.  Everything
   else is wasm.h's, as libwarmc implements it. */

#ifndef WARM_H
#define WARM_H

#include "wasm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define own

/* Value kinds wasm.h has no name for: what wasm_valtype_kind says of a v128
   or of a reference outside the extern and func hierarchies.  wasm_val_t
   cannot hold a v128, so a call that passes or returns one traps. */
enum warm_valkind_enum {
  WARM_V128 = 4,
  WARM_ANYREF = 130,
  WARM_EXNREF = 131,
};

/* ---- compiling --------------------------------------------------------- */

/* The optimisation level modules are compiled at, as warmc's -O: 0 to 3,
   and 2 unless this says otherwise.  Fails for any other. */
WASM_API_EXTERN bool warm_config_opt_level_set(wasm_config_t*, int level);

/* One more of warmc's compile options, as its command line spells it:
   -O<n>, or -f<pass> / -fno-<pass> (see warmc --help).  Options apply in
   the order they were added, after the level.  An unknown one makes every
   compile with the config fail, saying which. */
WASM_API_EXTERN void warm_config_flag_add(wasm_config_t*, const char* flag);
/* Every function import of `module` is a call of a C function, as warmc's
   --native-module makes it: no host function, no thunk, the arguments in
   the registers and stack slots C's convention has them in, the C function
   the one warm_config_native_function_add names after the import.  Such an
   import takes i32, i64, f32, f64 and byte arrays -- (array i8) or (array
   (mut i8)), each two of C's arguments, `uint8_t *bytes, size_t count`,
   valid while the call runs and written in place by a C function that
   writes them; a null traps -- and returns at most one of the four numbers;
   it can only be called (no ref.func, table, export or start of one).  It
   is no import of the C API's: wasm_module_imports does not list it, and
   an instantiation binds nothing to it.  A module that has one cannot be
   serialised.  AArch64's alone, as warmc's. */
WASM_API_EXTERN void warm_config_native_module_add(wasm_config_t*, const char* module);
/* The C function native imports named `name` call.  false where the name is
   empty or the runtime's own (the module's code reaches both by name). */
WASM_API_EXTERN bool warm_config_native_function_add(wasm_config_t*, const char* name, void* fn);

/* How many threads compile a module's functions at once: each holds the
   code of the function it compiles, so fewer use less memory at the peak
   of a compile and take longer.  0, the default, is as many as the machine
   has cores, up to a limit, and fewer for a small module. */
WASM_API_EXTERN void warm_config_compile_threads_set(wasm_config_t*, uint32_t n);

/* Tiered compiling: wasm_module_new compiles at -O0, which takes a fraction
   of the time, and hands the module back; the module is compiled again at
   the configured level on a thread of the library's, and its calls go to
   the optimised code once that is in place, instances made before then
   too.  A call already running when it arrives runs on in the -O0 code
   until it returns -- a long loop in a function that has started stays
   unoptimised -- so a program that runs most of its time in one call, as
   a WASI command does, is better compiled untiered.  Off by default. */
WASM_API_EXTERN void warm_config_tiered_set(wasm_config_t*, bool enable);

/* wasm_module_new, saying why when it fails: the message (NUL-terminated,
   which its size counts) is the compiler's diagnostic, such as
   "invalid: type mismatch in i32.add".  error_out may be NULL. */
WASM_API_EXTERN own wasm_module_t* warm_module_new(
  wasm_store_t*, const wasm_byte_vec_t* binary, own wasm_message_t* error_out);

/* Whether the module's code at the configured level is in place, waiting
   until it is with wait: true at once for an untiered module, false when
   it never will be (its compile at that level failed, and the module runs
   its -O0 code). */
WASM_API_EXTERN bool warm_module_optimized(const wasm_module_t*, bool wait);
/* A module in the text format, assembled to the binary one; false, with
   error_out set when it is not NULL, when it does not assemble. */
WASM_API_EXTERN bool warm_wat2wasm(
  const char* text, size_t len, own wasm_byte_vec_t* out,
  own wasm_message_t* error_out);

/* The module's custom sections named name: how many it carries, and the
   index-th one's payload in *data and *size (NULL and 0 when there is no
   index-th).  The bytes live as long as the module.  By default the debug
   information (the name section, DWARF's .debug_*) is left out, as
   warmc's --custom-sections=default leaves it. */
WASM_API_EXTERN size_t warm_module_custom_section(
  const wasm_module_t*, const char* name, size_t index,
  const wasm_byte_t** data, size_t* size);

/* ---- instantiating ----------------------------------------------------- */

/* wasm_instance_new, saying why an instance cannot be made: an import of
   the wrong kind or type, or too few given, in error_out (when not NULL);
   a trap in its start function comes back in *trap as with wasm.h. */
WASM_API_EXTERN own wasm_instance_t* warm_instance_new(
  wasm_store_t*, const wasm_module_t*, const wasm_extern_vec_t* imports,
  own wasm_trap_t** trap, own wasm_message_t* error_out);

/* ---- calling ------------------------------------------------------------- */

/* A value as wasm has it, in sixteen bytes: an i32, i64, f32 or f64 in its
   first bytes, a v128 in all of them, a reference as wasm_val_t holds one */
typedef union warm_val_raw_t {
  int32_t i32;
  int64_t i64;
  float32_t f32;
  float64_t f64;
  uint8_t v128[16];
  wasm_ref_t* ref;
} warm_val_raw_t;

/* wasm_func_call with no vectors, and v128 among the values: vals holds
   the parameters, in order, and gets the results in their place; nvals is
   its length, which must be at least the larger of the two counts.  A
   reference in a parameter stays the caller's; one in a result is the
   caller's to delete.  The types are the function's, unchecked. */
WASM_API_EXTERN own wasm_trap_t* warm_func_call_raw(
  const wasm_func_t*, warm_val_raw_t* vals, size_t nvals);

/* ---- traps --------------------------------------------------------------- */

/* Whether the trap is a WASI program's proc_exit, and its status if so. */
WASM_API_EXTERN bool warm_trap_exit_status(const wasm_trap_t*, int* status);

/* ---- WASI preview 1 ---------------------------------------------------- */

/* What a WASI program sees of the world: its arguments, its environment,
   the directories it may open, and stdin, stdout and stderr, which are the
   host's.  The host is warmc's own (rt/wasi.c), sandboxed as a program
   warmc --wasi links is: no ".." out of a preopened directory, symlinks
   resolved by hand. */
typedef struct warm_wasi_config_t warm_wasi_config_t;

WASM_API_EXTERN own warm_wasi_config_t* warm_wasi_config_new(void);
WASM_API_EXTERN void warm_wasi_config_delete(own warm_wasi_config_t*);
/* argv[0] is the program's name, as a C program's is */
WASM_API_EXTERN void warm_wasi_config_set_argv(
  warm_wasi_config_t*, size_t argc, const char* const argv[]);
/* one more NAME=VALUE in the environment, which starts empty */
WASM_API_EXTERN void warm_wasi_config_env_add(warm_wasi_config_t*, const char* entry);
/* the host's whole environment, ahead of what env_add adds */
WASM_API_EXTERN void warm_wasi_config_inherit_env(warm_wasi_config_t*);
/* a host directory the program may open files under, as fd 3, 4 and on in
   the order they are added; guest_path is the name it sees (NULL: the
   host path) */
WASM_API_EXTERN bool warm_wasi_config_preopen_dir(
  warm_wasi_config_t*, const char* host_path, const char* guest_path);

/* a TCP socket listening on [ADDR:]PORT (ADDR 127.0.0.1 unless given),
   which the program accepts connections on: numbered after the
   directories, as warmc --wasi's --tcplisten makes it */
WASM_API_EXTERN void warm_wasi_config_tcplisten(warm_wasi_config_t*, const char* addr);

/* The imports of a module, as wasm_instance_new takes them: each import of
   wasi_snapshot_preview1 bound to the WASI host, with config's world, and
   every other NULL for the caller to fill in.  One WASI world is live in a
   process at a time: the last this made.  False when config cannot be set
   up (a directory that cannot be opened). */
WASM_API_EXTERN bool warm_wasi_imports(
  wasm_store_t*, const wasm_module_t*, const warm_wasi_config_t*,
  own wasm_extern_vec_t* out);

#undef own

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
