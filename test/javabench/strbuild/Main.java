// StringBuilder: appending numbers and strings, then reading back.
//
// Integer.toString and Long.toString digit loops, char array growth by
// copying, and toString's copy into a new String.
public class Main {
    public static void main(String[] args) {
        long check = 0;
        for (int round = 0; round < 600; round++) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 1000; i++) {
                sb.append(i * round).append(',').append((long) i << 33).append(' ');
                if ((i & 63) == 0) {
                    sb.append("block").append(i / 64).append('\n');
                }
            }
            String s = sb.toString();
            check = check * 31 + s.length() + s.charAt(s.length() / 2);
        }
        System.out.println("check = " + check);
    }
}
