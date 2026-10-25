package org.lwjgl.system;

import org.lwjgl.PointerBuffer;

/**
 * Where LWJGL keeps the current context's function addresses for its JNI
 * functions to call through: in the thread's JNIEnv.  warmc has none to keep
 * them in, nor needs to: an OpenGL function is a direct call of the C
 * function of its name (lwjgl/teavm/LwjglTransformer.java), which macOS's
 * OpenGL framework dispatches to the current context.  The methods are
 * LWJGL's, for org.lwjgl.opengl.GL to call.
 */
public final class ThreadLocalUtil {
    private ThreadLocalUtil() {
    }

    public static void setCapabilities(long capabilities) {
    }

    public static void setFunctionMissingAddresses(int functionCount) {
    }

    public static PointerBuffer setupAddressBuffer(PointerBuffer addresses) {
        return addresses;
    }

    public static boolean areCapabilitiesDifferent(PointerBuffer ref, PointerBuffer caps) {
        for (int i = 0; i < ref.remaining(); i++) {
            if (ref.get(i) != caps.get(i) && caps.get(i) != MemoryUtil.NULL) {
                return true;
            }
        }
        return false;
    }
}
