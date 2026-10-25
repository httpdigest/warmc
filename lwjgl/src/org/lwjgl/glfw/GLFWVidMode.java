package org.lwjgl.glfw;

import static org.lwjgl.system.MemoryUtil.memGetInt;

import org.lwjgl.system.Struct;

/** GLFWvidmode, a monitor's video mode, in GLFW's memory: LWJGL's, for warmc. */
public class GLFWVidMode extends Struct<GLFWVidMode> {
    public static final int SIZEOF = 24;
    public static final int ALIGNOF = 4;
    public static final int WIDTH = 0;
    public static final int HEIGHT = 4;
    public static final int REDBITS = 8;
    public static final int GREENBITS = 12;
    public static final int BLUEBITS = 16;
    public static final int REFRESHRATE = 20;

    protected GLFWVidMode(long address) {
        super(address, null);
    }

    public static GLFWVidMode create(long address) {
        return new GLFWVidMode(address);
    }

    public static GLFWVidMode createSafe(long address) {
        return address == 0 ? null : new GLFWVidMode(address);
    }

    @Override
    public int sizeof() {
        return SIZEOF;
    }

    public int width() {
        return memGetInt(address() + WIDTH);
    }

    public int height() {
        return memGetInt(address() + HEIGHT);
    }

    public int redBits() {
        return memGetInt(address() + REDBITS);
    }

    public int greenBits() {
        return memGetInt(address() + GREENBITS);
    }

    public int blueBits() {
        return memGetInt(address() + BLUEBITS);
    }

    public int refreshRate() {
        return memGetInt(address() + REFRESHRATE);
    }
}
