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
}
