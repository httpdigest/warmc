// Classes: constructors chaining with this() and super(), initialiser order
// (static, instance, field, constructor), overriding and super calls,
// abstract methods, protected and private members, static and instance
// nested classes, inner classes reaching the outer instance, local and
// anonymous classes capturing effectively final locals, instanceof and
// casts, getClass and its names, and static fields shared across instances.
public class Main {
    static StringBuilder log = new StringBuilder();

    static void note(String s) {
        log.append(s).append(' ');
    }

    static class Base {
        static {
            note("Base.static");
        }

        protected int x = init("Base.field", 1);

        {
            note("Base.init");
        }

        Base() {
            this(10);
            note("Base()");
        }

        Base(int x) {
            note("Base(" + x + ")");
            this.x += x;
            describe();
        }

        static int init(String what, int v) {
            note(what);
            return v;
        }

        void describe() {
            note("Base.describe x=" + x);
        }

        String who() {
            return "Base";
        }
    }

    static class Derived extends Base {
        static {
            note("Derived.static");
        }

        int y = init("Derived.field", 2);

        Derived() {
            super(5);
            note("Derived() y=" + y);
        }

        @Override
        void describe() {
            note("Derived.describe y=" + y);
        }

        @Override
        String who() {
            return "Derived<" + super.who() + ">";
        }
    }

    abstract static class Animal {
        private final String name;
        private static int count;

        Animal(String name) {
            this.name = name;
            count++;
        }

        abstract String sound();

        String speak() {
            return name + " says " + sound();
        }

        static int count() {
            return count;
        }
    }

    static final class Dog extends Animal {
        Dog() {
            super("dog");
        }

        String sound() {
            return "woof";
        }
    }

    static class Cat extends Animal {
        Cat(String n) {
            super(n);
        }

        String sound() {
            return "meow";
        }
    }

    static class Kitten extends Cat {
        Kitten() {
            super("kitten");
        }

        @Override
        String sound() {
            return super.sound().toUpperCase() + "?";
        }
    }

    private int counter = 100;

    class Inner {
        int bump() {
            return ++counter;
        }
    }

    static class Counter {
        private int n;

        Counter add(int k) {
            n += k;
            return this;
        }

        int get() {
            return n;
        }
    }

    interface Op {
        int apply(int v);
    }

    static Op adder(int k) {
        class Local implements Op {
            public int apply(int v) {
                return v + k;
            }
        }
        return new Local();
    }

    public static void main(String[] args) {
        System.out.println("== initialisation order ==");
        new Base();
        System.out.println(log.toString().trim());
        log.setLength(0);
        Derived d = new Derived();
        System.out.println(log.toString().trim());
        System.out.println(d.x + " " + d.y + " " + d.who());
        Base b = d;
        System.out.println(b.who() + " " + (b instanceof Derived) + " " + b.getClass().getName());

        System.out.println("== abstract and overriding ==");
        Animal[] zoo = {new Dog(), new Cat("cat"), new Kitten()};
        for (Animal a : zoo) {
            System.out.println(a.speak() + " (" + a.getClass().getSimpleName() + ")");
        }
        System.out.println("count " + Animal.count() + " " + (zoo[2] instanceof Cat) + " " + (zoo[1] instanceof Kitten));

        System.out.println("== inner, local and anonymous ==");
        Main outer = new Main();
        Main.Inner in1 = outer.new Inner();
        Main.Inner in2 = outer.new Inner();
        in1.bump();
        in2.bump();
        System.out.println("outer counter " + outer.counter + " " + in1.bump());
        System.out.println(adder(5).apply(10) + " " + adder(-3).apply(10));
        int base = 7;
        Op anon = new Op() {
            int calls;

            public int apply(int v) {
                return v * base + ++calls;
            }
        };
        System.out.println(anon.apply(2) + " " + anon.apply(2) + " " + anon.getClass().getSimpleName().isEmpty());
        System.out.println(new Counter().add(3).add(4).get());

        System.out.println("== casts ==");
        Object o = zoo[2];
        Cat c = (Cat) o;
        System.out.println(c.speak());
        try {
            Dog dog = (Dog) o;
            System.out.println("cast " + dog);
        } catch (ClassCastException e) {
            System.out.println("ClassCastException");
        }
        Object n = 42;
        System.out.println((n instanceof Integer i ? i + 1 : -1) + " " + (n instanceof Number) + " " + (n instanceof Comparable<?>));
    }
}
