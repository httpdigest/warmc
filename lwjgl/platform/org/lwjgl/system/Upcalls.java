package org.lwjgl.system;

import java.util.HashMap;
import org.teavm.interop.Export;
import org.teavm.interop.Import;

/**
 * LWJGL's upcalls -- a C function that calls a Java callback -- for warmc, in
 * place of LWJGL's own backend, whose closures call Java through JNI.
 *
 * <p>lwjgl/native/lwjgl.c makes the C function: a closure of the system's
 * libffi, for the callback's descriptor's CIF, whose handler calls this
 * module's export lwjgl_upcall with the callback's number and the addresses
 * libffi gives of its return value and its arguments.  The callback reads
 * the arguments, and writes the return value, through MemoryUtil, as on the
 * JVM: they are C's memory, which it copies through linear memory.
 *
 * <p>An exception the callback throws is kept and thrown when the C function
 * whose call ran it returns, as a JNI upcall's exception is:
 * org.lwjgl.system.JNI, which lwjgl/build.py generates, checks after every
 * invoke, the calls of GLFW and the like.
 */
final class Upcalls {
    // the callbacks by number, and their numbers by C function; made when first
    // needed, as a static initializer would have TeaVM check that the class is
    // initialized at every invoke, which reads pending
    private static Object[] callbacks;
    private static HashMap<Long, Integer> numbers;
    static Throwable pending;

    @Import(module = "lwjgl", name = "warmc_lwjgl_upcall_create")
    private static native long ncreate(long cif, int number);

    @Import(module = "lwjgl", name = "warmc_lwjgl_upcall_free")
    private static native void nfree(long function);

    private Upcalls() {
    }

    static long upcallCreate(Callback.Descriptor descriptor, Object instance) {
        if (callbacks == null) {
            callbacks = new Object[16];
            numbers = new HashMap<>();
        }
        int number = 0;
        while (number < callbacks.length && callbacks[number] != null) {
            number++;
        }
        if (number == callbacks.length) {
            Object[] more = new Object[callbacks.length * 2];
            System.arraycopy(callbacks, 0, more, 0, callbacks.length);
            callbacks = more;
        }
        long function = ncreate(descriptor.cif.address(), number);
        if (function == MemoryUtil.NULL) {
            throw new OutOfMemoryError("libffi made no closure");
        }
        callbacks[number] = instance;
        numbers.put(function, number);
        return function;
    }

    @SuppressWarnings("unchecked")
    static <T extends CallbackI> T upcallGet(long functionPointer) {
        return (T) callbacks[numbers.get(functionPointer)];
    }

    static void upcallFree(long functionPointer) {
        Integer number = numbers.remove(functionPointer);
        callbacks[number] = null;
        nfree(functionPointer);
    }

    @Export(name = "lwjgl_upcall")
    static void upcall(int number, long ret, long args) {
        try {
            ((CallbackI) callbacks[number]).callback(ret, args);
        } catch (Throwable t) {
            if (pending == null) {
                pending = t;
            }
        }
    }

    /** Throws the exception a callback kept, if there is one. */
    static void rethrow() {
        Throwable t = pending;
        pending = null;
        Upcalls.<RuntimeException>sneaky(t);
    }

    @SuppressWarnings("unchecked")
    private static <T extends Throwable> void sneaky(Throwable t) throws T {
        throw (T) t;
    }
}
