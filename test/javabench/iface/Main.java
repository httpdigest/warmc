// Interface calls at a megamorphic site.
//
// Four classes implement one interface and an array mixes them, so the call
// cannot be devirtualised: TeaVM finds the method in the receiver's interface
// table and calls it with call_ref.
public class Main {
    interface Shape {
        long area(int k);
    }

    static final class Sq implements Shape {
        final int s;
        Sq(int s) { this.s = s; }
        public long area(int k) { return (long) (s + k) * (s + k); }
    }

    static final class Rect implements Shape {
        final int w, h;
        Rect(int w, int h) { this.w = w; this.h = h; }
        public long area(int k) { return (long) (w + k) * h; }
    }

    static final class Tri implements Shape {
        final int b, h;
        Tri(int b, int h) { this.b = b; this.h = h; }
        public long area(int k) { return (long) b * (h + k) / 2; }
    }

    static final class Ring implements Shape {
        final int r;
        Ring(int r) { this.r = r; }
        public long area(int k) { return 3L * r * r - k; }
    }

    public static void main(String[] args) {
        Shape[] shapes = new Shape[1024];
        for (int i = 0; i < shapes.length; i++) {
            shapes[i] = switch (i % 4) {
                case 0 -> new Sq(i);
                case 1 -> new Rect(i, i + 1);
                case 2 -> new Tri(i, 3);
                default -> new Ring(i % 50);
            };
        }
        long sum = 0;
        for (int k = 0; k < 18000; k++) {
            for (Shape s : shapes) {
                sum += s.area(k & 15);
            }
        }
        System.out.println("sum = " + sum);
    }
}
