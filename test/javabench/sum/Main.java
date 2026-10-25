// A tight arithmetic loop over ints and longs.
//
// Nothing to allocate and nothing to call: the code TeaVM writes for a
// counted loop, and what the engine makes of its locals.
public class Main {
    public static void main(String[] args) {
        long s = 0;
        int x = 1;
        for (int i = 0; i < 60_000_000; i++) {
            x = x * 1664525 + 1013904223;
            s += (x >>> 9) % 1000 + (i & 7);
        }
        System.out.println("s = " + s);
    }
}
