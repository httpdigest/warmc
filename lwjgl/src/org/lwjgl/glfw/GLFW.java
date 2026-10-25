package org.lwjgl.glfw;

import static org.lwjgl.system.MemoryStack.stackGet;
import static org.lwjgl.system.MemoryUtil.NULL;
import static org.lwjgl.system.MemoryUtil.memAddress;
import static org.lwjgl.system.MemoryUtil.memAddressSafe;
import static org.lwjgl.system.MemoryUtil.memUTF8Safe;

import java.nio.ByteBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.MemoryUtil;
import org.teavm.interop.Import;

/**
 * GLFW 3.4: LWJGL's binding, for warmc.  Each function calls GLFW's of its
 * name directly, a --native-module import of module "lwjgl" that warmc makes a
 * call of the C function and the system linker resolves against GLFW.
 */
public class GLFW {
    public static final int GLFW_VERSION_MAJOR = 3;
    public static final int GLFW_VERSION_MINOR = 4;
    public static final int GLFW_VERSION_REVISION = 0;
    public static final int GLFW_TRUE = 1;
    public static final int GLFW_FALSE = 0;
    public static final int GLFW_RELEASE = 0;
    public static final int GLFW_PRESS = 1;
    public static final int GLFW_REPEAT = 2;
    public static final int GLFW_HAT_CENTERED = 0;
    public static final int GLFW_HAT_UP = 1;
    public static final int GLFW_HAT_RIGHT = 2;
    public static final int GLFW_HAT_DOWN = 4;
    public static final int GLFW_HAT_LEFT = 8;
    public static final int GLFW_KEY_UNKNOWN = -1;
    public static final int GLFW_KEY_SPACE = 32;
    public static final int GLFW_KEY_APOSTROPHE = 39;
    public static final int GLFW_KEY_COMMA = 44;
    public static final int GLFW_KEY_MINUS = 45;
    public static final int GLFW_KEY_PERIOD = 46;
    public static final int GLFW_KEY_SLASH = 47;
    public static final int GLFW_KEY_0 = 48;
    public static final int GLFW_KEY_1 = 49;
    public static final int GLFW_KEY_2 = 50;
    public static final int GLFW_KEY_3 = 51;
    public static final int GLFW_KEY_4 = 52;
    public static final int GLFW_KEY_5 = 53;
    public static final int GLFW_KEY_6 = 54;
    public static final int GLFW_KEY_7 = 55;
    public static final int GLFW_KEY_8 = 56;
    public static final int GLFW_KEY_9 = 57;
    public static final int GLFW_KEY_SEMICOLON = 59;
    public static final int GLFW_KEY_EQUAL = 61;
    public static final int GLFW_KEY_A = 65;
    public static final int GLFW_KEY_B = 66;
    public static final int GLFW_KEY_C = 67;
    public static final int GLFW_KEY_D = 68;
    public static final int GLFW_KEY_E = 69;
    public static final int GLFW_KEY_F = 70;
    public static final int GLFW_KEY_G = 71;
    public static final int GLFW_KEY_H = 72;
    public static final int GLFW_KEY_I = 73;
    public static final int GLFW_KEY_J = 74;
    public static final int GLFW_KEY_K = 75;
    public static final int GLFW_KEY_L = 76;
    public static final int GLFW_KEY_M = 77;
    public static final int GLFW_KEY_N = 78;
    public static final int GLFW_KEY_O = 79;
    public static final int GLFW_KEY_P = 80;
    public static final int GLFW_KEY_Q = 81;
    public static final int GLFW_KEY_R = 82;
    public static final int GLFW_KEY_S = 83;
    public static final int GLFW_KEY_T = 84;
    public static final int GLFW_KEY_U = 85;
    public static final int GLFW_KEY_V = 86;
    public static final int GLFW_KEY_W = 87;
    public static final int GLFW_KEY_X = 88;
    public static final int GLFW_KEY_Y = 89;
    public static final int GLFW_KEY_Z = 90;
    public static final int GLFW_KEY_LEFT_BRACKET = 91;
    public static final int GLFW_KEY_BACKSLASH = 92;
    public static final int GLFW_KEY_RIGHT_BRACKET = 93;
    public static final int GLFW_KEY_GRAVE_ACCENT = 96;
    public static final int GLFW_KEY_WORLD_1 = 161;
    public static final int GLFW_KEY_WORLD_2 = 162;
    public static final int GLFW_KEY_ESCAPE = 256;
    public static final int GLFW_KEY_ENTER = 257;
    public static final int GLFW_KEY_TAB = 258;
    public static final int GLFW_KEY_BACKSPACE = 259;
    public static final int GLFW_KEY_INSERT = 260;
    public static final int GLFW_KEY_DELETE = 261;
    public static final int GLFW_KEY_RIGHT = 262;
    public static final int GLFW_KEY_LEFT = 263;
    public static final int GLFW_KEY_DOWN = 264;
    public static final int GLFW_KEY_UP = 265;
    public static final int GLFW_KEY_PAGE_UP = 266;
    public static final int GLFW_KEY_PAGE_DOWN = 267;
    public static final int GLFW_KEY_HOME = 268;
    public static final int GLFW_KEY_END = 269;
    public static final int GLFW_KEY_CAPS_LOCK = 280;
    public static final int GLFW_KEY_SCROLL_LOCK = 281;
    public static final int GLFW_KEY_NUM_LOCK = 282;
    public static final int GLFW_KEY_PRINT_SCREEN = 283;
    public static final int GLFW_KEY_PAUSE = 284;
    public static final int GLFW_KEY_F1 = 290;
    public static final int GLFW_KEY_F2 = 291;
    public static final int GLFW_KEY_F3 = 292;
    public static final int GLFW_KEY_F4 = 293;
    public static final int GLFW_KEY_F5 = 294;
    public static final int GLFW_KEY_F6 = 295;
    public static final int GLFW_KEY_F7 = 296;
    public static final int GLFW_KEY_F8 = 297;
    public static final int GLFW_KEY_F9 = 298;
    public static final int GLFW_KEY_F10 = 299;
    public static final int GLFW_KEY_F11 = 300;
    public static final int GLFW_KEY_F12 = 301;
    public static final int GLFW_KEY_F13 = 302;
    public static final int GLFW_KEY_F14 = 303;
    public static final int GLFW_KEY_F15 = 304;
    public static final int GLFW_KEY_F16 = 305;
    public static final int GLFW_KEY_F17 = 306;
    public static final int GLFW_KEY_F18 = 307;
    public static final int GLFW_KEY_F19 = 308;
    public static final int GLFW_KEY_F20 = 309;
    public static final int GLFW_KEY_F21 = 310;
    public static final int GLFW_KEY_F22 = 311;
    public static final int GLFW_KEY_F23 = 312;
    public static final int GLFW_KEY_F24 = 313;
    public static final int GLFW_KEY_F25 = 314;
    public static final int GLFW_KEY_KP_0 = 320;
    public static final int GLFW_KEY_KP_1 = 321;
    public static final int GLFW_KEY_KP_2 = 322;
    public static final int GLFW_KEY_KP_3 = 323;
    public static final int GLFW_KEY_KP_4 = 324;
    public static final int GLFW_KEY_KP_5 = 325;
    public static final int GLFW_KEY_KP_6 = 326;
    public static final int GLFW_KEY_KP_7 = 327;
    public static final int GLFW_KEY_KP_8 = 328;
    public static final int GLFW_KEY_KP_9 = 329;
    public static final int GLFW_KEY_KP_DECIMAL = 330;
    public static final int GLFW_KEY_KP_DIVIDE = 331;
    public static final int GLFW_KEY_KP_MULTIPLY = 332;
    public static final int GLFW_KEY_KP_SUBTRACT = 333;
    public static final int GLFW_KEY_KP_ADD = 334;
    public static final int GLFW_KEY_KP_ENTER = 335;
    public static final int GLFW_KEY_KP_EQUAL = 336;
    public static final int GLFW_KEY_LEFT_SHIFT = 340;
    public static final int GLFW_KEY_LEFT_CONTROL = 341;
    public static final int GLFW_KEY_LEFT_ALT = 342;
    public static final int GLFW_KEY_LEFT_SUPER = 343;
    public static final int GLFW_KEY_RIGHT_SHIFT = 344;
    public static final int GLFW_KEY_RIGHT_CONTROL = 345;
    public static final int GLFW_KEY_RIGHT_ALT = 346;
    public static final int GLFW_KEY_RIGHT_SUPER = 347;
    public static final int GLFW_KEY_MENU = 348;
    public static final int GLFW_KEY_LAST = GLFW_KEY_MENU;
    public static final int GLFW_MOD_SHIFT = 0x0001;
    public static final int GLFW_MOD_CONTROL = 0x0002;
    public static final int GLFW_MOD_ALT = 0x0004;
    public static final int GLFW_MOD_SUPER = 0x0008;
    public static final int GLFW_MOD_CAPS_LOCK = 0x0010;
    public static final int GLFW_MOD_NUM_LOCK = 0x0020;
    public static final int GLFW_MOUSE_BUTTON_1 = 0;
    public static final int GLFW_MOUSE_BUTTON_2 = 1;
    public static final int GLFW_MOUSE_BUTTON_3 = 2;
    public static final int GLFW_MOUSE_BUTTON_4 = 3;
    public static final int GLFW_MOUSE_BUTTON_5 = 4;
    public static final int GLFW_MOUSE_BUTTON_6 = 5;
    public static final int GLFW_MOUSE_BUTTON_7 = 6;
    public static final int GLFW_MOUSE_BUTTON_8 = 7;
    public static final int GLFW_MOUSE_BUTTON_LAST = GLFW_MOUSE_BUTTON_8;
    public static final int GLFW_MOUSE_BUTTON_LEFT = GLFW_MOUSE_BUTTON_1;
    public static final int GLFW_MOUSE_BUTTON_RIGHT = GLFW_MOUSE_BUTTON_2;
    public static final int GLFW_MOUSE_BUTTON_MIDDLE = GLFW_MOUSE_BUTTON_3;
    public static final int GLFW_JOYSTICK_1 = 0;
    public static final int GLFW_JOYSTICK_2 = 1;
    public static final int GLFW_JOYSTICK_3 = 2;
    public static final int GLFW_JOYSTICK_4 = 3;
    public static final int GLFW_JOYSTICK_5 = 4;
    public static final int GLFW_JOYSTICK_6 = 5;
    public static final int GLFW_JOYSTICK_7 = 6;
    public static final int GLFW_JOYSTICK_8 = 7;
    public static final int GLFW_JOYSTICK_9 = 8;
    public static final int GLFW_JOYSTICK_10 = 9;
    public static final int GLFW_JOYSTICK_11 = 10;
    public static final int GLFW_JOYSTICK_12 = 11;
    public static final int GLFW_JOYSTICK_13 = 12;
    public static final int GLFW_JOYSTICK_14 = 13;
    public static final int GLFW_JOYSTICK_15 = 14;
    public static final int GLFW_JOYSTICK_16 = 15;
    public static final int GLFW_JOYSTICK_LAST = GLFW_JOYSTICK_16;
    public static final int GLFW_GAMEPAD_BUTTON_A = 0;
    public static final int GLFW_GAMEPAD_BUTTON_B = 1;
    public static final int GLFW_GAMEPAD_BUTTON_X = 2;
    public static final int GLFW_GAMEPAD_BUTTON_Y = 3;
    public static final int GLFW_GAMEPAD_BUTTON_LEFT_BUMPER = 4;
    public static final int GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER = 5;
    public static final int GLFW_GAMEPAD_BUTTON_BACK = 6;
    public static final int GLFW_GAMEPAD_BUTTON_START = 7;
    public static final int GLFW_GAMEPAD_BUTTON_GUIDE = 8;
    public static final int GLFW_GAMEPAD_BUTTON_LEFT_THUMB = 9;
    public static final int GLFW_GAMEPAD_BUTTON_RIGHT_THUMB = 10;
    public static final int GLFW_GAMEPAD_BUTTON_DPAD_UP = 11;
    public static final int GLFW_GAMEPAD_BUTTON_DPAD_RIGHT = 12;
    public static final int GLFW_GAMEPAD_BUTTON_DPAD_DOWN = 13;
    public static final int GLFW_GAMEPAD_BUTTON_DPAD_LEFT = 14;
    public static final int GLFW_GAMEPAD_BUTTON_LAST = GLFW_GAMEPAD_BUTTON_DPAD_LEFT;
    public static final int GLFW_GAMEPAD_BUTTON_CROSS = GLFW_GAMEPAD_BUTTON_A;
    public static final int GLFW_GAMEPAD_BUTTON_CIRCLE = GLFW_GAMEPAD_BUTTON_B;
    public static final int GLFW_GAMEPAD_BUTTON_SQUARE = GLFW_GAMEPAD_BUTTON_X;
    public static final int GLFW_GAMEPAD_BUTTON_TRIANGLE = GLFW_GAMEPAD_BUTTON_Y;
    public static final int GLFW_GAMEPAD_AXIS_LEFT_X = 0;
    public static final int GLFW_GAMEPAD_AXIS_LEFT_Y = 1;
    public static final int GLFW_GAMEPAD_AXIS_RIGHT_X = 2;
    public static final int GLFW_GAMEPAD_AXIS_RIGHT_Y = 3;
    public static final int GLFW_GAMEPAD_AXIS_LEFT_TRIGGER = 4;
    public static final int GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER = 5;
    public static final int GLFW_GAMEPAD_AXIS_LAST = GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER;
    public static final int GLFW_NO_ERROR = 0;
    public static final int GLFW_NOT_INITIALIZED = 0x00010001;
    public static final int GLFW_NO_CURRENT_CONTEXT = 0x00010002;
    public static final int GLFW_INVALID_ENUM = 0x00010003;
    public static final int GLFW_INVALID_VALUE = 0x00010004;
    public static final int GLFW_OUT_OF_MEMORY = 0x00010005;
    public static final int GLFW_API_UNAVAILABLE = 0x00010006;
    public static final int GLFW_VERSION_UNAVAILABLE = 0x00010007;
    public static final int GLFW_PLATFORM_ERROR = 0x00010008;
    public static final int GLFW_FORMAT_UNAVAILABLE = 0x00010009;
    public static final int GLFW_NO_WINDOW_CONTEXT = 0x0001000A;
    public static final int GLFW_CURSOR_UNAVAILABLE = 0x0001000B;
    public static final int GLFW_FEATURE_UNAVAILABLE = 0x0001000C;
    public static final int GLFW_FEATURE_UNIMPLEMENTED = 0x0001000D;
    public static final int GLFW_PLATFORM_UNAVAILABLE = 0x0001000E;
    public static final int GLFW_FOCUSED = 0x00020001;
    public static final int GLFW_ICONIFIED = 0x00020002;
    public static final int GLFW_RESIZABLE = 0x00020003;
    public static final int GLFW_VISIBLE = 0x00020004;
    public static final int GLFW_DECORATED = 0x00020005;
    public static final int GLFW_AUTO_ICONIFY = 0x00020006;
    public static final int GLFW_FLOATING = 0x00020007;
    public static final int GLFW_MAXIMIZED = 0x00020008;
    public static final int GLFW_CENTER_CURSOR = 0x00020009;
    public static final int GLFW_TRANSPARENT_FRAMEBUFFER = 0x0002000A;
    public static final int GLFW_HOVERED = 0x0002000B;
    public static final int GLFW_FOCUS_ON_SHOW = 0x0002000C;
    public static final int GLFW_MOUSE_PASSTHROUGH = 0x0002000D;
    public static final int GLFW_POSITION_X = 0x0002000E;
    public static final int GLFW_POSITION_Y = 0x0002000F;
    public static final int GLFW_RED_BITS = 0x00021001;
    public static final int GLFW_GREEN_BITS = 0x00021002;
    public static final int GLFW_BLUE_BITS = 0x00021003;
    public static final int GLFW_ALPHA_BITS = 0x00021004;
    public static final int GLFW_DEPTH_BITS = 0x00021005;
    public static final int GLFW_STENCIL_BITS = 0x00021006;
    public static final int GLFW_ACCUM_RED_BITS = 0x00021007;
    public static final int GLFW_ACCUM_GREEN_BITS = 0x00021008;
    public static final int GLFW_ACCUM_BLUE_BITS = 0x00021009;
    public static final int GLFW_ACCUM_ALPHA_BITS = 0x0002100A;
    public static final int GLFW_AUX_BUFFERS = 0x0002100B;
    public static final int GLFW_STEREO = 0x0002100C;
    public static final int GLFW_SAMPLES = 0x0002100D;
    public static final int GLFW_SRGB_CAPABLE = 0x0002100E;
    public static final int GLFW_REFRESH_RATE = 0x0002100F;
    public static final int GLFW_DOUBLEBUFFER = 0x00021010;
    public static final int GLFW_CLIENT_API = 0x00022001;
    public static final int GLFW_CONTEXT_VERSION_MAJOR = 0x00022002;
    public static final int GLFW_CONTEXT_VERSION_MINOR = 0x00022003;
    public static final int GLFW_CONTEXT_REVISION = 0x00022004;
    public static final int GLFW_CONTEXT_ROBUSTNESS = 0x00022005;
    public static final int GLFW_OPENGL_FORWARD_COMPAT = 0x00022006;
    public static final int GLFW_CONTEXT_DEBUG = 0x00022007;
    public static final int GLFW_OPENGL_DEBUG_CONTEXT = GLFW_CONTEXT_DEBUG;
    public static final int GLFW_OPENGL_PROFILE = 0x00022008;
    public static final int GLFW_CONTEXT_RELEASE_BEHAVIOR = 0x00022009;
    public static final int GLFW_CONTEXT_NO_ERROR = 0x0002200A;
    public static final int GLFW_CONTEXT_CREATION_API = 0x0002200B;
    public static final int GLFW_SCALE_TO_MONITOR = 0x0002200C;
    public static final int GLFW_SCALE_FRAMEBUFFER = 0x0002200D;
    public static final int GLFW_COCOA_RETINA_FRAMEBUFFER = 0x00023001;
    public static final int GLFW_COCOA_FRAME_NAME = 0x00023002;
    public static final int GLFW_COCOA_GRAPHICS_SWITCHING = 0x00023003;
    public static final int GLFW_X11_CLASS_NAME = 0x00024001;
    public static final int GLFW_X11_INSTANCE_NAME = 0x00024002;
    public static final int GLFW_WIN32_KEYBOARD_MENU = 0x00025001;
    public static final int GLFW_WIN32_SHOWDEFAULT = 0x00025002;
    public static final int GLFW_WAYLAND_APP_ID = 0x00026001;
    public static final int GLFW_NO_API = 0;
    public static final int GLFW_OPENGL_API = 0x00030001;
    public static final int GLFW_OPENGL_ES_API = 0x00030002;
    public static final int GLFW_NO_ROBUSTNESS = 0;
    public static final int GLFW_NO_RESET_NOTIFICATION = 0x00031001;
    public static final int GLFW_LOSE_CONTEXT_ON_RESET = 0x00031002;
    public static final int GLFW_OPENGL_ANY_PROFILE = 0;
    public static final int GLFW_OPENGL_CORE_PROFILE = 0x00032001;
    public static final int GLFW_OPENGL_COMPAT_PROFILE = 0x00032002;
    public static final int GLFW_CURSOR = 0x00033001;
    public static final int GLFW_STICKY_KEYS = 0x00033002;
    public static final int GLFW_STICKY_MOUSE_BUTTONS = 0x00033003;
    public static final int GLFW_LOCK_KEY_MODS = 0x00033004;
    public static final int GLFW_RAW_MOUSE_MOTION = 0x00033005;
    public static final int GLFW_CURSOR_NORMAL = 0x00034001;
    public static final int GLFW_CURSOR_HIDDEN = 0x00034002;
    public static final int GLFW_CURSOR_DISABLED = 0x00034003;
    public static final int GLFW_CURSOR_CAPTURED = 0x00034004;
    public static final int GLFW_ANY_RELEASE_BEHAVIOR = 0;
    public static final int GLFW_RELEASE_BEHAVIOR_FLUSH = 0x00035001;
    public static final int GLFW_RELEASE_BEHAVIOR_NONE = 0x00035002;
    public static final int GLFW_NATIVE_CONTEXT_API = 0x00036001;
    public static final int GLFW_EGL_CONTEXT_API = 0x00036002;
    public static final int GLFW_OSMESA_CONTEXT_API = 0x00036003;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_NONE = 0x00037001;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_OPENGL = 0x00037002;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_OPENGLES = 0x00037003;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_D3D9 = 0x00037004;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_D3D11 = 0x00037005;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_VULKAN = 0x00037007;
    public static final int GLFW_ANGLE_PLATFORM_TYPE_METAL = 0x00037008;
    public static final int GLFW_WAYLAND_PREFER_LIBDECOR = 0x00038001;
    public static final int GLFW_WAYLAND_DISABLE_LIBDECOR = 0x00038002;
    public static final int GLFW_ANY_POSITION = 0x80000000;
    public static final int GLFW_ARROW_CURSOR = 0x00036001;
    public static final int GLFW_IBEAM_CURSOR = 0x00036002;
    public static final int GLFW_CROSSHAIR_CURSOR = 0x00036003;
    public static final int GLFW_POINTING_HAND_CURSOR = 0x00036004;
    public static final int GLFW_RESIZE_EW_CURSOR = 0x00036005;
    public static final int GLFW_RESIZE_NS_CURSOR = 0x00036006;
    public static final int GLFW_RESIZE_NWSE_CURSOR = 0x00036007;
    public static final int GLFW_RESIZE_NESW_CURSOR = 0x00036008;
    public static final int GLFW_RESIZE_ALL_CURSOR = 0x00036009;
    public static final int GLFW_NOT_ALLOWED_CURSOR = 0x0003600A;
    public static final int GLFW_HRESIZE_CURSOR = GLFW_RESIZE_EW_CURSOR;
    public static final int GLFW_VRESIZE_CURSOR = GLFW_RESIZE_NS_CURSOR;
    public static final int GLFW_HAND_CURSOR = GLFW_POINTING_HAND_CURSOR;
    public static final int GLFW_CONNECTED = 0x00040001;
    public static final int GLFW_DISCONNECTED = 0x00040002;
    public static final int GLFW_JOYSTICK_HAT_BUTTONS = 0x00050001;
    public static final int GLFW_ANGLE_PLATFORM_TYPE = 0x00050002;
    public static final int GLFW_PLATFORM = 0x00050003;
    public static final int GLFW_COCOA_CHDIR_RESOURCES = 0x00051001;
    public static final int GLFW_COCOA_MENUBAR = 0x00051002;
    public static final int GLFW_X11_XCB_VULKAN_SURFACE = 0x00052001;
    public static final int GLFW_WAYLAND_LIBDECOR = 0x00053001;
    public static final int GLFW_ANY_PLATFORM = 0x00060000;
    public static final int GLFW_PLATFORM_WIN32 = 0x00060001;
    public static final int GLFW_PLATFORM_COCOA = 0x00060002;
    public static final int GLFW_PLATFORM_WAYLAND = 0x00060003;
    public static final int GLFW_PLATFORM_X11 = 0x00060004;
    public static final int GLFW_PLATFORM_NULL = 0x00060005;
    public static final int GLFW_DONT_CARE = -1;

    protected GLFW() {
        throw new UnsupportedOperationException();
    }

    // --------------------------------------------------------------- natives

    @Import(module = "lwjgl", name = "glfwInit") private static native int nglfwInit0();
    @Import(module = "lwjgl", name = "glfwTerminate") private static native void nglfwTerminate0();
    @Import(module = "lwjgl", name = "glfwInitHint") private static native void nglfwInitHint0(int hint, int value);
    @Import(module = "lwjgl", name = "glfwGetVersion") public static native void nglfwGetVersion(long major, long minor, long rev);
    @Import(module = "lwjgl", name = "glfwGetVersionString") public static native long nglfwGetVersionString();
    @Import(module = "lwjgl", name = "glfwGetPrimaryMonitor") private static native long nglfwGetPrimaryMonitor0();
    @Import(module = "lwjgl", name = "glfwGetVideoMode") public static native long nglfwGetVideoMode(long monitor);
    @Import(module = "lwjgl", name = "glfwGetMonitorName") public static native long nglfwGetMonitorName(long monitor);
    @Import(module = "lwjgl", name = "glfwDefaultWindowHints") private static native void nglfwDefaultWindowHints0();
    @Import(module = "lwjgl", name = "glfwWindowHint") private static native void nglfwWindowHint0(int hint, int value);
    @Import(module = "lwjgl", name = "glfwWindowHintString") public static native void nglfwWindowHintString(int hint, long value);
    @Import(module = "lwjgl", name = "glfwCreateWindow") public static native long nglfwCreateWindow(int width, int height, long title, long monitor, long share);
    @Import(module = "lwjgl", name = "glfwDestroyWindow") private static native void nglfwDestroyWindow0(long window);
    @Import(module = "lwjgl", name = "glfwWindowShouldClose") private static native int nglfwWindowShouldClose0(long window);
    @Import(module = "lwjgl", name = "glfwSetWindowShouldClose") private static native void nglfwSetWindowShouldClose0(long window, int value);
    @Import(module = "lwjgl", name = "glfwSetWindowTitle") public static native void nglfwSetWindowTitle(long window, long title);
    @Import(module = "lwjgl", name = "glfwGetWindowPos") public static native void nglfwGetWindowPos(long window, long xpos, long ypos);
    @Import(module = "lwjgl", name = "glfwSetWindowPos") private static native void nglfwSetWindowPos0(long window, int xpos, int ypos);
    @Import(module = "lwjgl", name = "glfwGetWindowSize") public static native void nglfwGetWindowSize(long window, long width, long height);
    @Import(module = "lwjgl", name = "glfwSetWindowSize") private static native void nglfwSetWindowSize0(long window, int width, int height);
    @Import(module = "lwjgl", name = "glfwGetFramebufferSize") public static native void nglfwGetFramebufferSize(long window, long width, long height);
    @Import(module = "lwjgl", name = "glfwGetWindowContentScale") public static native void nglfwGetWindowContentScale(long window, long xscale, long yscale);
    @Import(module = "lwjgl", name = "glfwShowWindow") private static native void nglfwShowWindow0(long window);
    @Import(module = "lwjgl", name = "glfwHideWindow") private static native void nglfwHideWindow0(long window);
    @Import(module = "lwjgl", name = "glfwFocusWindow") private static native void nglfwFocusWindow0(long window);
    @Import(module = "lwjgl", name = "glfwIconifyWindow") private static native void nglfwIconifyWindow0(long window);
    @Import(module = "lwjgl", name = "glfwRestoreWindow") private static native void nglfwRestoreWindow0(long window);
    @Import(module = "lwjgl", name = "glfwMaximizeWindow") private static native void nglfwMaximizeWindow0(long window);
    @Import(module = "lwjgl", name = "glfwGetWindowAttrib") private static native int nglfwGetWindowAttrib0(long window, int attrib);
    @Import(module = "lwjgl", name = "glfwSetWindowAttrib") private static native void nglfwSetWindowAttrib0(long window, int attrib, int value);
    @Import(module = "lwjgl", name = "glfwPollEvents") private static native void nglfwPollEvents0();
    @Import(module = "lwjgl", name = "glfwWaitEvents") private static native void nglfwWaitEvents0();
    @Import(module = "lwjgl", name = "glfwWaitEventsTimeout") private static native void nglfwWaitEventsTimeout0(double timeout);
    @Import(module = "lwjgl", name = "glfwPostEmptyEvent") private static native void nglfwPostEmptyEvent0();
    @Import(module = "lwjgl", name = "glfwGetInputMode") private static native int nglfwGetInputMode0(long window, int mode);
    @Import(module = "lwjgl", name = "glfwSetInputMode") private static native void nglfwSetInputMode0(long window, int mode, int value);
    @Import(module = "lwjgl", name = "glfwGetKeyName") public static native long nglfwGetKeyName(int key, int scancode);
    @Import(module = "lwjgl", name = "glfwGetKey") private static native int nglfwGetKey0(long window, int key);
    @Import(module = "lwjgl", name = "glfwGetMouseButton") private static native int nglfwGetMouseButton0(long window, int button);
    @Import(module = "lwjgl", name = "glfwGetCursorPos") public static native void nglfwGetCursorPos(long window, long xpos, long ypos);
    @Import(module = "lwjgl", name = "glfwSetCursorPos") private static native void nglfwSetCursorPos0(long window, double xpos, double ypos);
    @Import(module = "lwjgl", name = "glfwGetTime") private static native double nglfwGetTime0();
    @Import(module = "lwjgl", name = "glfwSetTime") private static native void nglfwSetTime0(double time);
    @Import(module = "lwjgl", name = "glfwMakeContextCurrent") private static native void nglfwMakeContextCurrent0(long window);
    @Import(module = "lwjgl", name = "glfwGetCurrentContext") private static native long nglfwGetCurrentContext0();
    @Import(module = "lwjgl", name = "glfwSwapBuffers") private static native void nglfwSwapBuffers0(long window);
    @Import(module = "lwjgl", name = "glfwSwapInterval") private static native void nglfwSwapInterval0(int interval);
    @Import(module = "lwjgl", name = "glfwExtensionSupported") public static native int nglfwExtensionSupported(long extension);
    @Import(module = "lwjgl", name = "glfwGetProcAddress") public static native long nglfwGetProcAddress(long procname);
    @Import(module = "lwjgl", name = "glfwSetClipboardString") public static native void nglfwSetClipboardString(long window, long string);
    @Import(module = "lwjgl", name = "glfwGetClipboardString") public static native long nglfwGetClipboardString(long window);

    // what a callback the call made threw, thrown now that the call is back in Java
    private static void check() {
        WarmcUpcalls.rethrow();
    }

    // ----------------------------------------------------------- functions

    public static boolean glfwInit() {
        boolean ok = nglfwInit0() != 0;
        check();
        return ok;
    }

    public static void glfwTerminate() {
        nglfwTerminate0();
        check();
        WarmcUpcalls.clear();
    }

    public static void glfwInitHint(int hint, int value) {
        nglfwInitHint0(hint, value);
        check();
    }

    public static void glfwGetVersion(IntBuffer major, IntBuffer minor, IntBuffer rev) {
        nglfwGetVersion(memAddressSafe(major), memAddressSafe(minor), memAddressSafe(rev));
    }

    public static void glfwGetVersion(int[] major, int[] minor, int[] rev) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            IntBuffer v = stack.callocInt(3);
            nglfwGetVersion(memAddress(v), memAddress(v) + 4, memAddress(v) + 8);
            if (major != null) major[0] = v.get(0);
            if (minor != null) minor[0] = v.get(1);
            if (rev != null) rev[0] = v.get(2);
        } finally {
            stack.setPointer(sp);
        }
    }

    public static String glfwGetVersionString() {
        return memUTF8Safe(nglfwGetVersionString());
    }

    public static GLFWErrorCallback glfwSetErrorCallback(GLFWErrorCallbackI cbfun) {
        return GLFWErrorCallback.wrap((GLFWErrorCallbackI) WarmcUpcalls.set(NULL, WarmcUpcalls.ERROR, cbfun));
    }

    public static long glfwGetPrimaryMonitor() {
        long monitor = nglfwGetPrimaryMonitor0();
        check();
        return monitor;
    }

    public static GLFWVidMode glfwGetVideoMode(long monitor) {
        long mode = nglfwGetVideoMode(monitor);
        check();
        return GLFWVidMode.createSafe(mode);
    }

    public static String glfwGetMonitorName(long monitor) {
        long name = nglfwGetMonitorName(monitor);
        check();
        return memUTF8Safe(name);
    }

    public static void glfwDefaultWindowHints() {
        nglfwDefaultWindowHints0();
        check();
    }

    public static void glfwWindowHint(int hint, int value) {
        nglfwWindowHint0(hint, value);
        check();
    }

    public static void glfwWindowHintString(int hint, CharSequence value) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(value, true);
            nglfwWindowHintString(hint, stack.getPointerAddress());
            check();
        } finally {
            stack.setPointer(sp);
        }
    }

    public static long glfwCreateWindow(int width, int height, ByteBuffer title, long monitor, long share) {
        long window = nglfwCreateWindow(width, height, memAddress(title), monitor, share);
        check();
        return window;
    }

    public static long glfwCreateWindow(int width, int height, CharSequence title, long monitor, long share) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(title, true);
            long window = nglfwCreateWindow(width, height, stack.getPointerAddress(), monitor, share);
            check();
            return window;
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glfwDestroyWindow(long window) {
        nglfwDestroyWindow0(window);
        check();
        WarmcUpcalls.forget(window);
    }

    public static boolean glfwWindowShouldClose(long window) {
        boolean close = nglfwWindowShouldClose0(window) != 0;
        check();
        return close;
    }

    public static void glfwSetWindowShouldClose(long window, boolean value) {
        nglfwSetWindowShouldClose0(window, value ? 1 : 0);
        check();
    }

    public static void glfwSetWindowTitle(long window, CharSequence title) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(title, true);
            nglfwSetWindowTitle(window, stack.getPointerAddress());
            check();
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glfwGetWindowPos(long window, IntBuffer xpos, IntBuffer ypos) {
        nglfwGetWindowPos(window, memAddressSafe(xpos), memAddressSafe(ypos));
        check();
    }

    public static void glfwGetWindowPos(long window, int[] xpos, int[] ypos) {
        intPair(window, xpos, ypos, 0);
    }

    public static void glfwSetWindowPos(long window, int xpos, int ypos) {
        nglfwSetWindowPos0(window, xpos, ypos);
        check();
    }

    public static void glfwGetWindowSize(long window, IntBuffer width, IntBuffer height) {
        nglfwGetWindowSize(window, memAddressSafe(width), memAddressSafe(height));
        check();
    }

    public static void glfwGetWindowSize(long window, int[] width, int[] height) {
        intPair(window, width, height, 1);
    }

    public static void glfwSetWindowSize(long window, int width, int height) {
        nglfwSetWindowSize0(window, width, height);
        check();
    }

    public static void glfwGetFramebufferSize(long window, IntBuffer width, IntBuffer height) {
        nglfwGetFramebufferSize(window, memAddressSafe(width), memAddressSafe(height));
        check();
    }

    public static void glfwGetFramebufferSize(long window, int[] width, int[] height) {
        intPair(window, width, height, 2);
    }

    // the int[] forms through two ints of the stack
    private static void intPair(long window, int[] a, int[] b, int which) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            IntBuffer v = stack.callocInt(2);
            long p = memAddress(v);
            if (which == 0) {
                nglfwGetWindowPos(window, p, p + 4);
            } else if (which == 1) {
                nglfwGetWindowSize(window, p, p + 4);
            } else {
                nglfwGetFramebufferSize(window, p, p + 4);
            }
            check();
            if (a != null) a[0] = v.get(0);
            if (b != null) b[0] = v.get(1);
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glfwGetWindowContentScale(long window, FloatBuffer xscale, FloatBuffer yscale) {
        nglfwGetWindowContentScale(window, memAddressSafe(xscale), memAddressSafe(yscale));
        check();
    }

    public static void glfwShowWindow(long window) {
        nglfwShowWindow0(window);
        check();
    }

    public static void glfwHideWindow(long window) {
        nglfwHideWindow0(window);
        check();
    }

    public static void glfwFocusWindow(long window) {
        nglfwFocusWindow0(window);
        check();
    }

    public static void glfwIconifyWindow(long window) {
        nglfwIconifyWindow0(window);
        check();
    }

    public static void glfwRestoreWindow(long window) {
        nglfwRestoreWindow0(window);
        check();
    }

    public static void glfwMaximizeWindow(long window) {
        nglfwMaximizeWindow0(window);
        check();
    }

    public static int glfwGetWindowAttrib(long window, int attrib) {
        int value = nglfwGetWindowAttrib0(window, attrib);
        check();
        return value;
    }

    public static void glfwSetWindowAttrib(long window, int attrib, int value) {
        nglfwSetWindowAttrib0(window, attrib, value);
        check();
    }

    public static void glfwPollEvents() {
        nglfwPollEvents0();
        check();
    }

    public static void glfwWaitEvents() {
        nglfwWaitEvents0();
        check();
    }

    public static void glfwWaitEventsTimeout(double timeout) {
        nglfwWaitEventsTimeout0(timeout);
        check();
    }

    public static void glfwPostEmptyEvent() {
        nglfwPostEmptyEvent0();
        check();
    }

    public static int glfwGetInputMode(long window, int mode) {
        int value = nglfwGetInputMode0(window, mode);
        check();
        return value;
    }

    public static void glfwSetInputMode(long window, int mode, int value) {
        nglfwSetInputMode0(window, mode, value);
        check();
    }

    public static String glfwGetKeyName(int key, int scancode) {
        long name = nglfwGetKeyName(key, scancode);
        check();
        return memUTF8Safe(name);
    }

    public static int glfwGetKey(long window, int key) {
        int state = nglfwGetKey0(window, key);
        check();
        return state;
    }

    public static int glfwGetMouseButton(long window, int button) {
        int state = nglfwGetMouseButton0(window, button);
        check();
        return state;
    }

    public static void glfwGetCursorPos(long window, DoubleBuffer xpos, DoubleBuffer ypos) {
        nglfwGetCursorPos(window, memAddressSafe(xpos), memAddressSafe(ypos));
        check();
    }

    public static void glfwGetCursorPos(long window, double[] xpos, double[] ypos) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            DoubleBuffer v = stack.callocDouble(2);
            nglfwGetCursorPos(window, memAddress(v), memAddress(v) + 8);
            check();
            if (xpos != null) xpos[0] = v.get(0);
            if (ypos != null) ypos[0] = v.get(1);
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glfwSetCursorPos(long window, double xpos, double ypos) {
        nglfwSetCursorPos0(window, xpos, ypos);
        check();
    }

    public static GLFWKeyCallback glfwSetKeyCallback(long window, GLFWKeyCallbackI cbfun) {
        return GLFWKeyCallback.wrap((GLFWKeyCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.KEY, cbfun));
    }

    public static GLFWCharCallback glfwSetCharCallback(long window, GLFWCharCallbackI cbfun) {
        return GLFWCharCallback.wrap((GLFWCharCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.CHAR, cbfun));
    }

    public static GLFWCursorPosCallback glfwSetCursorPosCallback(long window, GLFWCursorPosCallbackI cbfun) {
        return GLFWCursorPosCallback.wrap(
                (GLFWCursorPosCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.CURSOR_POS, cbfun));
    }

    public static GLFWMouseButtonCallback glfwSetMouseButtonCallback(long window, GLFWMouseButtonCallbackI cbfun) {
        return GLFWMouseButtonCallback.wrap(
                (GLFWMouseButtonCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.MOUSE_BUTTON, cbfun));
    }

    public static GLFWScrollCallback glfwSetScrollCallback(long window, GLFWScrollCallbackI cbfun) {
        return GLFWScrollCallback.wrap((GLFWScrollCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.SCROLL, cbfun));
    }

    public static GLFWFramebufferSizeCallback glfwSetFramebufferSizeCallback(long window,
            GLFWFramebufferSizeCallbackI cbfun) {
        return GLFWFramebufferSizeCallback.wrap(
                (GLFWFramebufferSizeCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.FRAMEBUFFER_SIZE, cbfun));
    }

    public static GLFWWindowSizeCallback glfwSetWindowSizeCallback(long window, GLFWWindowSizeCallbackI cbfun) {
        return GLFWWindowSizeCallback.wrap(
                (GLFWWindowSizeCallbackI) WarmcUpcalls.set(window, WarmcUpcalls.WINDOW_SIZE, cbfun));
    }

    public static double glfwGetTime() {
        return nglfwGetTime0();
    }

    public static void glfwSetTime(double time) {
        nglfwSetTime0(time);
        check();
    }

    public static void glfwMakeContextCurrent(long window) {
        nglfwMakeContextCurrent0(window);
        check();
    }

    public static long glfwGetCurrentContext() {
        return nglfwGetCurrentContext0();
    }

    public static void glfwSwapBuffers(long window) {
        nglfwSwapBuffers0(window);
        check();
    }

    public static void glfwSwapInterval(int interval) {
        nglfwSwapInterval0(interval);
        check();
    }

    public static boolean glfwExtensionSupported(CharSequence extension) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(extension, true);
            boolean supported = nglfwExtensionSupported(stack.getPointerAddress()) != 0;
            check();
            return supported;
        } finally {
            stack.setPointer(sp);
        }
    }

    public static long glfwGetProcAddress(CharSequence procname) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(procname, true);
            long address = nglfwGetProcAddress(stack.getPointerAddress());
            check();
            return address;
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glfwSetClipboardString(long window, CharSequence string) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            stack.nUTF8(string, true);
            nglfwSetClipboardString(window, stack.getPointerAddress());
            check();
        } finally {
            stack.setPointer(sp);
        }
    }

    public static String glfwGetClipboardString(long window) {
        long string = nglfwGetClipboardString(window);
        check();
        return memUTF8Safe(string);
    }
}
