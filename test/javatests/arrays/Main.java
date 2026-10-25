// Arrays: every element type, multi-dimensional and jagged arrays, default
// values, clone, System.arraycopy with overlap, Arrays' sort, fill, copyOf,
// copyOfRange, binarySearch, equals, deepEquals, hashCode, deepToString, and
// arrays of arrays of objects.
//
// TeaVM makes a Java array a struct around a wasm array of the element type
// -- i8 for byte and boolean, i16 for short and char, f32, f64, i64, or
// nullable references -- so these are packed loads with sign or zero
// extension, array.copy for arraycopy, and array.new_default.
import java.util.Arrays;
import java.util.Comparator;

public class Main {
    public static void main(String[] args) {
        System.out.println("== defaults ==");
        System.out.println((new boolean[2])[0] + "," + (new byte[1])[0] + "," + (new short[1])[0] + "," + (int) (new char[1])[0]
                + "," + (new int[1])[0] + "," + (new long[1])[0] + "," + (new float[1])[0] + "," + (new double[1])[0]
                + "," + (new String[1])[0]);

        System.out.println("== packed widths ==");
        byte[] bs = {127, -128, (byte) 200, 0, -1};
        short[] ss = {32767, -32768, (short) 40000};
        char[] cs = {'a', '￿', (char) -1, 'Z'};
        int bsum = 0;
        for (byte b : bs) {
            bsum += b;
        }
        System.out.println("bytes " + Arrays.toString(bs) + " sum " + bsum + " masked " + (bs[2] & 0xff));
        System.out.println("shorts " + Arrays.toString(ss) + " chars " + (int) cs[1] + " " + (int) cs[2] + " " + cs[3]);
        boolean[] flags = new boolean[10];
        for (int i = 0; i < flags.length; i += 3) {
            flags[i] = true;
        }
        System.out.println("flags " + Arrays.toString(flags));
        float[] fs = {1.5f, -0.0f, Float.NaN, Float.MAX_VALUE};
        System.out.println("floats " + Arrays.toString(fs));
        long[] ls = {Long.MIN_VALUE, -1, 1L << 40};
        System.out.println("longs " + Arrays.toString(ls));

        System.out.println("== multi-dimensional ==");
        int[][] grid = new int[4][5];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 5; j++) {
                grid[i][j] = i * 10 + j;
            }
        }
        System.out.println(Arrays.deepToString(grid));
        int[][] jag = new int[4][];
        for (int i = 0; i < jag.length; i++) {
            jag[i] = new int[i + 1];
            Arrays.fill(jag[i], i);
        }
        System.out.println(Arrays.deepToString(jag));
        double[][][] cube = new double[2][3][2];
        cube[1][2][1] = 9.5;
        System.out.println(Arrays.deepToString(cube));
        String[][] names = {{"a", "b"}, {}, {null, "c"}};
        System.out.println(Arrays.deepToString(names) + " " + names[1].length);

        System.out.println("== clone and arraycopy ==");
        int[] src = {1, 2, 3, 4, 5, 6, 7, 8};
        int[] cl = src.clone();
        cl[0] = 99;
        System.out.println(src[0] + " " + cl[0] + " " + (src != cl));
        System.arraycopy(src, 0, src, 2, 5);
        System.out.println("overlap right " + Arrays.toString(src));
        System.arraycopy(src, 3, src, 1, 4);
        System.out.println("overlap left " + Arrays.toString(src));
        Object[] objs = {"x", 1, 2.5, null};
        Object[] objs2 = new Object[6];
        System.arraycopy(objs, 0, objs2, 1, 4);
        System.out.println(Arrays.toString(objs2));
        int[][] deep = {{1}, {2}};
        int[][] shallow = deep.clone();
        shallow[0][0] = 7;
        System.out.println("shared rows " + deep[0][0]);

        System.out.println("== Arrays utilities ==");
        int[] r = new int[20];
        int seed = 17;
        for (int i = 0; i < r.length; i++) {
            seed = seed * 1103515245 + 12345;
            r[i] = (seed >>> 16) % 100 - 50;
        }
        int[] sorted = r.clone();
        Arrays.sort(sorted);
        System.out.println(Arrays.toString(sorted));
        System.out.println("search " + Arrays.binarySearch(sorted, sorted[7]) + " missing " + Arrays.binarySearch(sorted, 1000));
        int[] part = Arrays.copyOfRange(r, 3, 9);
        Arrays.sort(r, 5, 15);
        System.out.println(Arrays.toString(part) + " " + Arrays.toString(r));
        System.out.println(Arrays.toString(Arrays.copyOf(new int[] {1, 2}, 5)) + " " + Arrays.toString(Arrays.copyOf(new String[] {"q"}, 3)));
        System.out.println(Arrays.equals(new int[] {1, 2}, new int[] {1, 2}) + " " + Arrays.deepEquals(grid, grid.clone())
                + " " + Arrays.hashCode(new int[] {1, 2, 3}) + " " + Arrays.hashCode(new long[] {1L << 33}));
        Integer[] boxed = {5, 3, 9, 1, 7};
        Arrays.sort(boxed, Comparator.reverseOrder());
        System.out.println(Arrays.toString(boxed));
        String[] words = {"pear", "Fig", "apple", "fig", "Banana"};
        Arrays.sort(words, String.CASE_INSENSITIVE_ORDER);
        System.out.println(Arrays.toString(words));
        Arrays.sort(words);
        System.out.println(Arrays.toString(words) + " " + Arrays.asList(words).indexOf("fig"));
        double[] ds = {3.5, -0.0, 0.0, Double.NaN, -1e300, 2};
        Arrays.sort(ds);
        System.out.println(Arrays.toString(ds));
        char[] letters = "hello world".toCharArray();
        Arrays.sort(letters);
        System.out.println(new String(letters).trim() + "|" + String.valueOf(letters, 2, 3));

        // An Integer stored into this array is a wasm cast failure under
        // TeaVM rather than an ArrayStoreException, so only a String is.
        System.out.println("== covariance ==");
        Object[] asObj = new String[2];
        asObj[0] = "ok";
        asObj[1] = asObj[0] + "!";
        System.out.println((asObj instanceof String[]) + " " + (asObj.getClass() == String[].class) + " " + asObj[1]);
    }
}
