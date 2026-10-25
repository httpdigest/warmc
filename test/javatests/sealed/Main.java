// Sealed hierarchies and pattern matching: sealed interfaces with records,
// final and non-sealed classes, exhaustive switches without default, record
// deconstruction nested two levels, guards, null in a switch, type patterns
// in instanceof with &&, and a small expression simplifier and evaluator.
import java.util.List;

public class Main {
    sealed interface Expr permits Num, Neg, Bin, Let {
    }

    record Num(int v) implements Expr {
    }

    record Neg(Expr e) implements Expr {
    }

    enum OpKind { ADD, MUL, SUB }

    record Bin(OpKind op, Expr l, Expr r) implements Expr {
    }

    record Let(String name, Expr value, Expr body) implements Expr {
    }

    sealed interface Vehicle permits Car, Truck, Bike {
    }

    static final class Car implements Vehicle {
        final int seats;

        Car(int seats) {
            this.seats = seats;
        }
    }

    static final class Truck implements Vehicle {
        final double load;

        Truck(double load) {
            this.load = load;
        }
    }

    static non-sealed class Bike implements Vehicle {
    }

    static class EBike extends Bike {
    }

    static int eval(Expr e) {
        return switch (e) {
            case Num n -> n.v();
            case Neg(Num(int v)) -> -v;
            case Neg(var inner) -> -eval(inner);
            case Bin(OpKind op, Expr l, Expr r) when op == OpKind.ADD -> eval(l) + eval(r);
            case Bin(OpKind op, Expr l, Expr r) when op == OpKind.SUB -> eval(l) - eval(r);
            case Bin b -> eval(b.l()) * eval(b.r());
            case Let let -> eval(subst(let.body(), let.name(), let.value()));
        };
    }

    static Expr subst(Expr e, String name, Expr value) {
        return switch (e) {
            case Num n -> n;
            case Neg(var x) -> new Neg(subst(x, name, value));
            case Bin(var op, var l, var r) -> new Bin(op, subst(l, name, value), subst(r, name, value));
            case Let(var n, var v, var b) -> n.equals(name) ? new Let(n, subst(v, name, value), b)
                    : new Let(n, subst(v, name, value), subst(b, name, value));
        };
    }

    static Expr simplify(Expr e) {
        return switch (e) {
            case Bin(OpKind op, Num(int a), Num(int b)) -> new Num(eval(e));
            case Bin(OpKind op, Expr l, Num(int z)) when z == 0 && op != OpKind.MUL -> simplify(l);
            case Bin(OpKind op, Expr l, Num(int one)) when one == 1 && op == OpKind.MUL -> simplify(l);
            case Bin(OpKind op, Expr l, Expr r) -> {
                Expr sl = simplify(l), sr = simplify(r);
                yield sl.equals(l) && sr.equals(r) ? e : simplify(new Bin(op, sl, sr));
            }
            case Neg(Neg(var x)) -> simplify(x);
            default -> e;
        };
    }

    static String show(Expr e) {
        return switch (e) {
            case Num(var v) -> Integer.toString(v);
            case Neg(var x) -> "-" + show(x);
            case Bin(var op, var l, var r) -> "(" + show(l) + switch (op) {
                case ADD -> "+";
                case SUB -> "-";
                case MUL -> "*";
            } + show(r) + ")";
            case Let(var n, var v, var b) -> "let " + n + "=" + show(v) + " in " + show(b);
        };
    }

    static String describe(Vehicle v) {
        return switch (v) {
            case Car c when c.seats > 4 -> "van with " + c.seats;
            case Car c -> "car with " + c.seats;
            case Truck t -> "truck carrying " + t.load;
            case EBike eb -> "e-bike";
            case Bike b -> "bike";
        };
    }

    static String nullable(Object o) {
        return switch (o) {
            case null -> "null!";
            case String s when s.isEmpty() -> "empty string";
            case String s -> "string " + s;
            case int[] a -> "int array of " + a.length;
            default -> o.getClass().getSimpleName();
        };
    }

    public static void main(String[] args) {
        Expr e = new Bin(OpKind.ADD, new Num(3), new Bin(OpKind.MUL, new Neg(new Num(4)), new Bin(OpKind.SUB, new Num(10), new Num(7))));
        System.out.println(show(e) + " = " + eval(e));
        Expr withZero = new Bin(OpKind.ADD, new Bin(OpKind.MUL, new Neg(new Neg(new Num(5))), new Num(1)), new Num(0));
        System.out.println(show(withZero) + " => " + show(simplify(withZero)));
        Expr folded = new Bin(OpKind.MUL, new Bin(OpKind.ADD, new Num(2), new Num(3)), new Bin(OpKind.SUB, new Num(9), new Num(4)));
        System.out.println(show(folded) + " => " + show(simplify(folded)));
        Expr let = new Let("x", new Num(6), new Bin(OpKind.MUL, new Num(7), new Num(1)));
        System.out.println(show(let) + " = " + eval(let) + " " + e.equals(simplify(e)) + " " + new Num(3).equals(new Num(3)));
        for (Vehicle v : List.of(new Car(2), new Car(7), new Truck(3.5), new Bike(), new EBike())) {
            System.out.println(describe(v));
        }
        for (Object o : new Object[] {null, "", "abc", new int[3], 4.5, List.of()}) {
            System.out.print(nullable(o) + "; ");
        }
        System.out.println();
        Object obj = new Bin(OpKind.SUB, new Num(1), new Num(2));
        if (obj instanceof Bin b && b.op() == OpKind.SUB && b.l() instanceof Num(int one) && one == 1) {
            System.out.println("matched sub from one: " + eval(b));
        }
    }
}
