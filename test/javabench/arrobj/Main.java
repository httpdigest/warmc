// An array of objects, walked and updated in place.
//
// A Java array of references is, in TeaVM's output, a struct holding a wasm
// array of nullable references, so every element access is a struct.get of
// the data, an array.get with its bounds check, and a null check before the
// field is read.
public class Main {
    static final class Particle {
        int x, y, vx, vy;

        Particle(int seed) {
            x = seed * 17 % 1000;
            y = seed * 31 % 1000;
            vx = seed % 7 - 3;
            vy = seed % 5 - 2;
        }
    }

    public static void main(String[] args) {
        Particle[] ps = new Particle[4096];
        for (int i = 0; i < ps.length; i++) {
            ps[i] = new Particle(i);
        }
        long sum = 0;
        for (int step = 0; step < 6000; step++) {
            for (Particle p : ps) {
                p.x += p.vx;
                p.y += p.vy;
                if (p.x < 0 || p.x > 1000) {
                    p.vx = -p.vx;
                }
                if (p.y < 0 || p.y > 1000) {
                    p.vy = -p.vy;
                }
            }
            sum += ps[step & 4095].x;
        }
        for (Particle p : ps) {
            sum = sum * 31 + p.x + p.y;
        }
        System.out.println("sum = " + sum);
    }
}
