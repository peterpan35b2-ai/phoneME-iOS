package corefixture;

public final class JitOps {
    private JitOps() {
    }

    public static int branch(int value) {
        if (value < 0) return -value;
        if (value == 0) return 7;
        return value + 3;
    }

    public static int sumLoop(int limit) {
        int total = 0;
        for (int index = 0; index <= limit; index++) {
            total += index;
        }
        return total;
    }

    public static int tableSwitch(int value) {
        switch (value) {
        case 0: return 10;
        case 1: return 20;
        case 2: return 30;
        case 3: return 40;
        default: return -1;
        }
    }

    public static int lookupSwitch(int value) {
        switch (value) {
        case -100: return 1;
        case 17: return 2;
        case 1000: return 3;
        default: return 4;
        }
    }

    public static int divide(int left, int right) {
        return left / right;
    }

    public static int remainder(int left, int right) {
        return left % right;
    }

    public static int largeConstant(int value) {
        return value + 123456789;
    }

    public static int bitMix(int value, int shift) {
        int mixed = (value << shift) ^ (value >>> (shift + 1));
        return (byte)mixed + (short)(mixed >>> 8) + (char)(mixed >>> 16);
    }

    public static int strengthReduce(int value) {
        return ((value * 8) + 0) | 0;
    }

    public static int propagatedLocal(int value) {
        int first = 7;
        int second = first + 5;
        return value + second;
    }

    public static int foldedBranch(int value) {
        int selector = 3;
        if (selector == 3) {
            return value + 11;
        }
        return value - 11;
    }

    public static int constantDivision(int value) {
        return value / 7 + value % 7;
    }

    public static int isNull(Object value) {
        return value == null ? 1 : 0;
    }

    public static int sameReference(Object left, Object right) {
        return left == right ? 1 : 0;
    }

    public static Object chooseReference(Object left, Object right, boolean first) {
        return first ? left : right;
    }

    public int sameAsReceiver(Object value) {
        return this == value ? 1 : 0;
    }
}
