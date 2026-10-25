// Dense matrix multiplication over double[][].
//
// A double[][] is an array of references to row objects, each a struct around
// a wasm array of f64: the inner loop is two array.gets with bounds checks
// and a multiply-add, with the row loads hoisted if the engine can.
public class Main {
    public static void main(String[] args) {
        int n = 120;
        double[][] a = new double[n][n], b = new double[n][n], c = new double[n][n];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                a[i][j] = (i * 7 + j * 3) % 11 - 5;
                b[i][j] = (i * 5 + j * 2) % 13 - 6;
            }
        }
        double trace = 0;
        for (int r = 0; r < 48; r++) {
            for (int i = 0; i < n; i++) {
                double[] ai = a[i], ci = c[i];
                for (int j = 0; j < n; j++) {
                    ci[j] = 0;
                }
                for (int k = 0; k < n; k++) {
                    double aik = ai[k];
                    double[] bk = b[k];
                    for (int j = 0; j < n; j++) {
                        ci[j] += aik * bk[j];
                    }
                }
            }
            for (int i = 0; i < n; i++) {
                trace += c[i][i];
                a[i][(i + r) % n] += 1;
            }
        }
        System.out.println("trace = " + trace);
    }
}
