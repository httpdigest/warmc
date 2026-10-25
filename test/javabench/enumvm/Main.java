// A small register machine whose opcodes are an enum, dispatched by switch.
//
// A switch on an enum compiles to a lookup of the constant's ordinal in a
// table javac generates, then a tableswitch, which TeaVM makes a br_table.
// The program is a counted loop folding the counter into an accumulator.
public class Main {
    enum Op { LOADI, ADD, MUL, XOR, MOV, DEC, JNZ, HALT }

    static long run(Op[] op, int[] a, int[] b, int[] c, long n) {
        long[] r = new long[8];
        r[0] = n;
        long steps = 0;
        int pc = 0;
        while (true) {
            steps++;
            switch (op[pc]) {
                case LOADI -> r[a[pc]] = b[pc];
                case ADD -> r[a[pc]] = r[b[pc]] + r[c[pc]];
                case MUL -> r[a[pc]] = r[b[pc]] * r[c[pc]];
                case XOR -> r[a[pc]] = r[b[pc]] ^ r[c[pc]];
                case MOV -> r[a[pc]] = r[b[pc]];
                case DEC -> r[a[pc]]--;
                case JNZ -> {
                    if (r[a[pc]] != 0) {
                        pc = b[pc];
                        continue;
                    }
                }
                case HALT -> {
                    return r[1] * 31 + steps;
                }
            }
            pc++;
        }
    }

    public static void main(String[] args) {
        // r1 = 0; r3 = 3; r4 = 0x55
        // loop: r1 = r1 * r3; r2 = r0 ^ r4; r1 = r1 + r2; r0--; jnz r0 loop
        Op[] op = {Op.LOADI, Op.LOADI, Op.LOADI, Op.MUL, Op.XOR, Op.ADD, Op.DEC, Op.JNZ, Op.MOV, Op.HALT};
        int[] a = {1, 3, 4, 1, 2, 1, 0, 0, 5, 0};
        int[] b = {0, 3, 0x55, 1, 0, 1, 0, 3, 1, 0};
        int[] c = {0, 0, 0, 3, 4, 2, 0, 0, 0, 0};
        long acc = 0;
        for (int i = 0; i < 200; i++) {
            acc += run(op, a, b, c, 25_000 + i);
        }
        System.out.println("acc = " + acc);
    }
}
