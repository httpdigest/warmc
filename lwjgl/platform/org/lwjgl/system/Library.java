package org.lwjgl.system;

import java.util.function.Consumer;
import java.util.function.Supplier;
import org.lwjgl.system.warmc.ProcessLibrary;

/**
 * LWJGL's library loading, for warmc, which loads nothing: LWJGL's own native
 * code is C functions lwjgl/native/lwjgl.c defines, and GLFW and OpenGL are
 * linked into the program, which finds their functions by name in itself
 * ({@link ProcessLibrary}).  The methods are LWJGL's, so that its classes
 * call them as they do on the JVM.
 */
public final class Library {
    public static final String JNI_LIBRARY_NAME = "lwjgl";
    static final String JAVA_LIBRARY_PATH = "java.library.path";

    private Library() {
    }

    public static void initialize() {
    }

    public static void loadSystem(String module, String name) {
    }

    public static void loadSystem(Consumer<String> load, Consumer<String> loadLibrary, Class<?> context,
            String module, String name) {
    }

    public static SharedLibrary loadNative(String module, String name) {
        return ProcessLibrary.of(name);
    }

    public static SharedLibrary loadNative(Class<?> context, String module, String name) {
        return ProcessLibrary.of(name);
    }

    public static SharedLibrary loadNative(Class<?> context, String module, String name, boolean bundledWithLWJGL) {
        return ProcessLibrary.of(name);
    }

    public static SharedLibrary loadNative(Class<?> context, String module, Configuration<String> name,
            String... defaultNames) {
        return loadNative(context, module, name, null, defaultNames);
    }

    public static SharedLibrary loadNative(Class<?> context, String module, Configuration<String> name,
            Supplier<SharedLibrary> fallback, String... defaultNames) {
        String libraryName = name == null ? null : name.get();
        return ProcessLibrary.of(libraryName != null ? libraryName : defaultNames.length > 0 ? defaultNames[0] : module);
    }

    static void printError(String message) {
        System.err.println("[LWJGL] " + message);
    }
}
