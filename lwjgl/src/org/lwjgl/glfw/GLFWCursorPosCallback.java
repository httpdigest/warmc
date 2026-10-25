package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's CursorPos callback as an object: LWJGL's, for warmc. */
public abstract class GLFWCursorPosCallback extends Callback implements GLFWCursorPosCallbackI {
    protected GLFWCursorPosCallback() {
    }

    public static GLFWCursorPosCallback create(GLFWCursorPosCallbackI instance) {
        return instance instanceof GLFWCursorPosCallback
                ? (GLFWCursorPosCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWCursorPosCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWCursorPosCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWCursorPosCallback at that address");
        }
        return create((GLFWCursorPosCallbackI) callback);
    }

    public static GLFWCursorPosCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWCursorPosCallback wrap(GLFWCursorPosCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWCursorPosCallback set(long window) {
        GLFW.glfwSetCursorPosCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWCursorPosCallback {
        private final GLFWCursorPosCallbackI delegate;

        Container(GLFWCursorPosCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, double xpos, double ypos) {
            delegate.invoke(window, xpos, ypos);
        }
    }
}
