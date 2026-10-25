// The names of array classes (test/teavm/patches/array-simple-name.patch):
// an array's simple name is its item type's with [] after it, and its name
// is made from its item type's, so the item type needs the metadata that
// names it, which TeaVM gave only to the class itself.  A local class's
// simple name is the one that cannot be had from its name (Main$1Local).
public class Main {
    static class Inner {
    }

    interface Shape {
    }

    record Point(int x, int y) implements Shape {
    }

    enum Colour { RED }

    static void show(Class<?> c) {
        System.out.println(c.getName() + " / " + c.getSimpleName());
    }

    public static void main(String[] args) {
        show(int[].class);
        show(long[][].class);
        show(boolean[].class);
        show(char[][][].class);
        show(String[].class);
        show(Object[][].class);
        show(Main[].class);
        show(Inner[].class);
        show(Inner[][].class);
        show(Shape[].class);
        show(Point[].class);
        show(Colour[].class);
        show(Integer[].class);
        class Local {
        }
        show(Local[].class);
        show(Local[][].class);
        // and one whose arrays have arrays for items, no class of its own
        class Deep {
        }
        show(Deep[][].class);
        Object anonymous = new Object() {
        };
        show(java.lang.reflect.Array.newInstance(anonymous.getClass(), 0).getClass());
        // the runtime classes of arrays, not literals
        Object[] made = {new double[1], new Inner[2], new Point[0][0], new String[][] {{"a"}}};
        for (Object o : made) {
            show(o.getClass());
        }
        System.out.println(made.getClass().getSimpleName() + " " + made.getClass().getComponentType().getSimpleName());
        // and the classes themselves still
        show(int.class);
        show(Inner.class);
        show(Point.class);
        show(String.class);
    }
}
