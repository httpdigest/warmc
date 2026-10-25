// A blue OpenGL window through LWJGL's API (lwjgl/), checked rather than
// looked at: what GLFW and OpenGL answer, an error GLFW reports through the
// error callback, an exception a callback throws coming out of the GLFW call
// that ran it, and the pixels a few frames cleared to blue read back.
import static org.lwjgl.glfw.Callbacks.glfwFreeCallbacks;
import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.system.MemoryStack.stackPush;
import static org.lwjgl.system.MemoryUtil.NULL;

import java.nio.ByteBuffer;
import java.nio.IntBuffer;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWVidMode;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GLCapabilities;
import org.lwjgl.system.MemoryStack;

public class Main {
    static int errors;
    static String lastError;

    public static void main(String[] args) {
        // a lambda for the error callback, which records what GLFW reports
        GLFWErrorCallback previous = glfwSetErrorCallback((error, description) -> {
            errors++;
            lastError = String.format("0x%X ", error) + GLFWErrorCallback.getDescription(description);
        });
        System.out.println("no callback before: " + (previous == null));
        if (!glfwInit()) {
            System.out.println("glfwInit failed");
            return;
        }
        int[] major = new int[1], minor = new int[1], rev = new int[1];
        glfwGetVersion(major, minor, rev);
        System.out.println("GLFW " + major[0] + "." + minor[0] + "." + rev[0]);

        // GLFW_INVALID_ENUM, through the callback
        glfwWindowHint(0x7FFFFFFF, 0);
        System.out.println("errors " + errors + ": " + lastError);

        // a callback that throws: GLFW's call returns, and then the exception
        glfwSetErrorCallback(GLFWErrorCallback.createThrow());
        try {
            glfwWindowHint(0x7FFFFFFF, 0);
            System.out.println("no exception");
        } catch (IllegalStateException e) {
            System.out.println("thrown after the call: " + e.getMessage());
        }
        glfwSetErrorCallback(GLFWErrorCallback.createPrint(System.err)).free();

        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        long window = glfwCreateWindow(200, 150, "warmc: a blue window", NULL, NULL);
        if (window == NULL) {
            System.out.println("no window");
            return;
        }
        int[] keys = new int[1];
        glfwSetKeyCallback(window, (w, key, scancode, action, mods) -> keys[0]++);

        try (MemoryStack stack = stackPush()) {
            IntBuffer w = stack.mallocInt(1);
            IntBuffer h = stack.mallocInt(1);
            glfwGetWindowSize(window, w, h);
            System.out.println("window " + w.get(0) + "x" + h.get(0));
            GLFWVidMode mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
            System.out.println("a video mode: " + (mode.width() > 0 && mode.height() > 0));
            glfwSetWindowPos(window, (mode.width() - w.get(0)) / 2, (mode.height() - h.get(0)) / 2);
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glfwShowWindow(window);

        GLCapabilities caps = GL.createCapabilities();
        System.out.println("OpenGL 1.1: " + caps.OpenGL11 + ", 2.1: " + caps.OpenGL21);
        System.out.println("renderer: " + (glGetString(GL_RENDERER) != null) + ", vendor: "
                + (glGetString(GL_VENDOR) != null));

        int[] fb = new int[1], fbh = new int[1];
        glfwGetFramebufferSize(window, fb, fbh);
        glViewport(0, 0, fb[0], fbh[0]);
        int[] viewport = new int[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        System.out.println("viewport matches the framebuffer: " + (viewport[2] == fb[0] && viewport[3] == fbh[0]));

        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        int frames = 0;
        String pixel = "";
        while (!glfwWindowShouldClose(window) && frames < 30) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            try (MemoryStack stack = stackPush()) {
                ByteBuffer rgba = stack.malloc(4);
                glReadPixels(fb[0] / 2, fbh[0] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                pixel = (rgba.get(0) & 0xFF) + " " + (rgba.get(1) & 0xFF) + " " + (rgba.get(2) & 0xFF) + " "
                        + (rgba.get(3) & 0xFF);
            }
            glfwSwapBuffers(window);
            glfwPollEvents();
            frames++;
        }
        System.out.println("frames " + frames + ", pixel " + pixel + ", GL error " + glGetError());

        glfwFreeCallbacks(window);
        glfwDestroyWindow(window);
        glfwTerminate();
        glfwSetErrorCallback(null).free();
        System.out.println("done");
    }
}
