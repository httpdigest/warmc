/* C of test/lwjgl/jni's own (lwjgl/build.py --c), which it calls through
   org.lwjgl.system.JNI at the addresses dlsym finds, and through libffi:
   results narrower than a register, arrays C reads and writes, arguments
   past the registers, floating point beside integers, and the program
   called back. */
#include <stddef.h>
#include <stdint.h>

/* A bool, a byte and a short whose register is not clear above them, which
   C's convention leaves undefined and a compiler clears, so written here:
   the argument's low bits, every other one set. */
__asm__(".globl _jni_bool_dirty\n.p2align 2\n_jni_bool_dirty:\n"
        "  and w0, w0, #0xff\n  orr w0, w0, #0xffffff00\n  ret\n"
        ".globl _jni_byte_dirty\n.p2align 2\n_jni_byte_dirty:\n"
        "  and w0, w0, #0xff\n  orr w0, w0, #0xffffff00\n  ret\n"
        ".globl _jni_short_dirty\n.p2align 2\n_jni_short_dirty:\n"
        "  and w0, w0, #0xffff\n  orr w0, w0, #0xffff0000\n  ret\n");

/* each element times 3, plus k; and whether the array was NULL */
static int last_null = -1;
#define SCALE(name, T, k)                                  \
  void name(int n, T *a) {                                 \
    last_null = a == NULL;                                 \
    for (int i = 0; a && i < n; i++) a[i] = (T)(a[i] * 3 + k); \
  }
SCALE(jni_scale_s, int16_t, 1)
SCALE(jni_scale_i, int32_t, 1)
SCALE(jni_scale_l, int64_t, 1)
SCALE(jni_scale_f, float, 0.5f)
SCALE(jni_scale_d, double, 0.25)

int jni_last_null(void) { return last_null; }

void jni_inc(int n, int32_t *a) { a[0] += n; }

/* JNI's callPPPPI of 23 arguments, 19 ints and 4 arrays: past the first eight
   in registers, on the stack, each at its own size.  The ints weighted by
   their place, the arrays' first elements by powers of ten, and then those
   elements written. */
int jni_many(int a0, int32_t *p1, int64_t *p2, int a3, int a4, int a5, int a6, int a7, int a8,
             int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, int a17,
             int a18, int a19, int a20, int32_t *p21, int64_t *p22) {
  int v[] = { a0, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20 };
  int sum = 0;
  for (int i = 0; i < 19; i++) sum += (i + 1) * v[i];
  sum += p1[0] * 100 + (int)p2[0] * 1000 + p21[0] * 10000 + (int)p22[0] * 100000;
  p1[0] = -1;
  p2[0] = -2;
  p21[0] = -3;
  p22[0] = -4;
  return sum;
}

double jni_scaled(double x, int k) { return x * k; }

static double last;
void jni_floats(float a, float b, float c, float d) { last = a + 2.0 * b + 4.0 * c + 8.0 * d; }
double jni_last(void) { return last; }

/* the program called back: through a GLFW error callback, once from a
   function an invoke calls, once from one a call does, and twice from one;
   a scroll callback, whose arguments are doubles; and an allocate callback,
   which returns one */
void jni_error_back(void (*f)(int, const char *), int code) { f(code, "from C"); }
void jni_error_back_call(void (*f)(int, const char *), intptr_t code) { f((int)code, "through a call"); }
void jni_error_twice(void (*f)(int, const char *), int code) {
  f(code, "first");
  f(code + 1, "second");
}
void jni_scroll_back(void *window, void (*f)(void *, double, double)) { f(window, 1.5, -2.25); }
intptr_t jni_alloc_back(intptr_t (*f)(size_t, void *), size_t size, void *user) { return f(size, user) + 1; }
