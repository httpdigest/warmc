// An expression tree of records under a sealed interface, evaluated by a
// pattern switch.
//
// javac compiles the switch to an invokedynamic of SwitchBootstraps.typeSwitch
// and the record patterns to accessor calls, which TeaVM lowers to a chain of
// type tests; the tree is rebuilt and evaluated many times.
public class Main {
    sealed interface Expr permits Num, Add, Mul, Neg, Var {
    }

    record Num(long v) implements Expr {
    }

    record Add(Expr l, Expr r) implements Expr {
    }

    record Mul(Expr l, Expr r) implements Expr {
    }

    record Neg(Expr e) implements Expr {
    }

    record Var(int slot) implements Expr {
    }

    static long eval(Expr e, long[] env) {
        return switch (e) {
            case Num n -> n.v();
            case Add(Expr l, Expr r) -> eval(l, env) + eval(r, env);
            case Mul(Expr l, Expr r) -> eval(l, env) * eval(r, env) % 1_000_003;
            case Neg(Expr x) -> -eval(x, env);
            case Var v -> env[v.slot()];
        };
    }

    static Expr build(int depth, int seed) {
        seed &= 0x7fffffff;
        if (depth == 0) {
            return (seed & 1) == 0 ? new Num(seed % 17) : new Var(seed % 4);
        }
        return switch (seed % 3) {
            case 0 -> new Add(build(depth - 1, seed * 7 + 1), build(depth - 1, seed * 5 + 2));
            case 1 -> new Mul(build(depth - 1, seed * 3 + 1), build(depth - 1, seed + 5));
            default -> new Neg(build(depth - 1, seed * 11 + 3));
        };
    }

    public static void main(String[] args) {
        long[] env = {3, 5, 7, 11};
        long acc = 0;
        for (int r = 0; r < 250; r++) {
            Expr e = build(15, r);
            env[r & 3] = r;
            acc = acc * 31 + eval(e, env);
        }
        System.out.println("acc = " + acc);
    }
}
