public final class Arm64Stress {
    private static final Object LOCK = new Object();
    private static int synchronizedCalls;

    private static final class Node {
        Node next;
        int value;
        long wide;

        Node(Node next, int value, long wide) {
            this.next = next;
            this.value = value;
            this.wide = wide;
        }
    }

    private static int synchronizedStep(int value) {
        synchronized (LOCK) {
            synchronizedCalls++;
            return value + synchronizedCalls;
        }
    }

    public static void main(String[] args) {
        Node head = null;
        long checksum = 0;

        for (int round = 0; round < 32; round++) {
            for (int i = 0; i < 160; i++) {
                long wide = (((long) round) << 32) | (long) i;
                head = new Node(head, round + i, wide);
                checksum += wide;
            }

            char[] text = new char[96];
            for (int i = 0; i < text.length; i++) {
                text[i] = (char) ('A' + ((round + i) % 26));
            }
            String value = new String(text);
            StringBuffer buffer = new StringBuffer(value);
            buffer.append(':').append(round);
            checksum += buffer.length();

            Object[] source = new Object[] { head, value, buffer };
            Object[] target = new Object[3];
            System.arraycopy(source, 0, target, 0, source.length);
            if (target[0] != head || target[1] != value || target[2] != buffer) {
                throw new RuntimeException("object array copy failed");
            }

            checksum += synchronizedStep(round);
            if ((round & 3) == 0) {
                System.gc();
            }
        }

        int nodes = 0;
        long tailChecksum = 0;
        for (Node node = head; node != null && nodes < 160; node = node.next) {
            tailChecksum += node.value;
            tailChecksum ^= node.wide;
            nodes++;
        }

        if (nodes != 160 || synchronizedCalls != 32) {
            throw new RuntimeException("reference or monitor state failed");
        }

        System.out.println("arm64-stress-ok:" + nodes);
        System.out.println("checksum:" + (checksum ^ tailChecksum));
    }
}
