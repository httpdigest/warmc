// Calls through org.lwjgl.system.JNI, which lwjgl/build.py generates, of C of
// the program's own (native.c) at the addresses dlsym finds, as LWJGL calls a
// library's functions: each result as C's convention has it, arrays copied
// for C and back, arguments past the registers, floating point, and the
// program called back through LWJGL's callbacks, an exception one throws
// coming out of the call that ran it.  And LWJGL's libffi, which is the
// system's: its ABIs, a call through a CIF, and a closure's fields.
import static org.lwjgl.system.MemoryUtil.NULL;
import static org.lwjgl.system.MemoryUtil.memAddress;

import java.nio.ByteBuffer;
import java.nio.DoubleBuffer;
import java.nio.IntBuffer;
import java.util.Arrays;
import org.lwjgl.PointerBuffer;
import org.lwjgl.glfw.GLFWAllocateCallback;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWScrollCallback;
import org.lwjgl.system.JNI;
import org.lwjgl.system.MemoryStack;
import org.lwjgl.system.libffi.FFICIF;
import org.lwjgl.system.libffi.FFIClosure;
import org.lwjgl.system.libffi.LibFFI;
import org.lwjgl.system.macosx.DynamicLinkLoader;

public class Main {
    static String last;

    static long f(String name) {
        long a = DynamicLinkLoader.dlsym(DynamicLinkLoader.RTLD_DEFAULT, name);
        if (a == NULL) {
            throw new IllegalStateException("no " + name);
        }
        return a;
    }

    public static void main(String[] args) {
        // a narrow result is its low bits
        long z = f("jni_bool_dirty"), b = f("jni_byte_dirty"), s = f("jni_short_dirty");
        System.out.println("bool " + JNI.invokePZ(0, z) + " " + JNI.invokePZ(1, z) + " " + JNI.invokePZ(0x100, z));
        System.out.println("byte " + JNI.invokePB(0x85, b) + " " + JNI.invokePB(0x7f, b));
        System.out.println("short " + JNI.invokePC(0x8001, s) + " " + JNI.invokePC(0x1234, s));

        short[] sa = {1, -2, 300};
        int[] ia = {7, -8, 100000};
        long[] la = {1L << 40, -1};
        float[] fa = {1.5f, -0.5f};
        double[] da = {0.25, 10};
        JNI.callPV(sa.length, sa, f("jni_scale_s"));
        JNI.callPV(ia.length, ia, f("jni_scale_i"));
        JNI.callPV(la.length, la, f("jni_scale_l"));
        JNI.callPV(fa.length, fa, f("jni_scale_f"));
        JNI.callPV(da.length, da, f("jni_scale_d"));
        System.out.println("arrays " + Arrays.toString(sa) + " " + Arrays.toString(ia) + " " + Arrays.toString(la)
                + " " + Arrays.toString(fa) + " " + Arrays.toString(da));
        System.out.println("after arrays, NULL: " + JNI.invokeI(f("jni_last_null")));
        JNI.callPV(0, (int[]) null, f("jni_scale_i"));
        System.out.println("a null array is NULL: " + JNI.invokeI(f("jni_last_null")));

        // the stack an array is copied to given back, call after call
        int[] counter = {0};
        long inc = f("jni_inc");
        for (int k = 0; k < 20000; k++) {
            JNI.callPV(1, counter, inc);
        }
        System.out.println("20000 calls with an array: " + counter[0]);

        int[] p1 = {2}, p21 = {4};
        long[] p2 = {3}, p22 = {5};
        int many23 = JNI.callPPPPI(1, p1, p2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, p21, p22,
                f("jni_many"));
        System.out.println("23 arguments " + many23 + ", written " + p1[0] + " " + p2[0] + " " + p21[0] + " " + p22[0]);

        JNI.callV(1.5f, 0.25f, -2f, 0.125f, f("jni_floats"));
        System.out.println("floats " + JNI.invokeD(f("jni_last")));

        GLFWErrorCallback error = GLFWErrorCallback.create(
                (code, description) -> last = code + " " + GLFWErrorCallback.getDescription(description));
        JNI.invokePV(error.address(), 42, f("jni_error_back"));
        System.out.println("called back: " + last);
        GLFWErrorCallback thrower = GLFWErrorCallback.create((code, description) -> {
            throw new IllegalStateException("thrown " + code + " " + GLFWErrorCallback.getDescription(description));
        });
        try {
            JNI.invokePV(thrower.address(), 7, f("jni_error_back"));
            System.out.println("no exception");
        } catch (IllegalStateException e) {
            System.out.println("after the invoke: " + e.getMessage());
        }
        try {
            JNI.callPPV(thrower.address(), 8, f("jni_error_back_call"));
            System.out.println("no exception");
        } catch (IllegalStateException e) {
            System.out.println("after the call: " + e.getMessage());
        }
        try {
            JNI.invokePV(thrower.address(), 5, f("jni_error_twice"));
            System.out.println("no exception");
        } catch (IllegalStateException e) {
            System.out.println("of two, the first: " + e.getMessage());
        }
        // more callbacks at once than the first table of them holds
        GLFWErrorCallback[] many = new GLFWErrorCallback[40];
        int[] sum = {0};
        for (int k = 0; k < many.length; k++) {
            int weight = k + 1;
            many[k] = GLFWErrorCallback.create((code, description) -> sum[0] += weight * code);
        }
        for (GLFWErrorCallback c : many) {
            JNI.invokePV(c.address(), 2, f("jni_error_back"));
        }
        for (int k = 0; k < many.length; k += 2) {
            many[k].free();
        }
        for (int k = 0; k < many.length; k += 2) {
            many[k] = GLFWErrorCallback.create((code, description) -> sum[0] -= code);
        }
        for (GLFWErrorCallback c : many) {
            JNI.invokePV(c.address(), 1, f("jni_error_back"));
            c.free();
        }
        System.out.println("40 callbacks: " + sum[0]);
        GLFWScrollCallback scroll = GLFWScrollCallback.create((window, x, y) -> last = window + " " + x + " " + y);
        JNI.invokePPV(77, scroll.address(), f("jni_scroll_back"));
        System.out.println("doubles: " + last);
        GLFWAllocateCallback allocate = GLFWAllocateCallback.create((size, user) -> size * 2 + user);
        System.out.println("a result: " + JNI.invokePPPP(allocate.address(), 20, 3, f("jni_alloc_back")));

        System.out.println("ABIs " + LibFFI.FFI_FIRST_ABI + " " + LibFFI.FFI_SYSV + " " + LibFFI.FFI_WIN64 + " "
                + LibFFI.FFI_LAST_ABI + " " + LibFFI.FFI_DEFAULT_ABI + " " + LibFFI.FFI_UNIX64 + " "
                + LibFFI.FFI_TYPE_LONGDOUBLE);
        try (MemoryStack stack = MemoryStack.stackPush()) {
            FFICIF cif = FFICIF.malloc(stack);
            int prepped = LibFFI.ffi_prep_cif(cif, LibFFI.FFI_DEFAULT_ABI, LibFFI.ffi_type_double,
                    stack.pointers(LibFFI.ffi_type_double, LibFFI.ffi_type_sint32));
            System.out.println("a CIF: " + prepped + " " + cif.abi() + " " + cif.nargs() + " "
                    + (cif.rtype().address() == LibFFI.ffi_type_double.address()) + " "
                    + (cif.arg_types().get(1) == LibFFI.ffi_type_sint32.address()) + " " + cif.rtype().size());
            ByteBuffer ret = stack.malloc(8);
            DoubleBuffer x = stack.doubles(2.5);
            IntBuffer k = stack.ints(3);
            LibFFI.ffi_call(cif, f("jni_scaled"), ret, stack.pointers(memAddress(x), memAddress(k)));
            System.out.println("called through it: " + ret.getDouble(0));
            PointerBuffer code = stack.mallocPointer(1);
            FFIClosure closure = LibFFI.ffi_closure_alloc(FFIClosure.SIZEOF, code);
            int closed = LibFFI.ffi_prep_closure_loc(closure, cif, f("jni_last"), 99, code.get(0));
            System.out.println("a closure: " + closed + " " + (closure.cif().address() == cif.address()) + " "
                    + (closure.fun() == f("jni_last")) + " " + closure.user_data());
            LibFFI.ffi_closure_free(closure);
        }
        error.free();
        thrower.free();
        scroll.free();
        allocate.free();
    }
}
