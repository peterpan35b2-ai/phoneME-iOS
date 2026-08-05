package corefixture;

public final class Arithmetic {
    private static int initializedValue = 4;
    private static int differentialStatic;
    private int value;

    public Arithmetic(int value) {
        this.value = value;
    }

    public int add(int other) {
        return value + other;
    }

    public static int twice(int value) {
        return value * 2;
    }

    public static int initializedValue() {
        return initializedValue;
    }

    public static long currentTime() {
        return System.currentTimeMillis();
    }

    public static int vietnameseLength() {
        return "Việt".length();
    }

    public static int vietnameseCharacter() {
        return "Việt".charAt(2);
    }

    public static boolean internedEquality() {
        return "Việt".equals("Việt");
    }

    public static int run() {
        Arithmetic arithmetic = new Arithmetic(7);
        return arithmetic.add(twice(3));
    }

    public static int arrayRun() {
        int[] values = new int[3];
        values[0] = 4;
        values[1] = 5;
        return values.length + values[0] + values[1];
    }

    public static Arithmetic resolvedOperandEffects() {
        Arithmetic arithmetic = new Arithmetic(11);
        differentialStatic = twice(5);
        arithmetic.value = arithmetic.add(differentialStatic);
        return arithmetic;
    }

    public static int differentialStaticValue() {
        return differentialStatic;
    }

    public static void stableThrowableMessage() {
        throw new NoSuchMethodError("decoded-linkage-stable");
    }
}
