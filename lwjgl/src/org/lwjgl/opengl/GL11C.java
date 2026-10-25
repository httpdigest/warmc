package org.lwjgl.opengl;

import static org.lwjgl.system.MemoryStack.stackGet;
import static org.lwjgl.system.MemoryUtil.memAddress;
import static org.lwjgl.system.MemoryUtil.memUTF8Safe;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import org.lwjgl.system.MemoryStack;
import org.teavm.interop.Import;

/**
 * OpenGL 1.1's core functions: LWJGL's GL11C, for warmc.  Each calls the
 * OpenGL function of its name, which macOS's OpenGL framework exports.
 */
public class GL11C {
    public static final int
        GL_FALSE = 0, GL_TRUE = 1,
        GL_NO_ERROR = 0, GL_INVALID_ENUM = 0x500, GL_INVALID_VALUE = 0x501, GL_INVALID_OPERATION = 0x502,
        GL_OUT_OF_MEMORY = 0x505,
        GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400, GL_COLOR_BUFFER_BIT = 0x4000,
        GL_POINTS = 0x0, GL_LINES = 0x1, GL_LINE_LOOP = 0x2, GL_LINE_STRIP = 0x3, GL_TRIANGLES = 0x4,
        GL_TRIANGLE_STRIP = 0x5, GL_TRIANGLE_FAN = 0x6,
        GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02, GL_EXTENSIONS = 0x1F03,
        GL_BYTE = 0x1400, GL_UNSIGNED_BYTE = 0x1401, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403,
        GL_INT = 0x1404, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406, GL_DOUBLE = 0x140A,
        GL_RED = 0x1903, GL_GREEN = 0x1904, GL_BLUE = 0x1905, GL_ALPHA = 0x1906, GL_RGB = 0x1907,
        GL_RGBA = 0x1908,
        GL_CULL_FACE = 0xB44, GL_DEPTH_TEST = 0xB71, GL_BLEND = 0xBE2, GL_SCISSOR_TEST = 0xC11,
        GL_VIEWPORT = 0xBA2, GL_SCISSOR_BOX = 0xC10, GL_COLOR_CLEAR_VALUE = 0xC22,
        GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR = 0x301, GL_SRC_ALPHA = 0x302,
        GL_ONE_MINUS_SRC_ALPHA = 0x303, GL_DST_ALPHA = 0x304, GL_ONE_MINUS_DST_ALPHA = 0x305,
        GL_PACK_ALIGNMENT = 0xD05, GL_UNPACK_ALIGNMENT = 0xCF5, GL_MAX_TEXTURE_SIZE = 0xD33,
        GL_FRONT = 0x404, GL_BACK = 0x405, GL_FRONT_AND_BACK = 0x408, GL_CW = 0x900, GL_CCW = 0x901,
        GL_NEVER = 0x200, GL_LESS = 0x201, GL_EQUAL = 0x202, GL_LEQUAL = 0x203, GL_GREATER = 0x204,
        GL_NOTEQUAL = 0x205, GL_GEQUAL = 0x206, GL_ALWAYS = 0x207;

    protected GL11C() {
        throw new UnsupportedOperationException();
    }

    @Import(module = "lwjgl", name = "glClearColor")
    public static native void glClearColor(float red, float green, float blue, float alpha);

    @Import(module = "lwjgl", name = "glClear")
    public static native void glClear(int mask);

    @Import(module = "lwjgl", name = "glViewport")
    public static native void glViewport(int x, int y, int w, int h);

    @Import(module = "lwjgl", name = "glScissor")
    public static native void glScissor(int x, int y, int width, int height);

    @Import(module = "lwjgl", name = "glEnable")
    public static native void glEnable(int target);

    @Import(module = "lwjgl", name = "glDisable")
    public static native void glDisable(int target);

    @Import(module = "lwjgl", name = "glIsEnabled")
    private static native int nglIsEnabled(int cap);

    public static boolean glIsEnabled(int cap) {
        // a GLboolean, whose upper bits C leaves as they are
        return (nglIsEnabled(cap) & 0xFF) != 0;
    }

    @Import(module = "lwjgl", name = "glGetError")
    public static native int glGetError();

    @Import(module = "lwjgl", name = "glFlush")
    public static native void glFlush();

    @Import(module = "lwjgl", name = "glFinish")
    public static native void glFinish();

    @Import(module = "lwjgl", name = "glBlendFunc")
    public static native void glBlendFunc(int sfactor, int dfactor);

    @Import(module = "lwjgl", name = "glDepthFunc")
    public static native void glDepthFunc(int func);

    @Import(module = "lwjgl", name = "glCullFace")
    public static native void glCullFace(int mode);

    @Import(module = "lwjgl", name = "glFrontFace")
    public static native void glFrontFace(int dir);

    @Import(module = "lwjgl", name = "glPixelStorei")
    public static native void glPixelStorei(int pname, int param);

    @Import(module = "lwjgl", name = "glDrawArrays")
    public static native void glDrawArrays(int mode, int first, int count);

    @Import(module = "lwjgl", name = "glGetString")
    public static native long nglGetString(int name);

    public static String glGetString(int name) {
        return memUTF8Safe(nglGetString(name));
    }

    @Import(module = "lwjgl", name = "glGetIntegerv")
    public static native void nglGetIntegerv(int pname, long params);

    public static void glGetIntegerv(int pname, IntBuffer params) {
        nglGetIntegerv(pname, memAddress(params));
    }

    public static int glGetInteger(int pname) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            IntBuffer params = stack.callocInt(1);
            nglGetIntegerv(pname, memAddress(params));
            return params.get(0);
        } finally {
            stack.setPointer(sp);
        }
    }

    public static void glGetIntegerv(int pname, int[] params) {
        MemoryStack stack = stackGet();
        int sp = stack.getPointer();
        try {
            IntBuffer buffer = stack.callocInt(Math.max(params.length, 16));
            nglGetIntegerv(pname, memAddress(buffer));
            for (int i = 0; i < params.length; i++) {
                params[i] = buffer.get(i);
            }
        } finally {
            stack.setPointer(sp);
        }
    }

    @Import(module = "lwjgl", name = "glGetFloatv")
    public static native void nglGetFloatv(int pname, long params);

    public static void glGetFloatv(int pname, FloatBuffer params) {
        nglGetFloatv(pname, memAddress(params));
    }

    @Import(module = "lwjgl", name = "glReadPixels")
    public static native void nglReadPixels(int x, int y, int width, int height, int format, int type, long pixels);

    public static void glReadPixels(int x, int y, int width, int height, int format, int type, ByteBuffer pixels) {
        nglReadPixels(x, y, width, height, format, type, memAddress(pixels));
    }

    public static void glReadPixels(int x, int y, int width, int height, int format, int type, IntBuffer pixels) {
        nglReadPixels(x, y, width, height, format, type, memAddress(pixels));
    }

    public static void glReadPixels(int x, int y, int width, int height, int format, int type, FloatBuffer pixels) {
        nglReadPixels(x, y, width, height, format, type, memAddress(pixels));
    }

    public static void glReadPixels(int x, int y, int width, int height, int format, int type, long pixels) {
        nglReadPixels(x, y, width, height, format, type, pixels);
    }
}
