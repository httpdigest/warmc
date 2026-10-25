package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's Key callback as an object: LWJGL's, for warmc. */
public abstract class GLFWKeyCallback extends Callback implements GLFWKeyCallbackI {
    protected GLFWKeyCallback() {
    }

    public static GLFWKeyCallback create(GLFWKeyCallbackI instance) {
        return instance instanceof GLFWKeyCallback
                ? (GLFWKeyCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWKeyCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWKeyCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWKeyCallback at that address");
        }
        return create((GLFWKeyCallbackI) callback);
    }

    public static GLFWKeyCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWKeyCallback wrap(GLFWKeyCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWKeyCallback set(long window) {
        GLFW.glfwSetKeyCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWKeyCallback {
        private final GLFWKeyCallbackI delegate;

        Container(GLFWKeyCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, int key, int scancode, int action, int mods) {
            delegate.invoke(window, key, scancode, action, mods);
        }
    }
}
