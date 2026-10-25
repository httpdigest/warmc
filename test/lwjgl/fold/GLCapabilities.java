// The shapes of LWJGL's GLCapabilities checks that lwjgl/teavm/
// LwjglTransformer.java folds, and those it must leave, in a class of that
// name, which takes the place of LWJGL's in this program: checks of
// functions macOS's OpenGL framework has none of (glDispatchCompute,
// glCreateBuffers, glTextureView, glNamedBufferData, glBufferStorage) and
// some of (glClear, glEnable).  Main's function provider finds every function
// but glEnable, so a check folded is one that comes out false.
package org.lwjgl.opengl;

import static org.lwjgl.system.Checks.checkFunctions;
import static org.lwjgl.system.Checks.reportMissing;

import java.util.Set;
import org.lwjgl.BufferUtils;
import org.lwjgl.PointerBuffer;
import org.lwjgl.system.FunctionProvider;

public final class GLCapabilities {
    // the calls of a helper that does more than ask the set
    static int asked;

    private GLCapabilities() {
    }

    private static boolean has(Set<String> ext, String name) {
        return ext.contains(name) || ext.contains("OpenGL99");
    }

    private static void count() {
        asked++;
    }

    // a call besides asking the set
    private static boolean counted(Set<String> ext, String name) {
        count();
        return ext.contains(name);
    }

    // none the framework has, an index counted: false, nothing looked up
    private static boolean absent(FunctionProvider p, PointerBuffer caps, Set<String> ext) {
        if (!ext.contains("A")) {
            return false;
        }
        return checkFunctions(p, caps, new int[] {0, 1}, "glDispatchCompute", "glCreateBuffers")
                || reportMissing("GL", "A");
    }

    // none the framework has, but each index a flag's, which may skip it
    private static boolean flagged(FunctionProvider p, PointerBuffer caps, Set<String> ext) {
        int flag = has(ext, "B") ? 0 : Integer.MIN_VALUE;
        return checkFunctions(p, caps, new int[] {flag + 2}, "glTextureView");
    }

    // one the framework has
    private static boolean some(FunctionProvider p, PointerBuffer caps, Set<String> ext) {
        return checkFunctions(p, caps, new int[] {3, 4}, "glClear", "glBufferStorage");
    }

    // a check folded beside one that is not, a flag between them
    private static boolean shared(FunctionProvider p, PointerBuffer caps, Set<String> ext) {
        int flag = has(ext, "C") ? 0 : Integer.MIN_VALUE;
        boolean folded = checkFunctions(p, caps, new int[] {5, flag + 6}, "glNamedBufferData", "glCreateBuffers");
        boolean kept = checkFunctions(p, caps, new int[] {flag + 7}, "glEnable");
        return folded || kept;
    }

    // a check folded whose flag's helper does more than ask
    private static boolean impure(FunctionProvider p, PointerBuffer caps, Set<String> ext) {
        int flag = counted(ext, "D") ? 0 : Integer.MIN_VALUE;
        return checkFunctions(p, caps, new int[] {8, flag + 9}, "glDispatchCompute", "glCreateBuffers");
    }

    // no lookup, and true
    private static boolean plain(Set<String> ext) {
        return ext.contains("E");
    }

    public static String run(FunctionProvider p, Set<String> ext) {
        PointerBuffer caps = BufferUtils.createPointerBuffer(16);
        return "absent " + absent(p, caps, ext) + ", flagged " + flagged(p, caps, ext) + ", some "
                + some(p, caps, ext) + ", shared " + shared(p, caps, ext) + ", impure " + impure(p, caps, ext)
                + " (asked " + asked + "), plain " + plain(ext);
    }
}
