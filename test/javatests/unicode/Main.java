// Text beyond ASCII: two-, three- and four-byte UTF-8 characters printed
// straight to stdout, surrogate pairs and code points (codePointAt,
// codePointCount, offsetByCodePoints, Character.toChars), case mapping
// outside Latin-1, Character's classification, reversal that keeps pairs,
// and String.getBytes and new String in UTF-8, UTF-16 and ISO-8859-1.
//
// System.out hands the WASI adapter one UTF-16 code unit at a time, and
// test/teavm/wasi.py encodes them as UTF-8, a surrogate pair as one four-byte
// character, as console.log does.
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class Main {
    public static void main(String[] args) {
        System.out.println("== output ==");
        System.out.println("café naïve jalapeño Ærø ß");
        System.out.println("Ελληνικά Кириллица עברית العربية 日本語 한국어");
        System.out.println("symbols € ™ ∑ √ ∞ ≠ ← → ✓ ★");
        System.out.println("astral 😀 🎉 𝄞 𐍈 🇳🇴");
        System.out.println("é vs é tab\tend nul[\u0000] del[\u007f]".replace('\u0000', '0'));

        System.out.println("== code points ==");
        String s = "a😀b𝄞c";
        System.out.println(s.length() + " " + s.codePointCount(0, s.length()) + " " + s.codePointAt(1) + " " + (int) s.charAt(1) + " "
                + (int) s.charAt(2) + " " + s.offsetByCodePoints(0, 3) + " " + Character.isHighSurrogate(s.charAt(1)) + " "
                + Character.isSurrogatePair(s.charAt(1), s.charAt(2)) + " " + Character.charCount(0x1F600));
        StringBuilder cps = new StringBuilder();
        s.codePoints().forEach(cp -> cps.append(Integer.toHexString(cp)).append(' '));
        System.out.println(cps.toString().trim() + " | " + new String(Character.toChars(0x1F44D)) + " " + new StringBuilder(s).reverse()
                + " " + new String(Character.toChars(0x10348)) + " " + new String(new int[] {0x48, 0x1F30D}, 0, 2));

        System.out.println("== case and classes ==");
        System.out.println("ÄÖÜ ÉÈ ΣΑΣ ДОМ".toLowerCase() + " " + "straße ﬁ ǆ".toUpperCase() + " " + Character.toUpperCase('ß')
                + " " + Character.isLetter('ж') + " " + Character.isDigit('٣') + " " + Character.getNumericValue('٣') + " "
                + Character.isUpperCase('Ж') + " " + Character.isWhitespace(' ') + " " + Character.isLetter(0x1F600));
        System.out.println("Ünïcödé".equalsIgnoreCase("üNÏCÖDÉ") + " " + "éa".compareTo("ea") + " " + "日本".hashCode() + " "
                + "😀".hashCode());

        System.out.println("== encodings ==");
        String text = "Grüße 😀";
        byte[] utf8 = text.getBytes(StandardCharsets.UTF_8);
        byte[] latin = "Grüße".getBytes(StandardCharsets.ISO_8859_1);
        byte[] utf16 = "Aé".getBytes(StandardCharsets.UTF_16BE);
        System.out.println(utf8.length + " " + Arrays.toString(utf8) + " " + latin.length + " " + Arrays.toString(utf16));
        System.out.println(new String(utf8, StandardCharsets.UTF_8) + " " + new String(latin, StandardCharsets.ISO_8859_1) + " "
                + new String(utf16, StandardCharsets.UTF_16BE) + " " + new String(new byte[] {(byte) 0xE2, (byte) 0x82, (byte) 0xAC},
                StandardCharsets.UTF_8));
        String broken = new String(new byte[] {(byte) 0xC3, 0x28, (byte) 0xFF}, StandardCharsets.UTF_8);
        System.out.println(broken.length() + " " + (int) broken.charAt(0) + " " + (int) broken.charAt(2));
    }
}
