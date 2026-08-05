package corefixture;

public final class GcOps {
    private static Object staticRoot;

    private interface Callback {
        void run();
    }

    private static final class NestedArgument implements Callback {
        private static final Object classRoot;
        private final int value;

        static {
            classRoot = new Object();
            for (int index = 0; index < 32; ++index) {
                Object temporary = new Object();
                if (temporary == null) {
                    throw new AssertionError();
                }
            }
        }

        NestedArgument(long token) {
            value = (int) token;
            for (int index = 0; index < 32; ++index) {
                Object temporary = new Object();
                if (temporary == null) {
                    throw new AssertionError();
                }
            }
        }

        public void run() {
        }
    }

    private static final class PendingConstructor {
        private short state;
        private String label;
        private Callback callback;

        PendingConstructor(String label, Callback callback, long token) {
            state = -1;
            this.label = label;
            this.callback = callback;
            if (token == 0L) {
                state = 0;
            }
        }
    }

    private GcOps() {
    }

    public static int pressure() {
        staticRoot = new Object();
        Object localRoot = new Object();
        for (int index = 0; index < 200; ++index) {
            Object temporary = new Object();
            if (temporary == null) {
                return 0;
            }
        }
        return staticRoot != null && localRoot != null ? 59 : 0;
    }

    public static int nestedConstructorPressure() {
        PendingConstructor value = new PendingConstructor(
            "avatar",
            new NestedArgument(7L),
            11L);
        return value.state == -1
                && value.label.equals("avatar")
                && value.callback != null ? 83 : 0;
    }

    public static int temporaryStringPressure() {
        int checksum = 0;
        String last = null;
        for (int index = 0; index < 20000; ++index) {
            last = String.valueOf(index);
            checksum += last.length();
        }
        return checksum > 0 && "19999".equals(last) ? 97 : 0;
    }

    public static int catchObjectOutOfMemory() {
        Object[] roots = new Object[64];
        try {
            for (int index = 0; index < roots.length; ++index) {
                roots[index] = new Object();
            }
        } catch (OutOfMemoryError expected) {
            return expected != null ? 71 : 0;
        }
        return 0;
    }

    public static int catchArrayOutOfMemory() {
        Object[] roots = new Object[64];
        try {
            for (int index = 0; index < roots.length; ++index) {
                roots[index] = new int[1];
            }
        } catch (OutOfMemoryError expected) {
            return expected != null ? 73 : 0;
        }
        return 0;
    }
}
