package org.lwjgl.system;

/**
 * LWJGL's stack walking, for warmc, where TeaVM's program has no stack trace
 * to walk: the debug allocator reports a leak without where it was
 * allocated, and the debug stack cannot tell a frame popped in another
 * method than pushed it.
 */
final class StackWalkUtil {
    // the method a frame was pushed in, which the debug stack keeps
    private static final StackTraceElement UNKNOWN = new StackTraceElement("?", "?", null, -1);

    private StackWalkUtil() {
    }

    static StackTraceElement[] stackWalkArray(Object[] a) {
        return (StackTraceElement[]) a;
    }

    static Object stackWalkGetMethod(Class<?> after) {
        return UNKNOWN;
    }

    static Object stackWalkCheckPop(Class<?> after, Object pushed) {
        return null;
    }

    static Object[] stackWalkGetTrace() {
        return new StackTraceElement[0];
    }
}
