package corefixture;

import java.lang.ref.WeakReference;

public final class WeakReferenceOps {
    private static WeakReference reference;
    private static Object strong;

    private static final class DerivedWeakReference extends WeakReference {
        DerivedWeakReference(Object referent) {
            super(referent);
        }
    }

    private WeakReferenceOps() {
    }

    public static int explicitClear() {
        Object value = new Object();
        WeakReference local = new WeakReference(value);
        if (local.get() != value) {
            return 1;
        }
        local.clear();
        return local.get() == null ? 0 : 2;
    }

    public static int createWeakOnly() {
        Object value = new Object();
        reference = new DerivedWeakReference(value);
        if (reference.get() != value) {
            return 1;
        }
        value = null;
        return 0;
    }

    public static int createStrong() {
        strong = new Object();
        reference = new WeakReference(strong);
        return reference.get() == strong ? 0 : 1;
    }

    public static int expectCleared() {
        return reference != null && reference.get() == null ? 0 : 1;
    }

    public static int expectStrong() {
        return reference != null && reference.get() == strong ? 0 : 1;
    }

    public static int releaseStrong() {
        strong = null;
        return 0;
    }
}
