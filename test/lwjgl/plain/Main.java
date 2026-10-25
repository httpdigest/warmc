// A program lwjgl/build.py builds that calls nothing of GLFW's or OpenGL's:
// no native import for warmc, which links it with the system linker still.
public class Main {
    public static void main(String[] args) {
        System.out.println("no C called");
    }
}
