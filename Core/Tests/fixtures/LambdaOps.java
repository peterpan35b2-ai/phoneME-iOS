package corefixture;

public final class LambdaOps {
    public interface IntUnary {
        int apply(int value);
    }

    public interface IntConsumer {
        void accept(int value);
    }

    public interface IntToLong {
        long apply(int value);
    }

    public interface IntSupplier {
        int get();
    }

    public interface LongSupplier {
        long get();
    }

    public interface StringSupplier {
        String get();
    }

    public interface DefaultValue {
        default int value() {
            return 9;
        }
    }

    public interface Factory {
        LambdaOps create(int base);
    }

    public interface StringLength {
        int length(String value);
    }

    public interface GenericLength<T> {
        int length(T value);
    }

    public interface GenericFactory<T, R> {
        R create(T value);
    }

    public interface GenericMapper<T, R> {
        R map(T value);
    }

    public static final class DefaultImpl implements DefaultValue {
    }

    public static class ParentAdder {
        int addInherited(int value) {
            return value + 40;
        }
    }

    public static final class ChildAdder extends ParentAdder {
    }

    public static final class BoxedConstructorTarget {
        final int value;

        BoxedConstructorTarget(int value) {
            this.value = value;
        }
    }

    private static int sideEffect;

    private final int base;

    private LambdaOps(int base) {
        this.base = base;
    }

    private int add(int value) {
        return base + value;
    }

    private static int twice(int value) {
        return value * 2;
    }

    private static int remember(int value) {
        sideEffect = value;
        return value + 1;
    }

    private static long widenInput(long value) {
        return value + 5L;
    }

    private static void acceptBoxed(Integer value) {
        sideEffect = value.intValue();
    }

    private static Integer boxedValue() {
        return Integer.valueOf(41);
    }

    private static Integer boxedLongValue() {
        return Integer.valueOf(43);
    }

    private static Integer nullBoxedValue() {
        return null;
    }

    @SuppressWarnings("unchecked")
    private static <T> T genericStringValue() {
        return (T) "lambda";
    }

    @SuppressWarnings("unchecked")
    private static <T> T genericWrongValue() {
        return (T) Integer.valueOf(1);
    }

    public static int staticLambda() {
        IntUnary operation = value -> value + 3;
        return operation.apply(4);
    }

    public static int capturedLambda() {
        int base = 7;
        IntUnary operation = value -> base + value;
        return operation.apply(5);
    }

    public static int staticMethodReference() {
        IntUnary operation = LambdaOps::twice;
        return operation.apply(6);
    }

    public static int boundMethodReference() {
        LambdaOps owner = new LambdaOps(8);
        IntUnary operation = owner::add;
        return operation.apply(5);
    }

    public static int inheritedBoundMethodReference() {
        ChildAdder owner = new ChildAdder();
        IntUnary operation = owner::addInherited;
        return operation.apply(2);
    }

    public static int constructorMethodReference() {
        Factory factory = LambdaOps::new;
        return factory.create(11).add(2);
    }

    public static int serializableLambda() {
        IntUnary operation = (IntUnary & java.io.Serializable)
            (value -> value + 2);
        int marker = operation instanceof java.io.Serializable ? 20 : 0;
        return marker + operation.apply(3);
    }

    public static int unboundMethodReference() {
        StringLength length = String::length;
        return length.length("Việt");
    }

    public static int genericLambda() {
        GenericLength<String> length = value -> value.length();
        return length.length("game");
    }

    public static int boxedConstructorMethodReference() {
        GenericFactory<Integer, BoxedConstructorTarget> factory =
            BoxedConstructorTarget::new;
        return factory.create(Integer.valueOf(17)).value;
    }

    public static int boxedReturnMethodReference() {
        GenericMapper<Integer, Integer> mapper = LambdaOps::twice;
        return mapper.map(Integer.valueOf(9)).intValue();
    }

    public static int voidCompatibleMethodReference() {
        sideEffect = 0;
        IntConsumer consumer = LambdaOps::remember;
        consumer.accept(23);
        return sideEffect;
    }

    public static int widenedPrimitiveArgumentMethodReference() {
        IntToLong operation = LambdaOps::widenInput;
        return (int) operation.apply(7);
    }

    public static int widenedPrimitiveReturnMethodReference() {
        IntToLong operation = LambdaOps::twice;
        return (int) operation.apply(7);
    }

    public static int boxedPrimitiveArgumentMethodReference() {
        sideEffect = 0;
        IntConsumer consumer = LambdaOps::acceptBoxed;
        consumer.accept(29);
        return sideEffect;
    }

    public static int unboxedReferenceReturnMethodReference() {
        IntSupplier supplier = LambdaOps::boxedValue;
        return supplier.get();
    }

    public static int unboxedWidenedReturnMethodReference() {
        LongSupplier supplier = LambdaOps::boxedLongValue;
        return (int) supplier.get();
    }

    public static int nullReturnUnboxingFailure() {
        IntSupplier supplier = LambdaOps::nullBoxedValue;
        try {
            supplier.get();
            return 0;
        } catch (NullPointerException expected) {
            return 47;
        }
    }

    public static int genericReferenceReturnCastSuccess() {
        StringSupplier supplier = LambdaOps::genericStringValue;
        return supplier.get().length();
    }

    public static int genericReferenceReturnCastFailure() {
        StringSupplier supplier = LambdaOps::genericWrongValue;
        try {
            supplier.get();
            return 0;
        } catch (ClassCastException expected) {
            return 53;
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static int genericReferenceCastFailure() {
        GenericLength<String> length = String::length;
        GenericLength raw = length;
        try {
            raw.length(Integer.valueOf(1));
            return 0;
        } catch (ClassCastException expected) {
            return 31;
        }
    }

    public static int nullUnboxingFailure() {
        GenericFactory<Integer, BoxedConstructorTarget> factory =
            BoxedConstructorTarget::new;
        try {
            factory.create(null);
            return 0;
        } catch (NullPointerException expected) {
            return 37;
        }
    }

    public static int defaultInterfaceMethod() {
        DefaultValue value = new DefaultImpl();
        return value.value();
    }
}
