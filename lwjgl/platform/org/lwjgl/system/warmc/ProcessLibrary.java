package org.lwjgl.system.warmc;

import java.nio.ByteBuffer;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.system.SharedLibrary;
import org.teavm.interop.Import;

/**
 * A library LWJGL loads, for warmc: the program itself, which has GLFW linked
 * into it and OpenGL's framework linked against it, its functions found by
 * name with dlsym among all the program's symbols (RTLD_DEFAULT).
 */
public final class ProcessLibrary extends SharedLibrary.Default {
    // dlfcn.h's RTLD_DEFAULT on macOS
    private static final long RTLD_DEFAULT = -2;

    @Import(module = "lwjgl", name = "dlsym")
    private static native long dlsym(long handle, long name);

    private ProcessLibrary(String name) {
        super(name, RTLD_DEFAULT);
    }

    public static SharedLibrary of(String name) {
        return new ProcessLibrary(name);
    }

    @Override
    public String getPath() {
        return null;
    }

    @Override
    public long getFunctionAddress(ByteBuffer functionName) {
        return dlsym(RTLD_DEFAULT, MemoryUtil.memAddress(functionName));
    }

    @Override
    public void free() {
    }
}
