// Enums: values, valueOf and its failure, ordinal and compareTo, fields and
// constructors, constant-specific bodies overriding an abstract method,
// enums implementing interfaces, switch over enums, EnumSet ranges and
// EnumMap, enums in HashMaps, and static lookup tables built in a static
// initialiser.
import java.util.*;

public class Main {
    enum Planet {
        MERCURY(3.303e+23, 2.4397e6), VENUS(4.869e+24, 6.0518e6), EARTH(5.976e+24, 6.37814e6), MARS(6.421e+23, 3.3972e6);

        private final double mass, radius;

        Planet(double mass, double radius) {
            this.mass = mass;
            this.radius = radius;
        }

        double gravity() {
            return 6.67300E-11 * mass / (radius * radius);
        }
    }

    enum Op implements java.util.function.IntBinaryOperator {
        ADD("+") {
            public int applyAsInt(int a, int b) {
                return a + b;
            }
        },
        SUB("-") {
            public int applyAsInt(int a, int b) {
                return a - b;
            }
        },
        MUL("*") {
            public int applyAsInt(int a, int b) {
                return a * b;
            }
        },
        POW("^") {
            public int applyAsInt(int a, int b) {
                int r = 1;
                for (int i = 0; i < b; i++) {
                    r *= a;
                }
                return r;
            }
        };

        final String symbol;
        private static final Map<String, Op> BY_SYMBOL = new HashMap<>();

        static {
            for (Op o : values()) {
                BY_SYMBOL.put(o.symbol, o);
            }
        }

        Op(String symbol) {
            this.symbol = symbol;
        }

        static Op of(String s) {
            return BY_SYMBOL.get(s);
        }
    }

    enum Level { LOW, MEDIUM, HIGH, CRITICAL }

    static String advice(Level l) {
        switch (l) {
            case LOW:
                return "relax";
            case MEDIUM:
            case HIGH:
                return "watch";
            default:
                return "act";
        }
    }

    public static void main(String[] args) {
        System.out.println("== basics ==");
        for (Planet p : Planet.values()) {
            System.out.println(String.format("%s %d %.4f", p, p.ordinal(), p.gravity()));
        }
        System.out.println(Planet.valueOf("MARS").name() + " " + Planet.EARTH.compareTo(Planet.VENUS) + " "
                + Planet.MARS.getDeclaringClass().getSimpleName());
        try {
            Planet.valueOf("PLUTO");
        } catch (IllegalArgumentException e) {
            System.out.println("no PLUTO");
        }

        System.out.println("== constant bodies ==");
        for (Op o : Op.values()) {
            System.out.println(o + " " + o.symbol + " " + o.applyAsInt(3, 4) + " " + (o.getClass() != Op.class));
        }
        System.out.println(Op.of("^").applyAsInt(2, 10) + " " + Op.of("?"));

        System.out.println("== switch and collections ==");
        for (Level l : Level.values()) {
            System.out.print(advice(l) + " ");
        }
        System.out.println();
        EnumSet<Level> serious = EnumSet.range(Level.MEDIUM, Level.CRITICAL);
        System.out.println(serious + " " + serious.contains(Level.LOW) + " " + EnumSet.allOf(Level.class).size()
                + " " + EnumSet.noneOf(Level.class));
        EnumMap<Level, List<String>> alerts = new EnumMap<>(Level.class);
        String[] msgs = {"disk", "cpu", "net", "fan", "ram"};
        for (int i = 0; i < msgs.length; i++) {
            alerts.computeIfAbsent(Level.values()[i * 3 % 4], k -> new ArrayList<>()).add(msgs[i]);
        }
        System.out.println(alerts);
        Map<Level, Integer> hm = new HashMap<>();
        for (Level l : Level.values()) {
            hm.put(l, l.name().length());
        }
        System.out.println(new TreeMap<>(hm) + " " + hm.get(Level.HIGH));
        List<Level> ls = new ArrayList<>(List.of(Level.HIGH, Level.LOW, Level.CRITICAL, Level.MEDIUM));
        Collections.sort(ls);
        System.out.println(ls + " " + Collections.max(ls));
    }
}
