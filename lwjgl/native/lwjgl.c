/* What warmc's LWJGL needs in C beside GLFW, OpenGL and the C library: the
   native methods of LWJGL's own classes that are not a library's function,
   each lwjgl_CLASS_METHOD (lwjgl/teavm/LwjglTransformer.java), and the
   closures LWJGL's callbacks are called through (org.lwjgl.system.Upcalls,
   lwjgl/platform).

   LWJGL's libffi is the system's (<ffi/ffi.h>): LibFFI's types, ABIs and
   functions are its, and FFICIF and FFIClosure have its layout, which LWJGL
   reads from here.

   The program is the instance warmc's WASI host made of it,
   warmc_wasi_instance; its export lwjgl_upcall is called with warmc_invoke,
   the integer arguments in iargs. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <ffi/ffi.h>
#include "warm_rt.h"

#define ADDR(x) ((int64_t)(intptr_t)(x))

/* ------------------------------------------------------------- upcalls */

static FuncRef upcall_fr;

/* libffi's handler of every closure: the callback numbered user_data, handed
   where its return value goes and the addresses of its arguments */
static void upcall(ffi_cif *cif, void *ret, void **args, void *user_data) {
  (void)cif;
  if (!upcall_fr.code) {
    const ExportDesc *e = warmc_find_export(warmc_wasi_instance, "lwjgl_upcall");
    if (!e) return;
    upcall_fr = warmc_export_func(warmc_wasi_instance, e);
  }
  u64 in[8] = { (u64)(uint32_t)(intptr_t)user_data, (u64)(uintptr_t)ret, (u64)(uintptr_t)args };
  u64 f[WARMC_FARGS_WORDS] = { 0 }, r[WARMC_RETBUF_WORDS];
  warmc_invoke(upcall_fr.code, (WarmInstance *)upcall_fr.instance, in, f, r);
}

/* The C function that calls callback number for the CIF cif, or 0.  libffi
   frees a closure by its writable address, LWJGL by its code's, so the two
   are kept together. */
typedef struct Closure {
  void *code;
  ffi_closure *closure;
  struct Closure *next;
} Closure;

static Closure *closures;

int64_t warmc_lwjgl_upcall_create(ffi_cif *cif, int32_t number) {
  void *code;
  ffi_closure *c = ffi_closure_alloc(sizeof(ffi_closure), &code);
  if (!c) return 0;
  if (ffi_prep_closure_loc(c, cif, upcall, (void *)(intptr_t)number, code) != FFI_OK) {
    ffi_closure_free(c);
    return 0;
  }
  Closure *k = malloc(sizeof *k);
  if (!k) {
    ffi_closure_free(c);
    return 0;
  }
  k->code = code;
  k->closure = c;
  k->next = closures;
  closures = k;
  return ADDR(code);
}

void warmc_lwjgl_upcall_free(int64_t function) {
  for (Closure **p = &closures; *p; p = &(*p)->next) {
    if ((*p)->code == (void *)(intptr_t)function) {
      Closure *k = *p;
      *p = k->next;
      ffi_closure_free(k->closure);
      free(k);
      return;
    }
  }
}

/* ------------------------------------------------- LWJGL's own natives */

int32_t lwjgl_MemoryAccessJNI_getPointerSize(void) { return (int32_t)sizeof(void *); }

int32_t lwjgl_MemoryUtil_ngetPageSize(void) { return (int32_t)getpagesize(); }

int32_t lwjgl_MemoryUtil_ngetCacheLineSize(void) {
  int64_t n = 0;
  size_t size = sizeof n;
  return sysctlbyname("hw.cachelinesize", &n, &size, NULL, 0) == 0 && n > 0 ? (int32_t)n : 64;
}

/* the offsets of a struct's members, then its alignment, into buffer, and
   its size */
int32_t lwjgl_FFICIF_offsets(int32_t *buffer) {
  buffer[0] = offsetof(ffi_cif, abi);
  buffer[1] = offsetof(ffi_cif, nargs);
  buffer[2] = offsetof(ffi_cif, arg_types);
  buffer[3] = offsetof(ffi_cif, rtype);
  buffer[4] = offsetof(ffi_cif, bytes);
  buffer[5] = offsetof(ffi_cif, flags);
  buffer[6] = _Alignof(ffi_cif);
  return sizeof(ffi_cif);
}

int32_t lwjgl_FFIClosure_offsets(int32_t *buffer) {
  buffer[0] = offsetof(ffi_closure, cif);
  buffer[1] = offsetof(ffi_closure, fun);
  buffer[2] = offsetof(ffi_closure, user_data);
  buffer[3] = _Alignof(ffi_closure);
  return sizeof(ffi_closure);
}

/* LibFFI: an ABI the system's libffi has not is -1, as LWJGL's own has it */
int32_t lwjgl_LibFFI_FFI_TYPE_DOUBLE(void) { return FFI_TYPE_LONGDOUBLE; }
int32_t lwjgl_LibFFI_FFI_FIRST_ABI(void) { return FFI_FIRST_ABI; }
int32_t lwjgl_LibFFI_FFI_SYSV(void) { return FFI_SYSV; }
int32_t lwjgl_LibFFI_FFI_WIN64(void) { return FFI_WIN64; }
int32_t lwjgl_LibFFI_FFI_LAST_ABI(void) { return FFI_LAST_ABI; }
int32_t lwjgl_LibFFI_FFI_DEFAULT_ABI(void) { return FFI_DEFAULT_ABI; }
#define NO_ABI(name) \
  int32_t lwjgl_LibFFI_##name(void) { return -1; }
NO_ABI(FFI_GNUW64)
NO_ABI(FFI_UNIX64)
NO_ABI(FFI_EFI64)
NO_ABI(FFI_STDCALL)
NO_ABI(FFI_THISCALL)
NO_ABI(FFI_FASTCALL)
NO_ABI(FFI_MS_CDECL)
NO_ABI(FFI_PASCAL)
NO_ABI(FFI_REGISTER)
NO_ABI(FFI_VFP)

#define TYPE(name) \
  int64_t lwjgl_LibFFI_nffi_type_##name(void) { return ADDR(&ffi_type_##name); }
TYPE(void)
TYPE(uint8)
TYPE(sint8)
TYPE(uint16)
TYPE(sint16)
TYPE(uint32)
TYPE(sint32)
TYPE(uint64)
TYPE(sint64)
TYPE(uchar)
TYPE(schar)
TYPE(ushort)
TYPE(sshort)
TYPE(uint)
TYPE(sint)
TYPE(ulong)
TYPE(slong)
TYPE(float)
TYPE(double)
TYPE(longdouble)
TYPE(pointer)

int32_t lwjgl_LibFFI_ffi_get_default_abi(void) { return FFI_DEFAULT_ABI; }

int32_t lwjgl_LibFFI_nffi_prep_cif(int64_t cif, int32_t abi, int32_t nargs, int64_t rtype, int64_t atypes) {
  return ffi_prep_cif((ffi_cif *)(intptr_t)cif, (ffi_abi)abi, (unsigned)nargs, (ffi_type *)(intptr_t)rtype,
                      (ffi_type **)(intptr_t)atypes);
}

int32_t lwjgl_LibFFI_nffi_prep_cif_var(int64_t cif, int32_t abi, int32_t nfixedargs, int32_t ntotalargs,
                                       int64_t rtype, int64_t atypes) {
  return ffi_prep_cif_var((ffi_cif *)(intptr_t)cif, (ffi_abi)abi, (unsigned)nfixedargs, (unsigned)ntotalargs,
                          (ffi_type *)(intptr_t)rtype, (ffi_type **)(intptr_t)atypes);
}

void lwjgl_LibFFI_nffi_call(int64_t cif, int64_t fn, int64_t rvalue, int64_t avalue) {
  ffi_call((ffi_cif *)(intptr_t)cif, (void (*)(void))(intptr_t)fn, (void *)(intptr_t)rvalue,
           (void **)(intptr_t)avalue);
}

int32_t lwjgl_LibFFI_nffi_get_struct_offsets(int32_t abi, int64_t struct_type, int64_t offsets) {
  return ffi_get_struct_offsets((ffi_abi)abi, (ffi_type *)(intptr_t)struct_type, (size_t *)(intptr_t)offsets);
}

int64_t lwjgl_LibFFI_ffi_get_closure_size(void) { return sizeof(ffi_closure); }

int64_t lwjgl_LibFFI_nffi_closure_alloc(int64_t size, int64_t code) {
  return ADDR(ffi_closure_alloc((size_t)size, (void **)(intptr_t)code));
}

void lwjgl_LibFFI_nffi_closure_free(int64_t writable) { ffi_closure_free((void *)(intptr_t)writable); }

int32_t lwjgl_LibFFI_nffi_prep_closure_loc(int64_t closure, int64_t cif, int64_t fun, int64_t user_data,
                                           int64_t codeloc) {
  return ffi_prep_closure_loc((ffi_closure *)(intptr_t)closure, (ffi_cif *)(intptr_t)cif,
                              (void (*)(ffi_cif *, void *, void **, void *))(intptr_t)fun,
                              (void *)(intptr_t)user_data, (void *)(intptr_t)codeloc);
}
