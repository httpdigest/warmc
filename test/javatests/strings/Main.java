// Strings: concatenation of every type, StringBuilder (append, insert,
// delete, reverse, setCharAt, setLength, replace), substring, indexOf and
// lastIndexOf, split with limits and regex-free separators, replace, trim and
// strip, repeat, join, case conversion, comparison and equalsIgnoreCase,
// chars and code points, valueOf and parsing, String.format, text blocks,
// and a few text algorithms (run-length coding, Caesar, word wrap,
// Levenshtein, palindromes).
//
// A TeaVM String is a struct around a wasm array of i16, so these are packed
// array copies, char loops and short-lived string allocation.
import java.util.*;

public class Main {
    static String rle(String s) {
        StringBuilder sb = new StringBuilder();
        int i = 0;
        while (i < s.length()) {
            int j = i;
            while (j < s.length() && s.charAt(j) == s.charAt(i)) {
                j++;
            }
            sb.append(j - i).append(s.charAt(i));
            i = j;
        }
        return sb.toString();
    }

    static String caesar(String s, int k) {
        char[] cs = s.toCharArray();
        for (int i = 0; i < cs.length; i++) {
            char c = cs[i];
            if (Character.isUpperCase(c)) {
                cs[i] = (char) ('A' + (c - 'A' + k + 26) % 26);
            } else if (Character.isLowerCase(c)) {
                cs[i] = (char) ('a' + (c - 'a' + k + 26) % 26);
            }
        }
        return new String(cs);
    }

    static List<String> wrap(String text, int width) {
        List<String> lines = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        for (String w : text.split(" ")) {
            if (w.isEmpty()) {
                continue;
            }
            if (cur.length() > 0 && cur.length() + 1 + w.length() > width) {
                lines.add(cur.toString());
                cur.setLength(0);
            }
            if (cur.length() > 0) {
                cur.append(' ');
            }
            cur.append(w);
        }
        if (cur.length() > 0) {
            lines.add(cur.toString());
        }
        return lines;
    }

    static int levenshtein(String a, String b) {
        int[] prev = new int[b.length() + 1];
        for (int j = 0; j <= b.length(); j++) {
            prev[j] = j;
        }
        for (int i = 1; i <= a.length(); i++) {
            int[] cur = new int[b.length() + 1];
            cur[0] = i;
            for (int j = 1; j <= b.length(); j++) {
                int cost = a.charAt(i - 1) == b.charAt(j - 1) ? 0 : 1;
                cur[j] = Math.min(Math.min(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
            }
            prev = cur;
        }
        return prev[b.length()];
    }

    public static void main(String[] args) {
        System.out.println("== concatenation ==");
        Object nothing = null;
        System.out.println("s" + 1 + 2L + 3.5f + 4.25 + 'c' + true + nothing + new int[0].length + (1 + 2) + Integer.MIN_VALUE + Long.MAX_VALUE);
        String s = "";
        for (int i = 0; i < 5; i++) {
            s += i + ",";
        }
        System.out.println(s + " " + String.valueOf((Object) null) + " " + String.valueOf(new char[] {'h', 'i'}) + " " + String.valueOf(1e-7));

        System.out.println("== StringBuilder ==");
        StringBuilder sb = new StringBuilder();
        sb.append("abc").append(1).append(2L).append(3.5).append(true).append('x').append((String) null).append(-0.0f);
        System.out.println(sb + " len=" + sb.length());
        sb.insert(0, "<<").insert(5, 99).deleteCharAt(2).delete(4, 8).replace(0, 1, "[").reverse();
        System.out.println(sb);
        sb.setLength(5);
        sb.setCharAt(0, 'Z');
        System.out.println(sb + " " + sb.indexOf("u") + " " + sb.charAt(1) + " " + new StringBuilder("racecar").reverse());

        System.out.println("== searching and slicing ==");
        String t = "the quick brown fox jumps over the lazy dog";
        System.out.println(t.indexOf("the") + " " + t.indexOf("the", 1) + " " + t.lastIndexOf('o') + " " + t.lastIndexOf("o", 20)
                + " " + t.indexOf('z') + " " + t.indexOf("cat") + " " + t.contains("fox") + " " + t.startsWith("quick", 4)
                + " " + t.endsWith("dog"));
        System.out.println(t.substring(4, 9) + "|" + t.substring(40) + "|" + t.replace("the", "a") + "|" + t.replace('o', '0'));
        System.out.println(Arrays.toString("a,b,,c,,".split(",")) + " " + Arrays.toString("a,b,,c,,".split(",", -1)) + " "
                + Arrays.toString("a,b,c".split(",", 2)) + " " + Arrays.toString("  x  y ".trim().split(" +")));
        System.out.println("[" + "  pad  ".trim() + "][" + " \t s \n".strip() + "][" + "  l".stripLeading() + "][" + "r  ".stripTrailing()
                + "][" + "ab".repeat(3) + "][" + String.join("/", List.of("x", "y", "z")) + "][" + " ".isBlank() + "]");

        System.out.println("== case and comparison ==");
        System.out.println("MiXeD".toLowerCase() + " " + "MiXeD".toUpperCase() + " " + "apple".compareTo("banana") + " " + "b".compareTo("a")
                + " " + "abc".compareTo("ab") + " " + "Hello".equalsIgnoreCase("hELLO") + " " + "a".compareToIgnoreCase("B")
                + " " + "x".equals(new String("x")) + " " + ("x" == "x"));
        List<String> names = new ArrayList<>(List.of("bob", "Alice", "carol", "Dave", "alice"));
        Collections.sort(names);
        System.out.println(names);
        names.sort(String.CASE_INSENSITIVE_ORDER);
        System.out.println(names);

        System.out.println("== chars ==");
        String digits = "a1b22c333";
        int dsum = 0;
        for (char c : digits.toCharArray()) {
            if (Character.isDigit(c)) {
                dsum += c - '0';
            }
        }
        System.out.println(dsum + " " + digits.chars().filter(Character::isLetter).count() + " " + Character.isWhitespace('\t')
                + " " + Character.toUpperCase('q') + " " + Character.isLetterOrDigit('_') + " " + (int) Character.forDigit(11, 16)
                + " " + Character.digit('f', 16) + " " + Character.toChars(65)[0]);

        System.out.println("== parsing and formatting ==");
        System.out.println(Integer.parseInt("-42") + Long.parseLong("9000000000") + " " + Boolean.parseBoolean("TRUE") + " "
                + Double.parseDouble("2.5") + " " + Integer.valueOf(77).toString() + " " + Long.toString(-5));
        System.out.println(String.format("[%d] [%5d] [%-5d] [%05d] [%x] [%X] [%o] [%s] [%10s] [%-10s] [%c] [%b] [%%]", 42, 42, 42, 42, 255, 255, 8,
                "str", "right", "left", 'z', true));
        System.out.println(String.format("[%.2f] [%8.3f] [%-8.1f] [%.0f] [%,d] [%+d] [%(d] [%s]", Math.PI, Math.E, -1.25, 2.5, 1234567, 5, -5,
                List.of(1, 2)));
        String block = """
            Dear %s,
              total: %d items
            """.formatted("Ann", 3);
        System.out.print(block);

        System.out.println("== algorithms ==");
        System.out.println(rle("aaabccddddde") + " " + caesar("Hello, World!", 3) + " " + caesar(caesar("Round Trip", 11), -11));
        System.out.println(wrap("the quick brown fox jumps over the lazy dog and keeps on running far away", 16));
        System.out.println(levenshtein("kitten", "sitting") + " " + levenshtein("flaw", "lawn") + " " + levenshtein("", "abc"));
        for (String p : new String[] {"racecar", "abba", "abc", "A man a plan a canal Panama"}) {
            String norm = p.toLowerCase().replace(" ", "");
            System.out.print(new StringBuilder(norm).reverse().toString().equals(norm) + " ");
        }
        System.out.println();
        System.out.println("hash " + t.hashCode() + " " + "".hashCode() + " " + "Aa".hashCode() + " " + "BB".hashCode());
    }
}
