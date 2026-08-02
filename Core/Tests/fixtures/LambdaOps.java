package corefixture;

public final class LambdaOps {
    public interface IntUnary {
        int apply(int value);
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

    public static final class DefaultImpl implements DefaultValue {
    }

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

    public static int defaultInterfaceMethod() {
        DefaultValue value = new DefaultImpl();
        return value.value();
    }
}
