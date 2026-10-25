package org.lwjgl.system;

/**
 * A Java function C calls: LWJGL's, for warmc.  Where LWJGL makes a native
 * closure of each, warmc's bindings give C one function of their own per kind
 * of callback, which finds the Java one to call.
 */
public interface CallbackI extends Pointer {
    @Override
    default long address() {
        return Callback.idOf(this);
    }
}
