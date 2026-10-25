// Exceptions: a custom hierarchy with messages and causes, catch clauses
// tried in order, multi-catch, finally blocks and the order they run in when
// control leaves by return, break, continue or a throw, a finally that
// overrides the result, rethrow and wrapping, try-with-resources closing in
// reverse with suppressed exceptions, exceptions through lambdas and deep
// recursion, from constructors and static initialisers, and the ones the
// runtime throws: NullPointerException, ArrayIndexOutOfBounds,
// StringIndexOutOfBounds, ClassCast and NumberFormat.
//
// Built with TeaVM's --strict (test/gobench.py's TEAVM_STRICT), which is what
// makes a null dereference and an index out of bounds Java exceptions rather
// than wasm traps.  Integer division by zero, a negative array size and stack
// overflow stay traps under TeaVM's wasm-gc target either way, so none is here.
import java.util.ArrayList;
import java.util.List;
import java.util.function.IntUnaryOperator;

public class Main {
    static class AppException extends Exception {
        AppException(String msg) {
            super(msg);
        }

        AppException(String msg, Throwable cause) {
            super(msg, cause);
        }
    }

    static class ValidationException extends AppException {
        final String field;

        ValidationException(String field, String msg) {
            super("invalid " + field + ": " + msg);
            this.field = field;
        }
    }

    static class NotFound extends AppException {
        NotFound(String key) {
            super("not found: " + key);
        }
    }

    static final List<String> trace = new ArrayList<>();

    static void flush(String label) {
        System.out.println("  " + label + ": " + String.join(" ", trace));
        trace.clear();
    }

    static String validate(int age, String name) throws AppException {
        if (name.isBlank()) {
            throw new ValidationException("name", "blank");
        }
        if (age < 0 || age > 150) {
            throw new ValidationException("age", age + " out of range");
        }
        if (name.equals("ghost")) {
            throw new NotFound(name);
        }
        if (name.equals("boom")) {
            throw new IllegalStateException("boom state");
        }
        return name + ":" + age;
    }

    static int finallyOverrides() {
        try {
            trace.add("try");
            return 1;
        } finally {
            trace.add("finally");
        }
    }

    @SuppressWarnings("finally")
    static int finallyWins() {
        try {
            throw new RuntimeException("lost");
        } finally {
            return 42;
        }
    }

    static int loops() {
        int r = 0;
        for (int i = 0; i < 5; i++) {
            try {
                if (i == 1) {
                    continue;
                }
                if (i == 3) {
                    break;
                }
                r += i;
            } finally {
                trace.add("f" + i);
            }
        }
        return r;
    }

    static class Res implements AutoCloseable {
        final String name;
        final boolean failClose;

        Res(String name, boolean failClose) {
            this.name = name;
            this.failClose = failClose;
            trace.add("open " + name);
        }

        public void close() {
            trace.add("close " + name);
            if (failClose) {
                throw new IllegalStateException("close failed: " + name);
            }
        }
    }

    static int depth(int n) {
        if (n == 0) {
            throw new UnsupportedOperationException("bottom");
        }
        try {
            return depth(n - 1) + 1;
        } finally {
            if (n % 100 == 0) {
                trace.add("unwound" + n);
            }
        }
    }

    static class Fragile {
        Fragile(int v) {
            if (v < 0) {
                throw new IllegalArgumentException("negative " + v);
            }
        }
    }

    static class BadInit {
        static final int VALUE;

        static {
            int[] a = new int[2];
            VALUE = a.length;
        }
    }

    public static void main(String[] args) {
        System.out.println("== custom hierarchy ==");
        String[][] inputs = {{"30", "ann"}, {"30", " "}, {"200", "bob"}, {"5", "ghost"}, {"7", "boom"}};
        for (String[] in : inputs) {
            try {
                System.out.println("ok " + validate(Integer.parseInt(in[0]), in[1]));
            } catch (ValidationException e) {
                System.out.println("validation[" + e.field + "] " + e.getMessage());
            } catch (AppException e) {
                System.out.println("app " + e.getMessage() + " " + e.getClass().getSimpleName());
            } catch (RuntimeException e) {
                System.out.println("runtime " + e.getMessage());
            }
        }
        try {
            try {
                validate(-1, "x");
            } catch (AppException e) {
                throw new AppException("wrapped", e);
            }
        } catch (AppException e) {
            System.out.println(e.getMessage() + " <- " + e.getCause().getMessage() + " " + (e.getCause() instanceof ValidationException));
        }
        try {
            Object o = List.of("a").get(0);
            Integer bad = (Integer) o;
            System.out.println(bad);
        } catch (ClassCastException | IndexOutOfBoundsException e) {
            System.out.println("multi-catch " + e.getClass().getSimpleName());
        }

        System.out.println("== finally ==");
        System.out.println(finallyOverrides() + " " + finallyWins());
        flush("return");
        System.out.println(loops());
        flush("loops");
        try {
            try {
                trace.add("inner");
                throw new IllegalStateException("x");
            } finally {
                trace.add("inner-finally");
            }
        } catch (IllegalStateException e) {
            trace.add("caught");
        } finally {
            trace.add("outer-finally");
        }
        flush("nested");

        System.out.println("== try-with-resources ==");
        try (Res a = new Res("a", false); Res b = new Res("b", true); Res c = new Res("c", true)) {
            trace.add("body");
            throw new RuntimeException("body failed");
        } catch (RuntimeException e) {
            trace.add("caught " + e.getMessage() + " suppressed " + e.getSuppressed().length);
            for (Throwable s : e.getSuppressed()) {
                trace.add("[" + s.getMessage() + "]");
            }
        }
        flush("resources");

        System.out.println("== through lambdas and frames ==");
        IntUnaryOperator risky = x -> {
            if (x > 3) {
                throw new ArithmeticException("too big " + x);
            }
            return x * 2;
        };
        int total = 0;
        for (int i = 0; i < 6; i++) {
            try {
                total += risky.applyAsInt(i);
            } catch (ArithmeticException e) {
                trace.add(e.getMessage());
            }
        }
        System.out.println(total);
        flush("lambda");
        try {
            depth(500);
        } catch (UnsupportedOperationException e) {
            trace.add(e.getMessage());
        }
        flush("recursion");
        try {
            new Fragile(-3);
        } catch (IllegalArgumentException e) {
            System.out.println("constructor: " + e.getMessage());
        }
        System.out.println("static init " + BadInit.VALUE);

        System.out.println("== runtime exceptions ==");
        int[] arr = new int[3];
        String str = null;
        Object[] cases = new Object[6];
        for (int k = 0; k < cases.length; k++) {
            try {
                switch (k) {
                    case 0 -> System.out.println(arr[k + 5]);
                    case 1 -> System.out.println(str.length());
                    case 2 -> System.out.println("abc".charAt(10));
                    case 3 -> System.out.println(Integer.parseInt("12x"));
                    case 4 -> System.out.println((String) (Object) Integer.valueOf(3));
                    default -> System.out.println(List.of(1, 2).get(5));
                }
            } catch (ArrayIndexOutOfBoundsException e) {
                System.out.println("ArrayIndexOutOfBoundsException");
            } catch (NullPointerException e) {
                System.out.println("NullPointerException");
            } catch (StringIndexOutOfBoundsException e) {
                System.out.println("StringIndexOutOfBoundsException");
            } catch (NumberFormatException e) {
                System.out.println("NumberFormatException " + e.getMessage());
            } catch (ClassCastException e) {
                System.out.println("ClassCastException");
            } catch (IndexOutOfBoundsException e) {
                System.out.println("IndexOutOfBoundsException");
            }
        }
        Throwable t = new Error("an error", new RuntimeException("its cause"));
        System.out.println(t + " / " + t.getCause() + " / " + (t instanceof Exception));
    }
}
