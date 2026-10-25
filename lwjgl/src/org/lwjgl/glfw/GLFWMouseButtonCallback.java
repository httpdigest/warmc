package org.lwjgl.glfw;

import org.lwjgl.system.Callback;

/** GLFW's MouseButton callback as an object: LWJGL's, for warmc. */
public abstract class GLFWMouseButtonCallback extends Callback implements GLFWMouseButtonCallbackI {
    protected GLFWMouseButtonCallback() {
    }

    public static GLFWMouseButtonCallback create(GLFWMouseButtonCallbackI instance) {
        return instance instanceof GLFWMouseButtonCallback
                ? (GLFWMouseButtonCallback) instance
                : new Container(instance);
    }

    // the one an address names, which warmc's are ids of
    public static GLFWMouseButtonCallback create(long functionPointer) {
        Object callback = WarmcUpcalls.byAddress(functionPointer);
        if (!(callback instanceof GLFWMouseButtonCallbackI)) {
            throw new IllegalArgumentException("warmc: no GLFWMouseButtonCallback at that address");
        }
        return create((GLFWMouseButtonCallbackI) callback);
    }

    public static GLFWMouseButtonCallback createSafe(long functionPointer) {
        return functionPointer == 0 ? null : create(functionPointer);
    }

    static GLFWMouseButtonCallback wrap(GLFWMouseButtonCallbackI instance) {
        return instance == null ? null : create(instance);
    }

    public GLFWMouseButtonCallback set(long window) {
        GLFW.glfwSetMouseButtonCallback(window, this);
        return this;
    }

    private static final class Container extends GLFWMouseButtonCallback {
        private final GLFWMouseButtonCallbackI delegate;

        Container(GLFWMouseButtonCallbackI delegate) {
            this.delegate = delegate;
        }

        @Override
        public void invoke(long window, int button, int action, int mods) {
            delegate.invoke(window, button, action, mods);
        }
    }
}
