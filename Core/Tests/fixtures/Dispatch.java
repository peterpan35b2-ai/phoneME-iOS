package corefixture;

interface Operation {
    int apply(int value);
}

final class AddOperation implements Operation {
    public int apply(int value) {
        return value + 6;
    }
}

abstract class AbstractOperation implements Operation {
}

public final class Dispatch {
    private Dispatch() {
    }

    private static native int missingNative();

    public static int interfaceCall() {
        Operation operation = new AddOperation();
        return operation.apply(4);
    }

    public static int missingNativeCall() {
        try {
            return missingNative();
        } catch (UnsatisfiedLinkError error) {
            return 56;
        }
    }
}
