// A table-driven CRC-32 over byte arrays.
//
// byte[] is a wasm array of i8: each step is an array.get_s, a mask to undo
// the sign extension, a table lookup in an int[] and a shift.
public class Main {
    public static void main(String[] args) {
        int[] table = new int[256];
        for (int n = 0; n < 256; n++) {
            int c = n;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) != 0 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
            }
            table[n] = c;
        }
        byte[] data = new byte[1 << 16];
        int seed = 1;
        for (int i = 0; i < data.length; i++) {
            seed = seed * 1103515245 + 12345;
            data[i] = (byte) (seed >>> 16);
        }
        int crc = 0;
        for (int round = 0; round < 300; round++) {
            int c = ~round;
            for (byte b : data) {
                c = table[(c ^ b) & 0xff] ^ (c >>> 8);
            }
            crc ^= ~c;
            data[round & 0xffff] ^= (byte) c;
        }
        System.out.println("crc = " + Integer.toHexString(crc));
    }
}
