"""test/shared/basic.wat as a shared library, called through Python's ctypes:
each scalar type in and out, both register classes at once, several
results through pointers, a trap that the call survives, linear memory, a
host import with its ctx, and recursion stopped at the stack limit.

Usage: basic.py LIBRARY
"""
import ctypes as C
import sys

lib = C.CDLL(sys.argv[1])
failures = []


def fn(name, res, *args):
    f = getattr(lib, name)
    f.restype = res
    f.argtypes = list(args)
    return f


def check(what, got, want):
    if got != want:
        failures.append("%s: got %r, want %r" % (what, got, want))


TWICE = C.CFUNCTYPE(C.c_int64, C.c_void_p, C.c_int64)
seen = []


def twice(ctx, v):
    seen.append(C.cast(ctx, C.c_char_p).value)
    return 2 * v


twice_c = TWICE(twice)
ctx = C.c_char_p(b"python")
bind = fn("basic_bind_import", C.c_int, C.c_char_p, C.c_char_p, C.c_void_p,
          C.c_void_p)
check("bind env.twice", bind(b"env", b"twice", C.cast(twice_c, C.c_void_p),
                             C.cast(ctx, C.c_void_p)), 0)
check("bind an import the module lacks", bind(b"env", b"nope", None, None), 1)

last = fn("basic_last_trap", C.c_uint32)
name = fn("basic_trap_name", C.c_char_p, C.c_uint32)

check("add", fn("add", C.c_int32, C.c_int32, C.c_int32)(40, 2), 42)
check("add wraps", fn("add", C.c_int32, C.c_int32, C.c_int32)(0x7fffffff, 1),
      -0x80000000)
check("fib", fn("fib", C.c_int64, C.c_int32)(30), 832040)
check("mix", fn("mix", C.c_double, C.c_double, C.c_int32, C.c_float,
                C.c_int64)(2.5, 4, 0.25, 3), 12.75)
check("half", fn("half", C.c_float, C.c_float)(3.0), 1.5)

quot = fn("quot", C.c_int32, C.c_int32, C.c_int32)
check("quot", (quot(7, 2), last()), (3, 0))
check("quot by zero", (quot(1, 0), last()), (0, 3))
check("its name", name(3), b"integer divide by zero")
check("the next call clears it", (quot(-9, 3), last()), (-3, 0))

r0, r1 = C.c_int32(), C.c_int64()
fn("divmod", None, C.c_int32, C.c_int32, C.POINTER(C.c_int32),
   C.POINTER(C.c_int64))(17, 5, C.byref(r0), C.byref(r1))
check("divmod", (r0.value, r1.value), (3, 2))

check("seven", fn("seven", C.c_int64, *[C.c_int64] * 7)(1, 2, 3, 4, 5, 6, 7),
      7654321)
# past the registers, in eight-byte and in four-byte parameters
check("wide", fn("wide", C.c_double, *[C.c_int64] * 12, *[C.c_double] * 10)(
          *range(1, 13), *[0.5 * k for k in range(1, 11)]), 1172.5)
check("narrow", fn("narrow", C.c_float, *[C.c_int32] * 11, *[C.c_float] * 11)(
          *range(1, 12), *[0.5 * k for k in range(1, 12)]), 1122.0)
r0, r1 = C.c_int32(), C.c_int64()
fn("wide_pair", None, *[C.c_int32] * 9, C.POINTER(C.c_int32),
   C.POINTER(C.c_int64))(*range(1, 10), C.byref(r0), C.byref(r1))
check("wide_pair", (r0.value, r1.value), (8, 45))
check("twice_plus_one", fn("twice_plus_one", C.c_int64, C.c_int64)(20), 41)
check("the import's ctx", seen, [b"python"])

size = C.c_size_t()
mem = fn("basic_memory", C.POINTER(C.c_uint8), C.POINTER(C.c_size_t))(
    C.byref(size))
check("memory size", size.value, 65536)
for i in range(10):
    mem[100 + i] = i + 1
check("sum of what Python wrote", fn("sum", C.c_int32, C.c_int32, C.c_int32)(
    100, 10), 55)

deep = fn("deep", C.c_int32, C.c_int32)
check("deep", (deep(1000), last()), (1000, 0))
check("too deep", (deep(100000000), last()), (0, 11))
check("and after it", deep(10), 10)

check("add-one", fn("add_one", C.c_int32, C.c_int32)(41), 42)
check("int", fn("basic_int", C.c_int32)(), 42)
check("no symbol for v128", hasattr(lib, "splat"), False)
check("calls", fn("calls", C.c_int32)(), 2)

for f in failures:
    print("FAIL[shared/python] " + f)
if failures:
    sys.exit(1)
print("shared/python: scalars, both register classes, results through "
      "pointers, traps, memory, an import and the stack limit")
