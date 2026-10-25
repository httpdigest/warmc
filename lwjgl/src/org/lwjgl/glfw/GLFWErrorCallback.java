package org.lwjgl.glfw;

import java.io.PrintStream;
import org.lwjgl.system.Callback;
import org.lwjgl.system.MemoryUtil;

/** GLFW's Error callback as an object: LWJGL's, for warmc. */
public abstract class GLFWErrorCallback extends Callback implements GLFWErrorCallbackI {
    protected GLFWErrorCallback() {
    }

    public static GLFWErrorCallback create(GLFWErrorCallbackI instance) {
        return instance instanceof GLFWErrorCallback
                ? (GLFWErrorCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWErrorCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWErrorCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWErrorCallback at that address");
        }
        return create((GLFWErrorCallbackI) callback);
    }

    public static GLFWErrorCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWErrorCallback wrap(GLFWErrorCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWErrorCallback set() {
        GLFW.glfwSetErrorCallback(this);
        return this;
    }

    /** The description GLFW's error callback receives, as a string. */
    public static String getDescription(long description) {
        return MemoryUtil.memUTF8(description);
    }

    public static GLFWErrorCallback createPrint() {
        return createPrint(System.err);
    }

    /** Prints each error to {@code stream}, as LWJGL's does. */
    public static GLFWErrorCallback createPrint(PrintStream stream) {
        return new GLFWErrorCallback() {
            @Override
            public void invoke(int error, long description) {
                String msg = getDescription(description);
                stream.println("[LWJGL] " + errorName(error) + " error");
                stream.println("\tDescription : " + msg);
                stream.println("\tStacktrace  :");
                StackTraceElement[] stack = Thread.currentThread().getStackTrace();
                for (int i = 4; i < stack.length; i++) {
                    stream.println("\t\t" + stack[i]);
                }
            }
        };
    }

    public static GLFWErrorCallback createThrow() {
        return new GLFWErrorCallback() {
            @Override
            public void invoke(int error, long description) {
                throw new IllegalStateException(String.format("GLFW error [0x%X]: ", error) + getDescription(description));
            }
        };
    }

    static String errorName(int error) {
        switch (error) {
            case GLFW.GLFW_NOT_INITIALIZED: return "GLFW_NOT_INITIALIZED";
            case GLFW.GLFW_NO_CURRENT_CONTEXT: return "GLFW_NO_CURRENT_CONTEXT";
            case GLFW.GLFW_INVALID_ENUM: return "GLFW_INVALID_ENUM";
            case GLFW.GLFW_INVALID_VALUE: return "GLFW_INVALID_VALUE";
            case GLFW.GLFW_OUT_OF_MEMORY: return "GLFW_OUT_OF_MEMORY";
            case GLFW.GLFW_API_UNAVAILABLE: return "GLFW_API_UNAVAILABLE";
            case GLFW.GLFW_VERSION_UNAVAILABLE: return "GLFW_VERSION_UNAVAILABLE";
            case GLFW.GLFW_PLATFORM_ERROR: return "GLFW_PLATFORM_ERROR";
            case GLFW.GLFW_FORMAT_UNAVAILABLE: return "GLFW_FORMAT_UNAVAILABLE";
            case GLFW.GLFW_NO_WINDOW_CONTEXT: return "GLFW_NO_WINDOW_CONTEXT";
            case GLFW.GLFW_CURSOR_UNAVAILABLE: return "GLFW_CURSOR_UNAVAILABLE";
            case GLFW.GLFW_FEATURE_UNAVAILABLE: return "GLFW_FEATURE_UNAVAILABLE";
            case GLFW.GLFW_FEATURE_UNIMPLEMENTED: return "GLFW_FEATURE_UNIMPLEMENTED";
            case GLFW.GLFW_PLATFORM_UNAVAILABLE: return "GLFW_PLATFORM_UNAVAILABLE";
            default: return "0x" + Integer.toHexString(error).toUpperCase();
        }
    }

    private static final class Container extends GLFWErrorCallback {
        private final GLFWErrorCallbackI delegate;

        Container(GLFWErrorCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(int error, long description) {
            delegate.invoke(error, description);
        }
    }
}
