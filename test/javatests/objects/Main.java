// Object's contract: equals and hashCode overridden together and used by
// HashSet and HashMap, toString, identity versus equality, Objects' helpers
// (equals, hash, requireNonNull, toString with a default, compare), clone of
// arrays and of a Cloneable class, getClass and class literals, and static
// state shared through a singleton and a holder class initialised lazily.
import java.util.*;

public class Main {
    static final class Money implements Comparable<Money> {
        final long cents;
        final String currency;

        Money(long cents, String currency) {
            this.cents = cents;
            this.currency = Objects.requireNonNull(currency, "currency");
        }

        @Override
        public boolean equals(Object o) {
            return this == o || (o instanceof Money m && m.cents == cents && m.currency.equals(currency));
        }

        @Override
        public int hashCode() {
            return Objects.hash(cents, currency);
        }

        @Override
        public String toString() {
            return String.format("%d.%02d %s", cents / 100, cents % 100, currency);
        }

        public int compareTo(Money o) {
            int c = currency.compareTo(o.currency);
            return c != 0 ? c : Long.compare(cents, o.cents);
        }
    }

    static class Sheep implements Cloneable {
        String name;
        int[] tags;

        Sheep(String name, int... tags) {
            this.name = name;
            this.tags = tags;
        }

        @Override
        protected Sheep clone() {
            try {
                Sheep s = (Sheep) super.clone();
                s.tags = tags.clone();
                return s;
            } catch (CloneNotSupportedException e) {
                throw new AssertionError(e);
            }
        }
    }

    enum Registry {
        INSTANCE;

        private final List<String> entries = new ArrayList<>();

        void add(String s) {
            entries.add(s);
        }

        int size() {
            return entries.size();
        }
    }

    static class Holder {
        static int loads;
        static final Holder INSTANCE;

        static {
            loads++;
            INSTANCE = new Holder();
        }

        final long created = 12345;
    }

    public static void main(String[] args) {
        System.out.println("== equals and hashCode ==");
        Money a = new Money(1050, "EUR"), b = new Money(1050, "EUR"), c = new Money(1050, "USD");
        System.out.println(a + " " + a.equals(b) + " " + (a == b) + " " + a.equals(c) + " " + (a.hashCode() == b.hashCode()) + " " + a.equals(null));
        Set<Money> set = new HashSet<>(List.of(a, b, c, new Money(5, "EUR")));
        System.out.println(set.size() + " " + new TreeSet<>(set));
        Map<Money, String> owner = new HashMap<>();
        owner.put(a, "ann");
        System.out.println(owner.get(b) + " " + owner.get(c));
        try {
            new Money(1, null);
        } catch (NullPointerException e) {
            System.out.println("NPE: " + e.getMessage());
        }

        System.out.println("== Objects ==");
        System.out.println(Objects.equals(null, null) + " " + Objects.equals(a, null) + " " + Objects.toString(null, "none") + " "
                + Objects.isNull(null) + " " + Objects.nonNull(a) + " " + Objects.compare(a, c, Comparator.naturalOrder())
                + " " + Objects.hashCode(null) + " " + Objects.requireNonNullElse(null, "fallback") + " " + (Objects.hash() == 1));

        System.out.println("== identity ==");
        Object o1 = new Object(), o2 = new Object();
        System.out.println((o1 == o2) + " " + o1.equals(o2) + " " + o1.equals(o1) + " " + (o1.hashCode() == o1.hashCode()) + " "
                + (System.identityHashCode(a) == a.hashCode()) + " " + (o1.toString().startsWith("java.lang.Object@")));
        String s1 = "lit", s2 = "lit", s3 = new String("lit");
        System.out.println((s1 == s2) + " " + (s1 == s3) + " " + s1.equals(s3) + " " + (s1 == s3.intern()));

        System.out.println("== clone ==");
        Sheep dolly = new Sheep("dolly", 1, 2, 3);
        Sheep copy = dolly.clone();
        copy.name = "polly";
        copy.tags[0] = 99;
        System.out.println(dolly.name + " " + Arrays.toString(dolly.tags) + " " + copy.name + " " + Arrays.toString(copy.tags) + " "
                + (copy.getClass() == Sheep.class));
        String[][] grid = {{"a"}, {"b"}};
        String[][] g2 = grid.clone();
        System.out.println((g2[0] == grid[0]) + " " + (g2 == grid));

        System.out.println("== classes ==");
        System.out.println(Money.class.getSimpleName() + " " + (int[].class == new int[0].getClass()) + " " + a.getClass().getName() + " "
                + Integer.class.isInstance(5) + " " + Number.class.isAssignableFrom(Integer.class) + " " + "x".getClass().getName());

        System.out.println("== singletons ==");
        Registry.INSTANCE.add("x");
        Registry.INSTANCE.add("y");
        System.out.println(Registry.INSTANCE.size() + " " + Registry.valueOf("INSTANCE").size());
        System.out.println("holder loads before: " + Holder.loads);
        System.out.println(Holder.INSTANCE.created + " " + Holder.loads + " " + (Holder.INSTANCE == Holder.INSTANCE));
    }
}
