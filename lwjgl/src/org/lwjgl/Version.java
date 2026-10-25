package org.lwjgl;

/** LWJGL's version: the release whose API these classes have, for warmc. */
public final class Version {
    public static final int VERSION_MAJOR = 3;
    public static final int VERSION_MINOR = 3;
    public static final int VERSION_REVISION = 6;
    public static final BuildType BUILD_TYPE = BuildType.STABLE;

    private Version() {
    }

    public static String getVersion() {
        return VERSION_MAJOR + "." + VERSION_MINOR + "." + VERSION_REVISION + " warmc";
    }

    public enum BuildType {
        ALPHA, BETA, STABLE
    }
}
