package org.lwjgl.opengl;

/** What the current context supports: LWJGL's GLCapabilities, for warmc. */
public final class GLCapabilities {
    public final boolean OpenGL11;
    public final boolean OpenGL12;
    public final boolean OpenGL13;
    public final boolean OpenGL14;
    public final boolean OpenGL15;
    public final boolean OpenGL20;
    public final boolean OpenGL21;
    public final boolean OpenGL30;
    public final boolean OpenGL31;
    public final boolean OpenGL32;
    public final boolean OpenGL33;
    public final boolean OpenGL40;
    public final boolean OpenGL41;
    public final boolean OpenGL42;
    public final boolean OpenGL43;
    public final boolean OpenGL44;
    public final boolean OpenGL45;
    public final boolean OpenGL46;
    public final boolean forwardCompatible;

    GLCapabilities(int major, int minor, boolean forwardCompatible) {
        int v = major * 10 + minor;
        OpenGL11 = v >= 11;
        OpenGL12 = v >= 12;
        OpenGL13 = v >= 13;
        OpenGL14 = v >= 14;
        OpenGL15 = v >= 15;
        OpenGL20 = v >= 20;
        OpenGL21 = v >= 21;
        OpenGL30 = v >= 30;
        OpenGL31 = v >= 31;
        OpenGL32 = v >= 32;
        OpenGL33 = v >= 33;
        OpenGL40 = v >= 40;
        OpenGL41 = v >= 41;
        OpenGL42 = v >= 42;
        OpenGL43 = v >= 43;
        OpenGL44 = v >= 44;
        OpenGL45 = v >= 45;
        OpenGL46 = v >= 46;
        this.forwardCompatible = forwardCompatible;
    }
}
