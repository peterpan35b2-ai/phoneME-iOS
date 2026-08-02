package corefixture;

public final class Exceptions {
    private static int marker;

    private Exceptions() {
    }

    private static int divideByZero() {
        int zero = 0;
        return 10 / zero;
    }

    public static int catchNestedDivideByZero() {
        try {
            return divideByZero();
        } catch (ArithmeticException exception) {
            return 41;
        }
    }

    public static int catchNullArrayLength() {
        try {
            int[] values = null;
            return values.length;
        } catch (NullPointerException exception) {
            return 42;
        }
    }

    public static int catchArrayBounds() {
        try {
            int[] values = new int[1];
            return values[2];
        } catch (ArrayIndexOutOfBoundsException exception) {
            return 43;
        }
    }

    public static int catchNegativeArraySize() {
        try {
            int length = -1;
            int[] values = new int[length];
            return values.length;
        } catch (NegativeArraySizeException exception) {
            return 44;
        }
    }

    private static void throwWithFinally() {
        try {
            marker = 1;
            throw new IllegalStateException();
        } finally {
            marker = 45;
        }
    }

    public static int catchFinallyAndSuperclass() {
        try {
            throwWithFinally();
            return 0;
        } catch (RuntimeException exception) {
            return marker;
        }
    }

    public static int catchThrowNull() {
        try {
            throw (RuntimeException) null;
        } catch (NullPointerException exception) {
            return 46;
        }
    }

    public static int catchArrayStore() {
        try {
            Object[] values = new String[1];
            values[0] = new Arithmetic(1);
            return 0;
        } catch (ArrayStoreException exception) {
            return 48;
        }
    }

    public static int typeChecks() {
        Object value = new Arithmetic(1);
        int score = value instanceof Arithmetic ? 1 : 0;
        try {
            String invalid = (String) value;
            return invalid.length();
        } catch (ClassCastException exception) {
            return score + 46;
        }
    }

    public static void uncaught() {
        throw new IllegalStateException();
    }
}
