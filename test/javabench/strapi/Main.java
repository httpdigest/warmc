// The String API: split, indexOf, substring, replace, case and comparison.
//
// A TeaVM string is a struct around a char array, made by every one of these
// calls, so this is copying of packed i16 arrays and allocation of the
// strings that hold them.
public class Main {
    public static void main(String[] args) {
        String text = "The quick brown fox jumps over the lazy dog, then the Dog sleeps; "
                + "foxes and dogs, quick or lazy, all sleep eventually. ";
        long h = 0;
        for (int round = 0; round < 12000; round++) {
            String s = text + round;
            String[] parts = s.split(" ");
            for (String p : parts) {
                if (p.indexOf('o') >= 0) {
                    h += p.toUpperCase().hashCode();
                } else {
                    h ^= p.length();
                }
            }
            String r = s.replace("the", "THE").replace(',', ';');
            h = h * 31 + r.indexOf("THE lazy") + r.lastIndexOf("o") + r.substring(4, 19).compareTo("quick brown fox");
            h += s.toLowerCase().contains("dog sleeps") ? 1 : 0;
            h += s.trim().length() + (s.startsWith("The") ? 2 : 0) + (s.endsWith(Integer.toString(round)) ? 4 : 0);
        }
        System.out.println("h = " + h);
    }
}
