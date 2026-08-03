package corefixture;

public final class GcOps {
    private static Object staticRoot;

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
