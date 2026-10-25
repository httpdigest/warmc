// The console as the WASI adapter provides it: print and println of each
// type TeaVM's PrintStream has a print for (it has none for float or
// boolean), printf, System.err beside System.out, lines longer than the
// adapter's 4,096-byte buffer, empty lines, bytes written as an array (which
// TeaVM decodes as UTF-8 before the host sees them), lone surrogates (written
// as U+FFFD, as console.log writes them), and a last line with no newline,
// which the adapter writes when main returns.
//
// A byte written alone waits in a buffer that only flush empties, and
// TeaVM 0.16's flush dereferences null, so there is none of those.
// test/gobench.py compares stdout with wasmtime's, so stderr is here only for
// what it does to stdout: nothing.
public class Main {
    public static void main(String[] args) {
        System.out.print("print: ");
        System.out.print(1);
        System.out.print(' ');
        System.out.print(2L);
        System.out.print(' ');
        System.out.print(4.25);
        System.out.print(' ');
        System.out.print(new char[] {'c', 'h'});
        System.out.print(' ');
        System.out.print((Object) null);
        System.out.println();
        System.out.println();
        System.out.println("after an empty line");
        System.err.println("to stderr, which stdout does not see");
        System.out.printf("printf %d %s %.3f\n", 7, "seven", 7.0 / 3);
        StringBuilder longLine = new StringBuilder();
        for (int i = 0; i < 1500; i++) {
            longLine.append(i % 10).append("é");
        }
        System.out.println(longLine.length() + " " + longLine.substring(0, 20));
        System.out.println(longLine);
        System.out.write(new byte[] {'W', (byte) 0xC3, (byte) 0xA9, ' ', (byte) 0xF0, (byte) 0x9F, (byte) 0x98, (byte) 0x80, '\n'}, 0, 9);
        System.out.println("lone high [" + '\uD83D' + "] lone low [" + '\uDE00' + "] pair [" + "😀" + "]");
        System.out.println("high then high [" + "\uD83D😀" + "]");
        System.out.println(String.valueOf(new char[] {'\uD800'}) + "|end");
        for (int i = 0; i < 3; i++) {
            System.out.println("line " + i);
            System.err.print("e" + i + " ");
        }
        System.err.println();
        System.out.print("no newline at the end");
    }
}
