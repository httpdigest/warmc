package org.lwjgl.opengl;

/**
 * OpenGL's capabilities: LWJGL's GL, for warmc.  OpenGL's functions are called
 * directly, linked against the platform's OpenGL, so there are no function
 * pointers to load: createCapabilities reads what the current context is.
 */
public final class GL {
    private static GLCapabilities capabilities;

    private GL() {
    }

    public static GLCapabilities createCapabilities() {
        return createCapabilities(false);
    }

    public static GLCapabilities createCapabilities(boolean forwardCompatible) {
        String version = GL11C.glGetString(GL11C.GL_VERSION);
        if (version == null) {
            throw new IllegalStateException("There is no OpenGL context current in the current thread.");
        }
        int major = 0;
        int minor = 0;
        int i = 0;
        while (i < version.length() && Character.isDigit(version.charAt(i))) {
            major = major * 10 + version.charAt(i++) - '0';
        }
        if (i < version.length() && version.charAt(i) == '.') {
            i++;
            while (i < version.length() && Character.isDigit(version.charAt(i))) {
                minor = minor * 10 + version.charAt(i++) - '0';
            }
        }
        capabilities = new GLCapabilities(major, minor, forwardCompatible);
        return capabilities;
    }

    public static GLCapabilities getCapabilities() {
        if (capabilities == null) {
            throw new IllegalStateException("No GLCapabilities instance set for the current thread. "
                    + "Possible solutions:\n\ta) Call GL.createCapabilities() after making a context current "
                    + "in the current thread.\n\tb) Call GL.setCapabilities() if a GLCapabilities instance "
                    + "already exists for the current context.");
        }
        return capabilities;
    }

    public static void setCapabilities(GLCapabilities caps) {
        capabilities = caps;
    }
}
