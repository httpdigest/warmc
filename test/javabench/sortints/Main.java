// Sorting int arrays: the class library's Arrays.sort and a merge sort.
//
// Arrays.sort(int[]) is TeaVM's own sort over a wasm array of i32; the merge
// sort beside it is the program's, with the same arrays and bounds checks.
import java.util.Arrays;

public class Main {
    static void mergeSort(int[] a, int[] tmp, int lo, int hi) {
        if (hi - lo < 2) {
            return;
        }
        int mid = (lo + hi) >>> 1;
        mergeSort(a, tmp, lo, mid);
        mergeSort(a, tmp, mid, hi);
        int i = lo, j = mid, k = lo;
        while (i < mid && j < hi) {
            tmp[k++] = a[i] <= a[j] ? a[i++] : a[j++];
        }
        while (i < mid) {
            tmp[k++] = a[i++];
        }
        while (j < hi) {
            tmp[k++] = a[j++];
        }
        System.arraycopy(tmp, lo, a, lo, hi - lo);
    }

    public static void main(String[] args) {
        int n = 200_000;
        int[] a = new int[n], b = new int[n], tmp = new int[n];
        long check = 0;
        int seed = 42;
        for (int round = 0; round < 3; round++) {
            for (int i = 0; i < n; i++) {
                seed = seed * 1664525 + 1013904223;
                a[i] = seed >> 4;
                b[i] = seed % 100_000;
            }
            Arrays.sort(a);
            mergeSort(b, tmp, 0, n);
            for (int i = 0; i < n; i += 997) {
                check = check * 31 + a[i] + b[i];
            }
        }
        System.out.println("check = " + check);
    }
}
