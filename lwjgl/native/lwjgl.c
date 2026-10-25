/* What warmc's LWJGL needs in C beside GLFW and OpenGL themselves: GLFW's
   callbacks, one C function per kind that calls the program's export of that
   kind (org.lwjgl.glfw.WarmcUpcalls), which calls the Java callback set for
   it.

   The program is the instance warmc's WASI host made of it,
   warmc_wasi_instance; its functions are called with warmc_invoke, the
   integer arguments in iargs and the floating-point ones in every other
   word of fargs. */
#include <stdint.h>
#include <string.h>
#include <GLFW/glfw3.h>
#include "warm_rt.h"

typedef struct {
  const char *name;
  FuncRef fr;
} Upcall;

static void upcall(Upcall *u, const u64 *iargs, const double *fargs, int nf) {
  if (!u->fr.code) {
    const ExportDesc *e = warmc_find_export(warmc_wasi_instance, u->name);
    if (!e) return;
    u->fr = warmc_export_func(warmc_wasi_instance, e);
  }
  u64 in[8] = { 0 }, f[WARMC_FARGS_WORDS] = { 0 }, ret[WARMC_RETBUF_WORDS] = { 0 };
  memcpy(in, iargs, sizeof in);
  for (int i = 0; i < nf; i++) memcpy(&f[2 * i], &fargs[i], sizeof(double));
  warmc_invoke(u->fr.code, (WarmInstance *)u->fr.instance, in, f, ret);
}

#define I(x) ((u64)(uint32_t)(x))
#define P(x) ((u64)(uintptr_t)(x))

static Upcall error_up = { "lwjgl_glfw_error" };
static void error_cb(int error, const char *description) {
  u64 a[8] = { I(error), P(description) };
  upcall(&error_up, a, 0, 0);
}

static Upcall key_up = { "lwjgl_glfw_key" };
static void key_cb(GLFWwindow *w, int key, int scancode, int action, int mods) {
  u64 a[8] = { P(w), I(key), I(scancode), I(action), I(mods) };
  upcall(&key_up, a, 0, 0);
}

static Upcall char_up = { "lwjgl_glfw_char" };
static void char_cb(GLFWwindow *w, unsigned int codepoint) {
  u64 a[8] = { P(w), I(codepoint) };
  upcall(&char_up, a, 0, 0);
}

static Upcall cursor_pos_up = { "lwjgl_glfw_cursor_pos" };
static void cursor_pos_cb(GLFWwindow *w, double x, double y) {
  u64 a[8] = { P(w) };
  double f[2] = { x, y };
  upcall(&cursor_pos_up, a, f, 2);
}

static Upcall mouse_button_up = { "lwjgl_glfw_mouse_button" };
static void mouse_button_cb(GLFWwindow *w, int button, int action, int mods) {
  u64 a[8] = { P(w), I(button), I(action), I(mods) };
  upcall(&mouse_button_up, a, 0, 0);
}

static Upcall scroll_up = { "lwjgl_glfw_scroll" };
static void scroll_cb(GLFWwindow *w, double x, double y) {
  u64 a[8] = { P(w) };
  double f[2] = { x, y };
  upcall(&scroll_up, a, f, 2);
}

static Upcall framebuffer_size_up = { "lwjgl_glfw_framebuffer_size" };
static void framebuffer_size_cb(GLFWwindow *w, int width, int height) {
  u64 a[8] = { P(w), I(width), I(height) };
  upcall(&framebuffer_size_up, a, 0, 0);
}

static Upcall window_size_up = { "lwjgl_glfw_window_size" };
static void window_size_cb(GLFWwindow *w, int width, int height) {
  u64 a[8] = { P(w), I(width), I(height) };
  upcall(&window_size_up, a, 0, 0);
}

/* the kinds as WarmcUpcalls numbers them */
void warmc_lwjgl_set_callback(GLFWwindow *w, int32_t kind, int32_t on) {
  switch (kind) {
    case 0: glfwSetErrorCallback(on ? error_cb : NULL); break;
    case 1: glfwSetKeyCallback(w, on ? key_cb : NULL); break;
    case 2: glfwSetCharCallback(w, on ? char_cb : NULL); break;
    case 3: glfwSetCursorPosCallback(w, on ? cursor_pos_cb : NULL); break;
    case 4: glfwSetMouseButtonCallback(w, on ? mouse_button_cb : NULL); break;
    case 5: glfwSetScrollCallback(w, on ? scroll_cb : NULL); break;
    case 6: glfwSetFramebufferSizeCallback(w, on ? framebuffer_size_cb : NULL); break;
    case 7: glfwSetWindowSizeCallback(w, on ? window_size_cb : NULL); break;
  }
}
