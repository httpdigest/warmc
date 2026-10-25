/*
 * The functions TeaVM's Math leaves to its host -- sin, cos, tan, asin, acos,
 * atan, atan2, exp, log, pow -- for test/teavm/wasi.py to give a module in
 * place of JavaScript's Math.  TeaVM compiles this class into every module the
 * corpora build, and each function is exported as warmc.fdlibm.NAME for the
 * adapter to call; it removes the exports, so a function the program does not
 * reach is code nothing calls.
 *
 * A translation of fdlibm 5.3 (netlib), which is what java.lang.StrictMath is
 * specified to compute and what V8's Math ports: test/teavm/FdLibmCheck.java
 * holds every function to StrictMath, bit for bit, on the JVM.  The notice
 * fdlibm asks to be kept with it:
 *
 * ====================================================
 * Copyright (C) 1993-2004 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
package warmc.teavm;

import org.teavm.interop.Export;

public final class FdLibm {
    private FdLibm() {
    }

    private static int hi(double x) {
        return (int) (Double.doubleToRawLongBits(x) >> 32);
    }

    private static int lo(double x) {
        return (int) Double.doubleToRawLongBits(x);
    }

    private static double withHi(double x, int hi) {
        return Double.longBitsToDouble(((long) hi << 32) | (Double.doubleToRawLongBits(x) & 0xffffffffL));
    }

    private static double withLo(double x, int lo) {
        return Double.longBitsToDouble((Double.doubleToRawLongBits(x) & 0xffffffff00000000L) | (lo & 0xffffffffL));
    }

    private static double fromWords(int hi, int lo) {
        return Double.longBitsToDouble(((long) hi << 32) | (lo & 0xffffffffL));
    }

    private static final double TWO54 = 1.80143985094819840000e+16;
    private static final double TWOM54 = 5.55111512312578270212e-17;
    private static final double HUGE = 1.0e+300;
    private static final double TINY = 1.0e-300;

    static double scalbn(double x, int n) {
        int hx = hi(x);
        int lx = lo(x);
        int k = (hx & 0x7ff00000) >> 20;
        if (k == 0) {
            if ((lx | (hx & 0x7fffffff)) == 0) {
                return x;
            }
            x *= TWO54;
            hx = hi(x);
            k = ((hx & 0x7ff00000) >> 20) - 54;
            if (n < -50000) {
                return TINY * x;
            }
        }
        if (k == 0x7ff) {
            return x + x;
        }
        k = k + n;
        if (k > 0x7fe) {
            return HUGE * Math.copySign(HUGE, x);
        }
        if (k > 0) {
            return withHi(x, (hx & 0x800fffff) | (k << 20));
        }
        if (k <= -54) {
            if (n > 50000) {
                return HUGE * Math.copySign(HUGE, x);
            }
            return TINY * Math.copySign(TINY, x);
        }
        k += 54;
        x = withHi(x, (hx & 0x800fffff) | (k << 20));
        return x * TWOM54;
    }

    // ---------------------------------------------------------------- k_sin.c

    private static final double S1 = -1.66666666666666324348e-01;
    private static final double S2 = 8.33333333332248946124e-03;
    private static final double S3 = -1.98412698298579493134e-04;
    private static final double S4 = 2.75573137070700676789e-06;
    private static final double S5 = -2.50507602534068634195e-08;
    private static final double S6 = 1.58969099521155010221e-10;

    static double kernelSin(double x, double y, int iy) {
        int ix = hi(x) & 0x7fffffff;
        if (ix < 0x3e400000) {
            if ((int) x == 0) {
                return x;
            }
        }
        double z = x * x;
        double v = z * x;
        double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
        if (iy == 0) {
            return x + v * (S1 + z * r);
        }
        return x - ((z * (0.5 * y - v * r) - y) - v * S1);
    }

    // ---------------------------------------------------------------- k_cos.c

    private static final double C1 = 4.16666666666666019037e-02;
    private static final double C2 = -1.38888888888741095749e-03;
    private static final double C3 = 2.48015872894767294178e-05;
    private static final double C4 = -2.75573143513906633035e-07;
    private static final double C5 = 2.08757232129817482790e-09;
    private static final double C6 = -1.13596475577881948265e-11;

    static double kernelCos(double x, double y) {
        int ix = hi(x) & 0x7fffffff;
        if (ix < 0x3e400000) {
            if ((int) x == 0) {
                return 1.0;
            }
        }
        double z = x * x;
        double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
        if (ix < 0x3FD33333) {
            return 1.0 - (0.5 * z - (z * r - x * y));
        }
        double qx;
        if (ix > 0x3fe90000) {
            qx = 0.28125;
        } else {
            qx = fromWords(ix - 0x00200000, 0);
        }
        double hz = 0.5 * z - qx;
        double a = 1.0 - qx;
        return a - (hz - (z * r - x * y));
    }

    // ---------------------------------------------------------------- k_tan.c

    private static final double[] T = {
        3.33333333333334091986e-01,
        1.33333333333201242699e-01,
        5.39682539762260521377e-02,
        2.18694882948595424599e-02,
        8.86323982359930005737e-03,
        3.59207910759131235356e-03,
        1.45620945432529025516e-03,
        5.88041240820264096874e-04,
        2.46463134818469906812e-04,
        7.81794442939557092300e-05,
        7.14072491382608190305e-05,
        -1.85586374855275456654e-05,
        2.59073051863633712884e-05,
    };
    private static final double PIO4 = 7.85398163397448278999e-01;
    private static final double PIO4LO = 3.06161699786838301793e-17;

    static double kernelTan(double x, double y, int iy) {
        double z, r, v, w, s;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        if (ix < 0x3e300000) {
            if ((int) x == 0) {
                if (((ix | lo(x)) | (iy + 1)) == 0) {
                    return 1.0 / Math.abs(x);
                }
                if (iy == 1) {
                    return x;
                }
                double a, t;
                z = w = x + y;
                z = withLo(z, 0);
                v = y - (z - x);
                t = a = -1.0 / w;
                t = withLo(t, 0);
                s = 1.0 + t * z;
                return t + a * (s + t * v);
            }
        }
        if (ix >= 0x3FE59428) {
            if (hx < 0) {
                x = -x;
                y = -y;
            }
            z = PIO4 - x;
            w = PIO4LO - y;
            x = z + w;
            y = 0.0;
        }
        z = x * x;
        w = z * z;
        r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
        v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
        s = z * x;
        r = y + z * (s * (r + v) + y);
        r += T[0] * s;
        w = x + r;
        if (ix >= 0x3FE59428) {
            v = (double) iy;
            return (double) (1 - ((hx >> 30) & 2)) * (v - 2.0 * (x - (w * w / (w + v) - r)));
        }
        if (iy == 1) {
            return w;
        }
        double a, t;
        z = w;
        z = withLo(z, 0);
        v = r - (z - x);
        t = a = -1.0 / w;
        t = withLo(t, 0);
        s = 1.0 + t * z;
        return t + a * (s + t * v);
    }

    // ---------------------------------------------------------------- e_rem_pio2.c

    private static final int[] TWO_OVER_PI = {
        0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
        0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
        0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
        0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
        0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
        0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
        0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
        0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
        0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
        0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
        0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
    };

    private static final int[] NPIO2_HW = {
        0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C,
        0x4025FDBB, 0x402921FB, 0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C,
        0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB, 0x403AB41B, 0x403C463A,
        0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
        0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB,
        0x404858EB, 0x404921FB,
    };

    private static final double TWO24 = 1.67772160000000000000e+07;
    private static final double INVPIO2 = 6.36619772367581382433e-01;
    private static final double PIO2_1 = 1.57079632673412561417e+00;
    private static final double PIO2_1T = 6.07710050650619224932e-11;
    private static final double PIO2_2 = 6.07710050630396597660e-11;
    private static final double PIO2_2T = 2.02226624879595063154e-21;
    private static final double PIO2_3 = 2.02226624871116645580e-21;
    private static final double PIO2_3T = 8.47842766036889956997e-32;

    static int remPio2(double x, double[] y) {
        double z, w, t, r, fn;
        int i, j, n;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        if (ix <= 0x3fe921fb) {
            y[0] = x;
            y[1] = 0;
            return 0;
        }
        if (ix < 0x4002d97c) {
            if (hx > 0) {
                z = x - PIO2_1;
                if (ix != 0x3ff921fb) {
                    y[0] = z - PIO2_1T;
                    y[1] = (z - y[0]) - PIO2_1T;
                } else {
                    z -= PIO2_2;
                    y[0] = z - PIO2_2T;
                    y[1] = (z - y[0]) - PIO2_2T;
                }
                return 1;
            }
            z = x + PIO2_1;
            if (ix != 0x3ff921fb) {
                y[0] = z + PIO2_1T;
                y[1] = (z - y[0]) + PIO2_1T;
            } else {
                z += PIO2_2;
                y[0] = z + PIO2_2T;
                y[1] = (z - y[0]) + PIO2_2T;
            }
            return -1;
        }
        if (ix <= 0x413921fb) {
            t = Math.abs(x);
            n = (int) (t * INVPIO2 + 0.5);
            fn = (double) n;
            r = t - fn * PIO2_1;
            w = fn * PIO2_1T;
            if (n < 32 && ix != NPIO2_HW[n - 1]) {
                y[0] = r - w;
            } else {
                j = ix >> 20;
                y[0] = r - w;
                i = j - ((hi(y[0]) >> 20) & 0x7ff);
                if (i > 16) {
                    t = r;
                    w = fn * PIO2_2;
                    r = t - w;
                    w = fn * PIO2_2T - ((t - r) - w);
                    y[0] = r - w;
                    i = j - ((hi(y[0]) >> 20) & 0x7ff);
                    if (i > 49) {
                        t = r;
                        w = fn * PIO2_3;
                        r = t - w;
                        w = fn * PIO2_3T - ((t - r) - w);
                        y[0] = r - w;
                    }
                }
            }
            y[1] = (r - y[0]) - w;
            if (hx < 0) {
                y[0] = -y[0];
                y[1] = -y[1];
                return -n;
            }
            return n;
        }
        if (ix >= 0x7ff00000) {
            y[0] = y[1] = x - x;
            return 0;
        }
        double[] tx = new double[3];
        int e0 = (ix >> 20) - 1046;
        z = fromWords(ix - (e0 << 20), lo(x));
        for (i = 0; i < 2; i++) {
            tx[i] = (double) ((int) z);
            z = (z - tx[i]) * TWO24;
        }
        tx[2] = z;
        int nx = 3;
        while (tx[nx - 1] == 0.0) {
            nx--;
        }
        n = kernelRemPio2(tx, y, e0, nx);
        if (hx < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        }
        return n;
    }

    // ---------------------------------------------------------------- k_rem_pio2.c

    private static final double[] PIO2 = {
        1.57079625129699707031e+00,
        7.54978941586159635335e-08,
        5.39030252995776476554e-15,
        3.28200341580791294123e-22,
        1.27065575308067607349e-29,
        1.22933308981111328932e-36,
        2.73370053816464559624e-44,
        2.16741683877804819444e-51,
    };
    private static final double TWON24 = 5.96046447753906250000e-08;

    // prec 2 (double precision, two parts) is the only one rem_pio2 asks for
    static int kernelRemPio2(double[] x, double[] y, int e0, int nx) {
        int jz, jx, jv, jp, jk, carry, n, i, j, k, m, q0, ih;
        double z, fw;
        int[] iq = new int[20];
        double[] f = new double[20];
        double[] fq = new double[20];
        double[] q = new double[20];

        jk = 4;
        jp = jk;
        jx = nx - 1;
        jv = (e0 - 3) / 24;
        if (jv < 0) {
            jv = 0;
        }
        q0 = e0 - 24 * (jv + 1);

        j = jv - jx;
        m = jx + jk;
        for (i = 0; i <= m; i++, j++) {
            f[i] = j < 0 ? 0.0 : (double) TWO_OVER_PI[j];
        }
        for (i = 0; i <= jk; i++) {
            for (j = 0, fw = 0.0; j <= jx; j++) {
                fw += x[j] * f[jx + i - j];
            }
            q[i] = fw;
        }
        jz = jk;
        while (true) {
            for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
                fw = (double) ((int) (TWON24 * z));
                iq[i] = (int) (z - TWO24 * fw);
                z = q[j - 1] + fw;
            }
            z = scalbn(z, q0);
            z -= 8.0 * Math.floor(z * 0.125);
            n = (int) z;
            z -= (double) n;
            ih = 0;
            if (q0 > 0) {
                i = iq[jz - 1] >> (24 - q0);
                n += i;
                iq[jz - 1] -= i << (24 - q0);
                ih = iq[jz - 1] >> (23 - q0);
            } else if (q0 == 0) {
                ih = iq[jz - 1] >> 23;
            } else if (z >= 0.5) {
                ih = 2;
            }
            if (ih > 0) {
                n += 1;
                carry = 0;
                for (i = 0; i < jz; i++) {
                    j = iq[i];
                    if (carry == 0) {
                        if (j != 0) {
                            carry = 1;
                            iq[i] = 0x1000000 - j;
                        }
                    } else {
                        iq[i] = 0xffffff - j;
                    }
                }
                if (q0 > 0) {
                    switch (q0) {
                        case 1:
                            iq[jz - 1] &= 0x7fffff;
                            break;
                        case 2:
                            iq[jz - 1] &= 0x3fffff;
                            break;
                    }
                }
                if (ih == 2) {
                    z = 1.0 - z;
                    if (carry != 0) {
                        z -= scalbn(1.0, q0);
                    }
                }
            }
            if (z == 0.0) {
                j = 0;
                for (i = jz - 1; i >= jk; i--) {
                    j |= iq[i];
                }
                if (j == 0) {
                    for (k = 1; iq[jk - k] == 0; k++) {
                        // count the terms needed
                    }
                    for (i = jz + 1; i <= jz + k; i++) {
                        f[jx + i] = (double) TWO_OVER_PI[jv + i];
                        for (j = 0, fw = 0.0; j <= jx; j++) {
                            fw += x[j] * f[jx + i - j];
                        }
                        q[i] = fw;
                    }
                    jz += k;
                    continue;
                }
            }
            break;
        }
        if (z == 0.0) {
            jz -= 1;
            q0 -= 24;
            while (iq[jz] == 0) {
                jz--;
                q0 -= 24;
            }
        } else {
            z = scalbn(z, -q0);
            if (z >= TWO24) {
                fw = (double) ((int) (TWON24 * z));
                iq[jz] = (int) (z - TWO24 * fw);
                jz += 1;
                q0 += 24;
                iq[jz] = (int) fw;
            } else {
                iq[jz] = (int) z;
            }
        }
        fw = scalbn(1.0, q0);
        for (i = jz; i >= 0; i--) {
            q[i] = fw * (double) iq[i];
            fw *= TWON24;
        }
        for (i = jz; i >= 0; i--) {
            for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) {
                fw += PIO2[k] * q[i + k];
            }
            fq[jz - i] = fw;
        }
        fw = 0.0;
        for (i = jz; i >= 0; i--) {
            fw += fq[i];
        }
        y[0] = ih == 0 ? fw : -fw;
        fw = fq[0] - fw;
        for (i = 1; i <= jz; i++) {
            fw += fq[i];
        }
        y[1] = ih == 0 ? fw : -fw;
        return n & 7;
    }

    // ---------------------------------------------------------------- s_sin.c, s_cos.c, s_tan.c

    @Export(name = "warmc.fdlibm.sin")
    public static double sin(double x) {
        int ix = hi(x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) {
            return kernelSin(x, 0.0, 0);
        }
        if (ix >= 0x7ff00000) {
            return x - x;
        }
        double[] y = new double[2];
        int n = remPio2(x, y);
        switch (n & 3) {
            case 0:
                return kernelSin(y[0], y[1], 1);
            case 1:
                return kernelCos(y[0], y[1]);
            case 2:
                return -kernelSin(y[0], y[1], 1);
            default:
                return -kernelCos(y[0], y[1]);
        }
    }

    @Export(name = "warmc.fdlibm.cos")
    public static double cos(double x) {
        int ix = hi(x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) {
            return kernelCos(x, 0.0);
        }
        if (ix >= 0x7ff00000) {
            return x - x;
        }
        double[] y = new double[2];
        int n = remPio2(x, y);
        switch (n & 3) {
            case 0:
                return kernelCos(y[0], y[1]);
            case 1:
                return -kernelSin(y[0], y[1], 1);
            case 2:
                return -kernelCos(y[0], y[1]);
            default:
                return kernelSin(y[0], y[1], 1);
        }
    }

    @Export(name = "warmc.fdlibm.tan")
    public static double tan(double x) {
        int ix = hi(x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) {
            return kernelTan(x, 0.0, 1);
        }
        if (ix >= 0x7ff00000) {
            return x - x;
        }
        double[] y = new double[2];
        int n = remPio2(x, y);
        return kernelTan(y[0], y[1], 1 - ((n & 1) << 1));
    }

    // ---------------------------------------------------------------- e_asin.c, e_acos.c

    private static final double PIO2_HI = 1.57079632679489655800e+00;
    private static final double PIO2_LO = 6.12323399573676603587e-17;
    private static final double PIO4_HI = 7.85398163397448278999e-01;
    private static final double PS0 = 1.66666666666666657415e-01;
    private static final double PS1 = -3.25565818622400915405e-01;
    private static final double PS2 = 2.01212532134862925881e-01;
    private static final double PS3 = -4.00555345006794114027e-02;
    private static final double PS4 = 7.91534994289814532176e-04;
    private static final double PS5 = 3.47933107596021167570e-05;
    private static final double QS1 = -2.40339491173441421878e+00;
    private static final double QS2 = 2.02094576023350569471e+00;
    private static final double QS3 = -6.88283971605453293030e-01;
    private static final double QS4 = 7.70381505559019352791e-02;
    private static final double PI = 3.14159265358979311600e+00;

    @Export(name = "warmc.fdlibm.asin")
    public static double asin(double x) {
        double t = 0.0, w, p, q, c, r, s;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        if (ix >= 0x3ff00000) {
            if (((ix - 0x3ff00000) | lo(x)) == 0) {
                return x * PIO2_HI + x * PIO2_LO;
            }
            return (x - x) / (x - x);
        } else if (ix < 0x3fe00000) {
            if (ix < 0x3e400000) {
                if (HUGE + x > 1.0) {
                    return x;
                }
            } else {
                t = x * x;
            }
            p = t * (PS0 + t * (PS1 + t * (PS2 + t * (PS3 + t * (PS4 + t * PS5)))));
            q = 1.0 + t * (QS1 + t * (QS2 + t * (QS3 + t * QS4)));
            w = p / q;
            return x + x * w;
        }
        w = 1.0 - Math.abs(x);
        t = w * 0.5;
        p = t * (PS0 + t * (PS1 + t * (PS2 + t * (PS3 + t * (PS4 + t * PS5)))));
        q = 1.0 + t * (QS1 + t * (QS2 + t * (QS3 + t * QS4)));
        s = Math.sqrt(t);
        if (ix >= 0x3FEF3333) {
            w = p / q;
            t = PIO2_HI - (2.0 * (s + s * w) - PIO2_LO);
        } else {
            w = withLo(s, 0);
            c = (t - w * w) / (s + w);
            r = p / q;
            p = 2.0 * s * r - (PIO2_LO - 2.0 * c);
            q = PIO4_HI - 2.0 * w;
            t = PIO4_HI - (p - q);
        }
        return hx > 0 ? t : -t;
    }

    @Export(name = "warmc.fdlibm.acos")
    public static double acos(double x) {
        double z, p, q, r, w, s, c, df;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        if (ix >= 0x3ff00000) {
            if (((ix - 0x3ff00000) | lo(x)) == 0) {
                if (hx > 0) {
                    return 0.0;
                }
                return PI + 2.0 * PIO2_LO;
            }
            return (x - x) / (x - x);
        }
        if (ix < 0x3fe00000) {
            if (ix <= 0x3c600000) {
                return PIO2_HI + PIO2_LO;
            }
            z = x * x;
            p = z * (PS0 + z * (PS1 + z * (PS2 + z * (PS3 + z * (PS4 + z * PS5)))));
            q = 1.0 + z * (QS1 + z * (QS2 + z * (QS3 + z * QS4)));
            r = p / q;
            return PIO2_HI - (x - (PIO2_LO - x * r));
        } else if (hx < 0) {
            z = (1.0 + x) * 0.5;
            p = z * (PS0 + z * (PS1 + z * (PS2 + z * (PS3 + z * (PS4 + z * PS5)))));
            q = 1.0 + z * (QS1 + z * (QS2 + z * (QS3 + z * QS4)));
            s = Math.sqrt(z);
            r = p / q;
            w = r * s - PIO2_LO;
            return PI - 2.0 * (s + w);
        }
        z = (1.0 - x) * 0.5;
        s = Math.sqrt(z);
        df = withLo(s, 0);
        c = (z - df * df) / (s + df);
        p = z * (PS0 + z * (PS1 + z * (PS2 + z * (PS3 + z * (PS4 + z * PS5)))));
        q = 1.0 + z * (QS1 + z * (QS2 + z * (QS3 + z * QS4)));
        r = p / q;
        w = r * s + c;
        return 2.0 * (df + w);
    }

    // ---------------------------------------------------------------- s_atan.c, e_atan2.c

    private static final double[] ATANHI = {
        4.63647609000806093515e-01,
        7.85398163397448278999e-01,
        9.82793723247329054082e-01,
        1.57079632679489655800e+00,
    };
    private static final double[] ATANLO = {
        2.26987774529616870924e-17,
        3.06161699786838301793e-17,
        1.39033110312309984516e-17,
        6.12323399573676603587e-17,
    };
    private static final double[] AT = {
        3.33333333333329318027e-01,
        -1.99999999998764832476e-01,
        1.42857142725034663711e-01,
        -1.11111104054623557880e-01,
        9.09088713343650656196e-02,
        -7.69187620504482999495e-02,
        6.66107313738753120669e-02,
        -5.83357013379057348645e-02,
        4.97687799461593236017e-02,
        -3.65315727442169155270e-02,
        1.62858201153657823623e-02,
    };

    @Export(name = "warmc.fdlibm.atan")
    public static double atan(double x) {
        double w, s1, s2, z;
        int id;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        if (ix >= 0x44100000) {
            if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lo(x) != 0)) {
                return x + x;
            }
            if (hx > 0) {
                return ATANHI[3] + ATANLO[3];
            }
            return -ATANHI[3] - ATANLO[3];
        }
        if (ix < 0x3fdc0000) {
            if (ix < 0x3e200000) {
                if (HUGE + x > 1.0) {
                    return x;
                }
            }
            id = -1;
        } else {
            x = Math.abs(x);
            if (ix < 0x3ff30000) {
                if (ix < 0x3fe60000) {
                    id = 0;
                    x = (2.0 * x - 1.0) / (2.0 + x);
                } else {
                    id = 1;
                    x = (x - 1.0) / (x + 1.0);
                }
            } else {
                if (ix < 0x40038000) {
                    id = 2;
                    x = (x - 1.5) / (1.0 + 1.5 * x);
                } else {
                    id = 3;
                    x = -1.0 / x;
                }
            }
        }
        z = x * x;
        w = z * z;
        s1 = z * (AT[0] + w * (AT[2] + w * (AT[4] + w * (AT[6] + w * (AT[8] + w * AT[10])))));
        s2 = w * (AT[1] + w * (AT[3] + w * (AT[5] + w * (AT[7] + w * AT[9]))));
        if (id < 0) {
            return x - x * (s1 + s2);
        }
        z = ATANHI[id] - ((x * (s1 + s2) - ATANLO[id]) - x);
        return hx < 0 ? -z : z;
    }

    private static final double PI_O_4 = 7.8539816339744827900E-01;
    private static final double PI_O_2 = 1.5707963267948965580E+00;
    private static final double PI_LO = 1.2246467991473531772E-16;

    @Export(name = "warmc.fdlibm.atan2")
    public static double atan2(double y, double x) {
        double z;
        int hx = hi(x);
        int ix = hx & 0x7fffffff;
        int lx = lo(x);
        int hy = hi(y);
        int iy = hy & 0x7fffffff;
        int ly = lo(y);
        if ((ix | ((lx | -lx) >>> 31)) > 0x7ff00000 || (iy | ((ly | -ly) >>> 31)) > 0x7ff00000) {
            return x + y;
        }
        if (((hx - 0x3ff00000) | lx) == 0) {
            return atan(y);
        }
        int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
        if ((iy | ly) == 0) {
            switch (m) {
                case 0:
                case 1:
                    return y;
                case 2:
                    return PI + TINY;
                default:
                    return -PI - TINY;
            }
        }
        if ((ix | lx) == 0) {
            return hy < 0 ? -PI_O_2 - TINY : PI_O_2 + TINY;
        }
        if (ix == 0x7ff00000) {
            if (iy == 0x7ff00000) {
                switch (m) {
                    case 0:
                        return PI_O_4 + TINY;
                    case 1:
                        return -PI_O_4 - TINY;
                    case 2:
                        return 3.0 * PI_O_4 + TINY;
                    default:
                        return -3.0 * PI_O_4 - TINY;
                }
            } else {
                switch (m) {
                    case 0:
                        return 0.0;
                    case 1:
                        return -0.0;
                    case 2:
                        return PI + TINY;
                    default:
                        return -PI - TINY;
                }
            }
        }
        if (iy == 0x7ff00000) {
            return hy < 0 ? -PI_O_2 - TINY : PI_O_2 + TINY;
        }
        int k = (iy - ix) >> 20;
        if (k > 60) {
            z = PI_O_2 + 0.5 * PI_LO;
        } else if (hx < 0 && k < -60) {
            z = 0.0;
        } else {
            z = atan(Math.abs(y / x));
        }
        switch (m) {
            case 0:
                return z;
            case 1:
                return withHi(z, hi(z) ^ 0x80000000);
            case 2:
                return PI - (z - PI_LO);
            default:
                return (z - PI_LO) - PI;
        }
    }

    // ---------------------------------------------------------------- e_exp.c

    private static final double[] HALF = {0.5, -0.5};
    private static final double TWOM1000 = 9.33263618503218878990e-302;
    private static final double O_THRESHOLD = 7.09782712893383973096e+02;
    private static final double U_THRESHOLD = -7.45133219101941108420e+02;
    private static final double[] LN2HI = {6.93147180369123816490e-01, -6.93147180369123816490e-01};
    private static final double[] LN2LO = {1.90821492927058770002e-10, -1.90821492927058770002e-10};
    private static final double INVLN2 = 1.44269504088896338700e+00;
    private static final double P1 = 1.66666666666666019037e-01;
    private static final double P2 = -2.77777777770155933842e-03;
    private static final double P3 = 6.61375632143793436117e-05;
    private static final double P4 = -1.65339022054652515390e-06;
    private static final double P5 = 4.13813679705723846039e-08;

    @Export(name = "warmc.fdlibm.exp")
    public static double exp(double x) {
        double y, hi = 0.0, lo = 0.0, c, t;
        int k = 0;
        int hx = hi(x);
        int xsb = (hx >>> 31) & 1;
        hx &= 0x7fffffff;
        if (hx >= 0x40862E42) {
            if (hx >= 0x7ff00000) {
                if (((hx & 0xfffff) | lo(x)) != 0) {
                    return x + x;
                }
                return xsb == 0 ? x : 0.0;
            }
            if (x > O_THRESHOLD) {
                return HUGE * HUGE;
            }
            if (x < U_THRESHOLD) {
                return TWOM1000 * TWOM1000;
            }
        }
        if (hx > 0x3fd62e42) {
            if (hx < 0x3FF0A2B2) {
                hi = x - LN2HI[xsb];
                lo = LN2LO[xsb];
                k = 1 - xsb - xsb;
            } else {
                k = (int) (INVLN2 * x + HALF[xsb]);
                t = k;
                hi = x - t * LN2HI[0];
                lo = t * LN2LO[0];
            }
            x = hi - lo;
        } else if (hx < 0x3e300000) {
            if (HUGE + x > 1.0) {
                return 1.0 + x;
            }
        } else {
            k = 0;
        }
        t = x * x;
        c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
        if (k == 0) {
            return 1.0 - ((x * c) / (c - 2.0) - x);
        }
        y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi);
        if (k >= -1021) {
            return withHi(y, hi(y) + (k << 20));
        }
        y = withHi(y, hi(y) + ((k + 1000) << 20));
        return y * TWOM1000;
    }

    // ---------------------------------------------------------------- e_log.c

    private static final double LN2_HI = 6.93147180369123816490e-01;
    private static final double LN2_LO = 1.90821492927058770002e-10;
    private static final double LG1 = 6.666666666666735130e-01;
    private static final double LG2 = 3.999999999940941908e-01;
    private static final double LG3 = 2.857142874366239149e-01;
    private static final double LG4 = 2.222219843214978396e-01;
    private static final double LG5 = 1.818357216161805012e-01;
    private static final double LG6 = 1.531383769920937332e-01;
    private static final double LG7 = 1.479819860511658591e-01;

    @Export(name = "warmc.fdlibm.log")
    public static double log(double x) {
        double hfsq, f, s, z, r, w, t1, t2, dk;
        int i, j;
        int hx = hi(x);
        int lx = lo(x);
        int k = 0;
        if (hx < 0x00100000) {
            if (((hx & 0x7fffffff) | lx) == 0) {
                return -TWO54 / 0.0;
            }
            if (hx < 0) {
                return (x - x) / 0.0;
            }
            k -= 54;
            x *= TWO54;
            hx = hi(x);
        }
        if (hx >= 0x7ff00000) {
            return x + x;
        }
        k += (hx >> 20) - 1023;
        hx &= 0x000fffff;
        i = (hx + 0x95f64) & 0x100000;
        x = withHi(x, hx | (i ^ 0x3ff00000));
        k += i >> 20;
        f = x - 1.0;
        if ((0x000fffff & (2 + hx)) < 3) {
            if (f == 0.0) {
                if (k == 0) {
                    return 0.0;
                }
                dk = (double) k;
                return dk * LN2_HI + dk * LN2_LO;
            }
            r = f * f * (0.5 - 0.33333333333333333 * f);
            if (k == 0) {
                return f - r;
            }
            dk = (double) k;
            return dk * LN2_HI - ((r - dk * LN2_LO) - f);
        }
        s = f / (2.0 + f);
        dk = (double) k;
        z = s * s;
        i = hx - 0x6147a;
        w = z * z;
        j = 0x6b851 - hx;
        t1 = w * (LG2 + w * (LG4 + w * LG6));
        t2 = z * (LG1 + w * (LG3 + w * (LG5 + w * LG7)));
        i |= j;
        r = t2 + t1;
        if (i > 0) {
            hfsq = 0.5 * f * f;
            if (k == 0) {
                return f - (hfsq - s * (hfsq + r));
            }
            return dk * LN2_HI - ((hfsq - (s * (hfsq + r) + dk * LN2_LO)) - f);
        }
        if (k == 0) {
            return f - s * (f - r);
        }
        return dk * LN2_HI - ((s * (f - r) - dk * LN2_LO) - f);
    }

    // ---------------------------------------------------------------- e_pow.c

    private static final double[] BP = {1.0, 1.5};
    private static final double[] DP_H = {0.0, 5.84962487220764160156e-01};
    private static final double[] DP_L = {0.0, 1.35003920212974897128e-08};
    private static final double TWO53 = 9007199254740992.0;
    private static final double L1 = 5.99999999999994648725e-01;
    private static final double L2 = 4.28571428578550184252e-01;
    private static final double L3 = 3.33333329818377432918e-01;
    private static final double L4 = 2.72728123808534006489e-01;
    private static final double L5 = 2.30660745775561754067e-01;
    private static final double L6 = 2.06975017800338417784e-01;
    private static final double LG2_FULL = 6.93147180559945286227e-01;
    private static final double LG2_H = 6.93147182464599609375e-01;
    private static final double LG2_L = -1.90465429995776804525e-09;
    private static final double OVT = 8.0085662595372944372e-17;
    private static final double CP = 9.61796693925975554329e-01;
    private static final double CP_H = 9.61796700954437255859e-01;
    private static final double CP_L = -7.02846165095275826516e-09;
    private static final double IVLN2 = 1.44269504088896338700e+00;
    private static final double IVLN2_H = 1.44269502162933349609e+00;
    private static final double IVLN2_L = 1.92596299112661746887e-08;

    @Export(name = "warmc.fdlibm.pow")
    public static double pow(double x, double y) {
        double z, ax, zH, zL, pH, pL;
        double y1, t1, t2, r, s, t, u, v, w;
        int i, j, k, yisint, n;
        int hx = hi(x);
        int lx = lo(x);
        int hy = hi(y);
        int ly = lo(y);
        int ix = hx & 0x7fffffff;
        int iy = hy & 0x7fffffff;

        if ((iy | ly) == 0) {
            return 1.0;
        }
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0)
                || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0)) {
            return x + y;
        }
        yisint = 0;
        if (hx < 0) {
            if (iy >= 0x43400000) {
                yisint = 2;
            } else if (iy >= 0x3ff00000) {
                k = (iy >> 20) - 0x3ff;
                if (k > 20) {
                    j = ly >>> (52 - k);
                    if ((j << (52 - k)) == ly) {
                        yisint = 2 - (j & 1);
                    }
                } else if (ly == 0) {
                    j = iy >> (20 - k);
                    if ((j << (20 - k)) == iy) {
                        yisint = 2 - (j & 1);
                    }
                }
            }
        }
        if (ly == 0) {
            if (iy == 0x7ff00000) {
                if (((ix - 0x3ff00000) | lx) == 0) {
                    return y - y;
                } else if (ix >= 0x3ff00000) {
                    return hy >= 0 ? y : 0.0;
                } else {
                    return hy < 0 ? -y : 0.0;
                }
            }
            if (iy == 0x3ff00000) {
                if (hy < 0) {
                    return 1.0 / x;
                }
                return x;
            }
            if (hy == 0x40000000) {
                return x * x;
            }
            if (hy == 0x3fe00000) {
                if (hx >= 0) {
                    return Math.sqrt(x);
                }
            }
        }
        ax = Math.abs(x);
        if (lx == 0) {
            if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
                z = ax;
                if (hy < 0) {
                    z = 1.0 / z;
                }
                if (hx < 0) {
                    if (((ix - 0x3ff00000) | yisint) == 0) {
                        z = (z - z) / (z - z);
                    } else if (yisint == 1) {
                        z = -z;
                    }
                }
                return z;
            }
        }
        n = (hx >> 31) + 1;
        if ((n | yisint) == 0) {
            return (x - x) / (x - x);
        }
        s = 1.0;
        if ((n | (yisint - 1)) == 0) {
            s = -1.0;
        }
        if (iy > 0x41e00000) {
            if (iy > 0x43f00000) {
                if (ix <= 0x3fefffff) {
                    return hy < 0 ? HUGE * HUGE : TINY * TINY;
                }
                if (ix >= 0x3ff00000) {
                    return hy > 0 ? HUGE * HUGE : TINY * TINY;
                }
            }
            if (ix < 0x3fefffff) {
                return hy < 0 ? s * HUGE * HUGE : s * TINY * TINY;
            }
            if (ix > 0x3ff00000) {
                return hy > 0 ? s * HUGE * HUGE : s * TINY * TINY;
            }
            t = ax - 1.0;
            w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
            u = IVLN2_H * t;
            v = t * IVLN2_L - w * IVLN2;
            t1 = u + v;
            t1 = withLo(t1, 0);
            t2 = v - (t1 - u);
        } else {
            double ss, s2, sH, sL, tH, tL;
            n = 0;
            if (ix < 0x00100000) {
                ax *= TWO53;
                n -= 53;
                ix = hi(ax);
            }
            n += (ix >> 20) - 0x3ff;
            j = ix & 0x000fffff;
            ix = j | 0x3ff00000;
            if (j <= 0x3988E) {
                k = 0;
            } else if (j < 0xBB67A) {
                k = 1;
            } else {
                k = 0;
                n += 1;
                ix -= 0x00100000;
            }
            ax = withHi(ax, ix);
            u = ax - BP[k];
            v = 1.0 / (ax + BP[k]);
            ss = u * v;
            sH = withLo(ss, 0);
            tH = fromWords(((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18), 0);
            tL = ax - (tH - BP[k]);
            sL = v * ((u - sH * tH) - sH * tL);
            s2 = ss * ss;
            r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
            r += sL * (sH + ss);
            s2 = sH * sH;
            tH = 3.0 + s2 + r;
            tH = withLo(tH, 0);
            tL = r - ((tH - 3.0) - s2);
            u = sH * tH;
            v = sL * tH + tL * ss;
            pH = u + v;
            pH = withLo(pH, 0);
            pL = v - (pH - u);
            zH = CP_H * pH;
            zL = CP_L * pH + pL * CP + DP_L[k];
            t = (double) n;
            t1 = ((zH + zL) + DP_H[k]) + t;
            t1 = withLo(t1, 0);
            t2 = zL - (((t1 - t) - DP_H[k]) - zH);
        }
        y1 = withLo(y, 0);
        pL = (y - y1) * t1 + y * t2;
        pH = y1 * t1;
        z = pL + pH;
        j = hi(z);
        i = lo(z);
        if (j >= 0x40900000) {
            if (((j - 0x40900000) | i) != 0) {
                return s * HUGE * HUGE;
            }
            if (pL + OVT > z - pH) {
                return s * HUGE * HUGE;
            }
        } else if ((j & 0x7fffffff) >= 0x4090cc00) {
            if (((j - 0xc090cc00) | i) != 0) {
                return s * TINY * TINY;
            }
            if (pL <= z - pH) {
                return s * TINY * TINY;
            }
        }
        i = j & 0x7fffffff;
        k = (i >> 20) - 0x3ff;
        n = 0;
        if (i > 0x3fe00000) {
            n = j + (0x00100000 >> (k + 1));
            k = ((n & 0x7fffffff) >> 20) - 0x3ff;
            t = fromWords(n & ~(0x000fffff >> k), 0);
            n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
            if (j < 0) {
                n = -n;
            }
            pH -= t;
        }
        t = pL + pH;
        t = withLo(t, 0);
        u = t * LG2_H;
        v = (pL - (t - pH)) * LG2_FULL + t * LG2_L;
        z = u + v;
        w = v - (z - u);
        t = z * z;
        t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
        r = (z * t1) / (t1 - 2.0) - (w + z * w);
        z = 1.0 - (r - z);
        j = hi(z);
        j += n << 20;
        if ((j >> 20) <= 0) {
            z = scalbn(z, n);
        } else {
            z = withHi(z, hi(z) + (n << 20));
        }
        return s * z;
    }
}
