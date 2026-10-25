package org.lwjgl.system.warmc;

import java.util.ArrayList;
import java.util.List;
import org.lwjgl.system.Configuration;

/**
 * What a program needs before LWJGL's classes run and after its main returns,
 * which the class lwjgl/build.py makes the entry point does around calling
 * the program's main: the platform, which LWJGL reads from os.name and
 * os.arch, and LWJGL's off-heap memory, warmc's backend and allocator; and
 * then the shutdown hooks, which TeaVM has not (lwjgl/teavm/
 * LwjglTransformer.java has LWJGL's classes add them here).
 */
public final class Setup {
    private static final List<Thread> hooks = new ArrayList<>();

    private Setup() {
    }

    public static void run() {
        System.setProperty("os.name", "Mac OS X");
        System.setProperty("os.arch", "aarch64");
        Configuration.MEMORY_BACKEND.set(new WarmcMemory());
        Configuration.MEMORY_ALLOCATOR.set(new WarmcAllocator());
    }

    public static void addShutdownHook(Thread hook) {
        hooks.add(hook);
    }

    public static void exit() {
        for (Thread hook : hooks) {
            hook.run();
        }
        hooks.clear();
    }
}
