package corefixture;

public final class ClinitHarness {
    private ClinitHarness() {
    }

    public static int firstUse() {
        try {
            return ClinitFailure.value();
        } catch (ExceptionInInitializerError error) {
            return error.getException() instanceof IllegalStateException &&
                   error.getCause() == error.getException()
                ? ClinitTracker.count() : -1;
        }
    }

    public static int secondUse() {
        try {
            return ClinitFailure.value();
        } catch (NoClassDefFoundError error) {
            return ClinitTracker.count() + 10;
        }
    }
}

final class ClinitTracker {
    private static int initializationCount;

    private ClinitTracker() {
    }

    static void increment() {
        ++initializationCount;
    }

    static int count() {
        return initializationCount;
    }
}

final class ClinitFailure {
    static {
        ClinitTracker.increment();
        if (ClinitTracker.count() > 0) {
            throw new IllegalStateException();
        }
    }

    private ClinitFailure() {
    }

    static int value() {
        return 99;
    }
}
