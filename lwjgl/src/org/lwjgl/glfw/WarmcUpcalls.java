package org.lwjgl.glfw;

import java.util.HashMap;
import java.util.Map;
import org.teavm.interop.Export;
import org.teavm.interop.Import;

/**
 * GLFW's callbacks, for warmc.  lwjgl/native/lwjgl.c registers one C function
 * per kind of callback with GLFW, which calls the export of that kind here, and
 * this calls the Java callback set for it.  A callback's exception is thrown
 * when the GLFW function whose call ran it returns, as LWJGL's JNI upcalls do.
 */
final class WarmcUpcalls {
    static final int ERROR = 0;
    static final int KEY = 1;
    static final int CHAR = 2;
    static final int CURSOR_POS = 3;
    static final int MOUSE_BUTTON = 4;
    static final int SCROLL = 5;
    static final int FRAMEBUFFER_SIZE = 6;
    static final int WINDOW_SIZE = 7;
    private static final int KINDS = 8;

    private static Object onError;
    // made when first needed: a static initializer would have TeaVM check that
    // the class is initialized at each GLFW call, which all check pending
    private static Map<Long, Object[]> windows;
    private static Throwable pending;

    @Import(module = "lwjgl", name = "warmc_lwjgl_set_callback")
    private static native void nsetCallback(long window, int kind, int on);

    private WarmcUpcalls() {
    }

    private static Map<Long, Object[]> windows() {
        if (windows == null) {
            windows = new HashMap<>();
        }
        return windows;
    }

    private static Object[] callbacks(long window) {
        Object[] set = windows().get(window);
        if (set == null) {
            set = new Object[KINDS];
            windows().put(window, set);
        }
        return set;
    }

    /** Sets a callback, or none for null, and returns the one it replaced. */
    static Object set(long window, int kind, Object callback) {
        Object previous;
        if (kind == ERROR) {
            previous = onError;
            onError = callback;
        } else {
            Object[] set = callbacks(window);
            previous = set[kind];
            set[kind] = callback;
        }
        nsetCallback(window, kind, callback != null ? 1 : 0);
        rethrow();
        return previous;
    }

    static Object get(long window, int kind) {
        if (kind == ERROR) {
            return onError;
        }
        Object[] set = windows().get(window);
        return set == null ? null : set[kind];
    }

    static Object byAddress(long address) {
        if (onError != null && ((org.lwjgl.system.CallbackI) onError).address() == address) {
            return onError;
        }
        for (Object[] set : windows().values()) {
            for (Object callback : set) {
                if (callback != null && ((org.lwjgl.system.CallbackI) callback).address() == address) {
                    return callback;
                }
            }
        }
        return null;
    }

    static void forget(long window) {
        windows().remove(window);
    }

    static void clear() {
        windows().clear();
    }

    private static void pend(Throwable t) {
        if (pending == null) {
            pending = t;
        }
    }

    static void rethrow() {
        Throwable t = pending;
        if (t != null) {
            pending = null;
            if (t instanceof RuntimeException) {
                throw (RuntimeException) t;
            }
            if (t instanceof Error) {
                throw (Error) t;
            }
            throw new RuntimeException(t);
        }
    }

    @Export(name = "lwjgl_glfw_error")
    public static void errorCallback(int error, long description) {
        Object callback = onError;
        if (callback != null) {
            try {
                ((GLFWErrorCallbackI) callback).invoke(error, description);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_key")
    public static void keyCallback(long window, int key, int scancode, int action, int mods) {
        Object callback = callbacks(window)[KEY];
        if (callback != null) {
            try {
                ((GLFWKeyCallbackI) callback).invoke(window, key, scancode, action, mods);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_char")
    public static void charCallback(long window, int codepoint) {
        Object callback = callbacks(window)[CHAR];
        if (callback != null) {
            try {
                ((GLFWCharCallbackI) callback).invoke(window, codepoint);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_cursor_pos")
    public static void cursorPosCallback(long window, double xpos, double ypos) {
        Object callback = callbacks(window)[CURSOR_POS];
        if (callback != null) {
            try {
                ((GLFWCursorPosCallbackI) callback).invoke(window, xpos, ypos);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_mouse_button")
    public static void mouseButtonCallback(long window, int button, int action, int mods) {
        Object callback = callbacks(window)[MOUSE_BUTTON];
        if (callback != null) {
            try {
                ((GLFWMouseButtonCallbackI) callback).invoke(window, button, action, mods);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_scroll")
    public static void scrollCallback(long window, double xoffset, double yoffset) {
        Object callback = callbacks(window)[SCROLL];
        if (callback != null) {
            try {
                ((GLFWScrollCallbackI) callback).invoke(window, xoffset, yoffset);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_framebuffer_size")
    public static void framebufferSizeCallback(long window, int width, int height) {
        Object callback = callbacks(window)[FRAMEBUFFER_SIZE];
        if (callback != null) {
            try {
                ((GLFWFramebufferSizeCallbackI) callback).invoke(window, width, height);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }

    @Export(name = "lwjgl_glfw_window_size")
    public static void windowSizeCallback(long window, int width, int height) {
        Object callback = callbacks(window)[WINDOW_SIZE];
        if (callback != null) {
            try {
                ((GLFWWindowSizeCallbackI) callback).invoke(window, width, height);
            } catch (Throwable t) {
                pend(t);
            }
        }
    }
}
