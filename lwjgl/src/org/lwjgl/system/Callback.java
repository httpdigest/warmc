package org.lwjgl.system;

import java.util.HashMap;
import java.util.Map;

/** A callback object: LWJGL's, for warmc. */
public abstract class Callback implements Pointer, NativeResource {
    // made when first needed, as a static initializer would cost a check of
    // the class at each call of its methods
    private static Map<Object, Long> ids;
    private static long lastId;

    protected Callback() {
    }

    @Override
    public long address() {
        return idOf(this);
    }

    private static Map<Object, Long> ids() {
        if (ids == null) {
            ids = new HashMap<>();
        }
        return ids;
    }

    @Override
    public void free() {
        ids().remove(this);
    }

    // an address for a callback, unique while it lives: warmc gives C no
    // closure of its own to point at
    static long idOf(Object callback) {
        Long id = ids().get(callback);
        if (id == null) {
            id = ++lastId << 4;
            ids().put(callback, id);
        }
        return id;
    }

    public static void free(long functionPointer) {
        ids().values().remove(functionPointer);
    }
}
