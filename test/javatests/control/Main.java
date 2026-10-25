// Control flow: nested loops with labelled break and continue, do-while,
// every switch form (old fall-through on ints, chars and strings, arrow
// cases, switch expressions with yield, enums), conditional operators, short
// circuits with side effects, and loops that exit by return from inside.
public class Main {
    enum Day { MON, TUE, WED, THU, FRI, SAT, SUN }

    static int calls;

    static boolean side(boolean v) {
        calls++;
        return v;
    }

    static int classic(int k) {
        int r = 0;
        switch (k) {
            case 0:
                r += 1;
            case 1:
                r += 10;
                break;
            case 5:
            case 6:
                r += 100;
                break;
            case -1000000:
                r = -1;
                break;
            default:
                r = 1000;
        }
        return r;
    }

    static String kind(String s) {
        switch (s) {
            case "apple":
            case "pear":
                return "fruit";
            case "carrot":
                return "vegetable";
            default:
                return s.isEmpty() ? "empty" : "unknown";
        }
    }

    static int weight(Day d) {
        return switch (d) {
            case SAT, SUN -> 0;
            case FRI -> {
                int w = d.ordinal();
                yield w * 2;
            }
            default -> 1;
        };
    }

    static int sparse(int k) {
        return switch (k) {
            case 1 -> 11;
            case 100 -> 22;
            case 10000 -> 33;
            case -7 -> 44;
            case Integer.MAX_VALUE -> 55;
            default -> 0;
        };
    }

    static int find(int[][] m, int want) {
        for (int i = 0; i < m.length; i++) {
            for (int j = 0; j < m[i].length; j++) {
                if (m[i][j] == want) {
                    return i * 100 + j;
                }
            }
        }
        return -1;
    }

    public static void main(String[] args) {
        System.out.println("== labelled loops ==");
        StringBuilder sb = new StringBuilder();
        outer:
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 6; j++) {
                if (j == i) {
                    continue outer;
                }
                if (i * j > 12) {
                    break outer;
                }
                sb.append(i).append(j).append(' ');
            }
        }
        System.out.println(sb.toString().trim());
        int n = 0, steps = 0;
        do {
            n += 3;
            steps++;
        } while (n % 7 != 0);
        System.out.println("do-while " + n + " " + steps);
        int collatz = 27, len = 0;
        while (collatz != 1) {
            collatz = (collatz & 1) == 0 ? collatz / 2 : 3 * collatz + 1;
            len++;
        }
        System.out.println("collatz 27 " + len);

        System.out.println("== switch ==");
        for (int k : new int[] {0, 1, 2, 5, 6, -1000000, 99}) {
            System.out.print(classic(k) + " ");
        }
        System.out.println();
        for (String s : new String[] {"apple", "pear", "carrot", "", "rock"}) {
            System.out.print(kind(s) + " ");
        }
        System.out.println();
        for (Day d : Day.values()) {
            System.out.print(weight(d));
        }
        System.out.println();
        for (int k : new int[] {1, 100, 10000, -7, Integer.MAX_VALUE, 3}) {
            System.out.print(sparse(k) + " ");
        }
        System.out.println();
        char grade = 'B';
        switch (grade) {
            case 'A' -> System.out.println("excellent");
            case 'B', 'C' -> System.out.println("good");
            default -> System.out.println("other");
        }

        System.out.println("== conditionals ==");
        calls = 0;
        boolean r1 = side(false) && side(true);
        boolean r2 = side(true) || side(false);
        boolean r3 = side(false) & side(true);
        boolean r4 = side(true) | side(false);
        boolean r5 = side(true) ^ side(true);
        System.out.println(r1 + " " + r2 + " " + r3 + " " + r4 + " " + r5 + " calls=" + calls);
        int x = 5;
        String size = x > 10 ? "big" : x > 3 ? "medium" : "small";
        System.out.println(size + " " + (x == 5 ? 1 : 2.5) + " " + (char) (x > 2 ? 'y' : 'n'));
        int[][] m = {{1, 2}, {3, 4, 5}, {6}};
        System.out.println(find(m, 5) + " " + find(m, 6) + " " + find(m, 9));
        long acc = 0;
        for (int i = 0, j = 10; i < j; i += 2, j--) {
            acc = acc * 10 + i + j;
        }
        System.out.println("comma loop " + acc);
        int idx = 0;
        int[] arr = new int[5];
        arr[idx++] = idx;
        arr[idx] = idx++ + idx;
        System.out.println(java.util.Arrays.toString(arr) + " " + idx);
    }
}
