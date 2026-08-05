package corefixture;

interface Operation {
    int apply(int value);
}

final class AddOperation implements Operation {
    public int apply(int value) {
        return value + 6;
    }
}

final class MultiplyOperation implements Operation {
    public int apply(int value) {
        return value * 3;
    }
}

abstract class AbstractOperation implements Operation {
}

public final class Dispatch {
    private Dispatch() {
    }

    private static native int missingNative();

    private static int applyAtOneCallSite(Operation operation) {
        return operation.apply(4);
    }

    public static int interfaceCall() {
        Operation operation = new AddOperation();
        return operation.apply(4);
    }

    public static int polymorphicInterfaceCall() {
        return applyAtOneCallSite(new AddOperation()) * 100
            + applyAtOneCallSite(new MultiplyOperation());
    }

    public static int missingNativeCall() {
        try {
            return missingNative();
        } catch (UnsatisfiedLinkError error) {
            return 56;
        }
    }
}
