package corefixture;

public final class JitOps {
    private static int fieldState = 1;
    private int instanceValue;

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

    public int scanWithNativeProgress(int limit) {
        String marker = "x";
        int total = 0;
        for (int index = 0; index < limit; index++) {
            total += marker.charAt(0);
        }
        return total;
    }

    public static int cfgConstantLoop(int limit) {
        int scale = 8;
        int total = 0;
        for (int index = 0; index < limit; index++) {
            total += index * scale;
        }
        return total;
    }

    public static int loopInvariantMath(int left, int right, int limit) {
        int total = 0;
        for (int index = 0; index < limit; index++) {
            total += left * right;
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

    public static void throwObject(Throwable value) throws Throwable {
        throw value;
    }

    public static int catchNullArrayLength(Object[] values) {
        try {
            return values.length;
        } catch (NullPointerException exception) {
            return 41;
        }
    }

    public static int catchArrayBounds(int[] values, int index) {
        try {
            return values[index];
        } catch (ArrayIndexOutOfBoundsException exception) {
            return 42;
        }
    }

    public static int catchThrownRuntime(Throwable value) throws Throwable {
        try {
            throw value;
        } catch (RuntimeException exception) {
            return 43;
        }
    }

    public static int synchronizedAdd(Object lock, int value) {
        synchronized (lock) {
            return value + 5;
        }
    }

    public static void synchronizedThrow(Object lock, Throwable value)
            throws Throwable {
        synchronized (lock) {
            throw value;
        }
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

    public static int deadLocalWrites(int value) {
        int dead = value + 1;
        dead++;
        return value * 3;
    }

    public static int commonExpression(int left, int right) {
        int first = left + right;
        int second = left + right;
        return first + second;
    }

    public static int crossBlockCommonExpression(int left, int right, int selector) {
        int first = left + right;
        if (selector != 0) return first + (left + right);
        return first;
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

    public static long longArithmetic(long left, long right, int shift) {
        long mixed = ((left + right) * (left - right));
        return mixed ^ (left << shift) ^ (right >>> shift);
    }

    public static long longDivide(long left, long right) {
        return left / right + left % right;
    }

    public static int longCompare(long left, long right) {
        return left < right ? -1 : (left == right ? 0 : 1);
    }

    public static long longLoop(int limit) {
        long total = 0L;
        for (int index = 0; index <= limit; index++) {
            total += index;
        }
        return total;
    }

    public static long intLongRoundTrip(int value) {
        long widened = value;
        return (widened << 33) + (int)(widened * 3L);
    }

    public static int fieldAccessFallback(int value) {
        fieldState += value;
        return fieldState;
    }

    public static int instanceThenDivide(JitOps target, int divisor) {
        target.instanceValue++;
        return 10 / divisor;
    }

    public static int callInstanceThenDivide(JitOps target, int divisor) {
        return instanceThenDivide(target, divisor);
    }

    private static int identity(int value) {
        return value;
    }

    public static int callFallback(int value) {
        return identity(value);
    }

    private int privateAdd(int value) {
        return instanceValue + value;
    }

    private int privateIdentity(int value) {
        return value;
    }

    public int callPrivate(int value) {
        return privateAdd(value);
    }

    public int callPrivateIdentity(int value) {
        return privateIdentity(value);
    }

    public final int finalIdentity(int value) {
        return value;
    }

    public int callFinalIdentity(int value) {
        return finalIdentity(value);
    }

    public int virtualAdd(int value) {
        return instanceValue + value + 1;
    }

    public static int callVirtual(JitOps target, int value) {
        return target.virtualAdd(value);
    }

    public interface IntUnary {
        int apply(int value);
    }

    public static final class IntUnaryImpl implements IntUnary {
        public int apply(int value) {
            return value * 3;
        }
    }

    public static final class IntUnaryPlus implements IntUnary {
        public int apply(int value) {
            return value + 5;
        }
    }

    public static int callInterface(IntUnary target, int value) {
        return target.apply(value);
    }

    public static IntUnary makeCapturedAdder(final int addend) {
        return value -> value + addend;
    }

    public static int callCapturedAdder(int addend, int value) {
        return makeCapturedAdder(addend).apply(value);
    }

    public static int scalarReplaceIntArray(int value) {
        int[] box = new int[1];
        box[0] = value;
        return box[0] + 1;
    }

    public static int allocationFallback(int length) {
        return new int[length].length;
    }

    public static int referenceArrayLength(int length) {
        return new Object[length].length;
    }

    public static Object preserveAcrossArrayAllocation(Object live, int length) {
        int[] temporary = new int[length];
        if (temporary.length != length) return null;
        return live;
    }

    public static Object objectAllocationFallback() {
        return new Object();
    }

    public static JitOps newJitOps() {
        return new JitOps();
    }

    public static final class LazyStatic {
        static int value = 77;
    }

    public static int incrementThenReadLazyStatic(JitOps target) {
        target.instanceValue++;
        return LazyStatic.value;
    }

    public static int statefulOsrLoop(int[] counter, int limit) {
        int total = 0;
        for (int index = 0; index < limit; index++) {
            counter[0] = counter[0] + 1;
            if (index == 10) total += LazyStatic.value;
            total += index;
        }
        return total;
    }

    public static int writeThenCall(JitOps target, int value) {
        target.instanceValue++;
        return identity(value);
    }

    public static int osrCallLoop(int[] counter, int limit) {
        int total = 0;
        for (int index = 0; index < limit; index++) {
            counter[0] = counter[0] + 1;
            total += identity(index);
        }
        return total;
    }

    public static int osrUnboundedCallLoop(int[] counter, int limit) {
        int total = 0;
        for (int index = 0; index < limit; index++) {
            counter[0] = counter[0] + 1;
            total += loopIdentity(index);
        }
        return total;
    }

    private static int loopIdentity(int value) {
        int result = value;
        for (int pass = 0; pass < 1; pass++) {
            result += pass;
        }
        return result;
    }

    public static final class Allocated {
        public int value;

        public Allocated(int value) {
            this.value = value;
        }
    }

    public static Allocated allocateObject(int value) {
        return new Allocated(value);
    }

    public static int multiArrayFallback(int length) {
        return new int[length][1].length;
    }

    public static int multiArrayShape(int outer, int inner) {
        int[][] values = new int[outer][inner];
        return values.length * 100 + values[0].length;
    }

    public static String stringConstant() {
        return "phoneME-JIT";
    }

    public static Class classConstant() {
        return JitOps.class;
    }

    public static int readStaticValue() {
        return fieldState;
    }

    public static int isJitOps(Object value) {
        return value instanceof JitOps ? 1 : 0;
    }

    public static JitOps castJitOps(Object value) {
        return (JitOps)value;
    }

    public int readInstanceValue() {
        return instanceValue;
    }

    public void writeInstanceValue(int value) {
        instanceValue = value;
    }

    public static void writeNullableInstanceValue(JitOps target, int value) {
        target.instanceValue = value;
    }

    public static int readIntArray(int[] values, int index) {
        return values[index] + values.length;
    }

    public static int sumIntArray(int[] values) {
        int total = 0;
        int length = values.length;
        for (int index = 0; index < length; index++) {
            total += values[index];
        }
        return total;
    }

    public static int fillIntArray(int[] values) {
        int length = values.length;
        for (int index = 0; index < length; index++) {
            values[index] = index;
        }
        return length;
    }

    public static long readLongArray(long[] values, int index) {
        return values[index];
    }

    public static Object readObjectArray(Object[] values, int index) {
        return values[index];
    }

    public static int readByteArray(byte[] values, int index) {
        return values[index];
    }

    public static int readCharArray(char[] values, int index) {
        return values[index];
    }

    public static int readShortArray(short[] values, int index) {
        return values[index];
    }

    public static void writeIntArray(int[] values, int index, int value) {
        values[index] = value;
    }

    public static void writeLongArray(long[] values, int index, long value) {
        values[index] = value;
    }

    public static void writeFloatArray(float[] values, int index, float value) {
        values[index] = value;
    }

    public static void writeDoubleArray(double[] values, int index, double value) {
        values[index] = value;
    }

    public static void writeObjectArray(Object[] values, int index, Object value) {
        values[index] = value;
    }

    public static void writeByteArray(byte[] values, int index, int value) {
        values[index] = (byte)value;
    }

    public static void writeCharArray(char[] values, int index, int value) {
        values[index] = (char)value;
    }

    public static void writeShortArray(short[] values, int index, int value) {
        values[index] = (short)value;
    }

    private static float addFloats(float left, float right) {
        return left + right;
    }

    public static float callFloats(float left, float right) {
        return addFloats(left, right);
    }

    private static double addDoubles(double left, double right) {
        return left + right;
    }

    public static double callDoubles(double left, double right) {
        return addDoubles(left, right);
    }

    public static float floatArithmetic(float left, float right) {
        float mixed = (left + right) * (left - right);
        return -mixed / right + 1.5f;
    }

    public static double doubleArithmetic(double left, double right) {
        double mixed = (left + right) * (left - right);
        return -mixed / right + 1.25d;
    }

    public static float readFloatArray(float[] values, int index) {
        return values[index];
    }

    public static double readDoubleArray(double[] values, int index) {
        return values[index];
    }

    public static float floatRemainder(float left, float right) {
        return left % right;
    }

    public static double doubleRemainder(double left, double right) {
        return left % right;
    }

    public static int floatCompareLess(float left, float right) {
        if (left < right) return -1;
        if (left == right) return 0;
        return 1;
    }

    public static int floatCompareGreater(float left, float right) {
        if (left > right) return 1;
        if (left == right) return 0;
        return -1;
    }

    public static int doubleCompareLess(double left, double right) {
        if (left < right) return -1;
        if (left == right) return 0;
        return 1;
    }

    public static int doubleCompareGreater(double left, double right) {
        if (left > right) return 1;
        if (left == right) return 0;
        return -1;
    }

    public static float intToFloat(int value) {
        return value;
    }

    public static double intToDouble(int value) {
        return value;
    }

    public static float longToFloat(long value) {
        return value;
    }

    public static double longToDouble(long value) {
        return value;
    }

    public static int floatToInt(float value) {
        return (int)value;
    }

    public static long floatToLong(float value) {
        return (long)value;
    }

    public static double floatToDouble(float value) {
        return value;
    }

    public static int doubleToInt(double value) {
        return (int)value;
    }

    public static long doubleToLong(double value) {
        return (long)value;
    }

    public static float doubleToFloat(double value) {
        return (float)value;
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

    private static long addLongs(long left, long right) {
        return left + right;
    }

    private static int divideHelper(int value) {
        return 100 / value;
    }

    public static long callLongs(long left, long right) {
        return addLongs(left, right);
    }

    public static int callThrowing(int value) {
        return divideHelper(value);
    }
}
