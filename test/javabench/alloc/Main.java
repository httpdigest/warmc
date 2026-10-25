// Short-lived objects: a small immutable point made and dropped on every step.
//
// None of them outlives the iteration that makes it, so an engine that keeps
// such an object in registers allocates nothing, and one that does not spends
// the loop in its allocator and collector.  TeaVM makes each `new` a
// struct.new of the class's struct type, with its vtable as the first field.
public class Main {
    static final class Vec {
        final double x, y;

        Vec(double x, double y) {
            this.x = x;
            this.y = y;
        }

        Vec add(Vec o) {
            return new Vec(x + o.x, y + o.y);
        }

        Vec scale(double k) {
            return new Vec(x * k, y * k);
        }

        double dot(Vec o) {
            return x * o.x + y * o.y;
        }
    }

    public static void main(String[] args) {
        Vec p = new Vec(1, 0);
        Vec v = new Vec(0, 1);
        double acc = 0;
        for (int i = 0; i < 18_000_000; i++) {
            Vec step = v.scale(0.001);
            p = p.add(step);
            v = v.add(p.scale(-0.001));
            acc += p.dot(v);
        }
        System.out.println("acc = " + Double.doubleToLongBits(acc) + " p = " + Double.doubleToLongBits(p.x));
    }
}
