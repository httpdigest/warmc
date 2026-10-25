// Build and walk a binary tree, the classic collector benchmark.
//
// The same shape as ktbench/bintree, gobench/bintree and rsbench/bintree, so
// the rows are comparable: here, as in Kotlin's, the nodes are wasm GC structs
// the engine owns.
public class Main {
    static final class Node {
        final Node left, right;
        final int value;

        Node(Node left, Node right, int value) {
            this.left = left;
            this.right = right;
            this.value = value;
        }
    }

    static Node build(int depth, int v) {
        if (depth == 0) {
            return null;
        }
        return new Node(build(depth - 1, v * 2), build(depth - 1, v * 2 + 1), v);
    }

    static long walk(Node n) {
        if (n == null) {
            return 0;
        }
        return n.value + walk(n.left) + walk(n.right);
    }

    public static void main(String[] args) {
        long s = 0;
        for (int r = 0; r < 60; r++) {
            s += walk(build(16, r));
        }
        System.out.println("s = " + s);
    }
}
