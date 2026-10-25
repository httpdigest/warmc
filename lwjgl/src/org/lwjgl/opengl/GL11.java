package org.lwjgl.opengl;

import org.teavm.interop.Import;

/**
 * OpenGL 1.1 with the compatibility profile's functions: LWJGL's GL11, for
 * warmc.  macOS has them in a legacy context, the one GLFW makes unless asked
 * for a core profile.
 */
public class GL11 extends GL11C {
    public static final int
        GL_QUADS = 0x7, GL_QUAD_STRIP = 0x8, GL_POLYGON = 0x9,
        GL_MATRIX_MODE = 0xBA0, GL_MODELVIEW = 0x1700, GL_PROJECTION = 0x1701, GL_TEXTURE = 0x1702,
        GL_SMOOTH = 0x1D01, GL_FLAT = 0x1D00, GL_LIGHTING = 0xB50, GL_TEXTURE_2D = 0xDE1;

    protected GL11() {
        throw new UnsupportedOperationException();
    }

    @Import(module = "lwjgl", name = "glBegin")
    public static native void glBegin(int mode);

    @Import(module = "lwjgl", name = "glEnd")
    public static native void glEnd();

    @Import(module = "lwjgl", name = "glVertex2f")
    public static native void glVertex2f(float x, float y);

    @Import(module = "lwjgl", name = "glVertex3f")
    public static native void glVertex3f(float x, float y, float z);

    @Import(module = "lwjgl", name = "glColor3f")
    public static native void glColor3f(float red, float green, float blue);

    @Import(module = "lwjgl", name = "glColor4f")
    public static native void glColor4f(float red, float green, float blue, float alpha);

    @Import(module = "lwjgl", name = "glMatrixMode")
    public static native void glMatrixMode(int mode);

    @Import(module = "lwjgl", name = "glLoadIdentity")
    public static native void glLoadIdentity();

    @Import(module = "lwjgl", name = "glOrtho")
    public static native void glOrtho(double l, double r, double b, double t, double n, double f);

    @Import(module = "lwjgl", name = "glRotatef")
    public static native void glRotatef(float angle, float x, float y, float z);

    @Import(module = "lwjgl", name = "glTranslatef")
    public static native void glTranslatef(float x, float y, float z);

    @Import(module = "lwjgl", name = "glScalef")
    public static native void glScalef(float x, float y, float z);

    @Import(module = "lwjgl", name = "glPushMatrix")
    public static native void glPushMatrix();

    @Import(module = "lwjgl", name = "glPopMatrix")
    public static native void glPopMatrix();

    @Import(module = "lwjgl", name = "glShadeModel")
    public static native void glShadeModel(int mode);
}
