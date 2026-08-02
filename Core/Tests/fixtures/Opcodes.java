package corefixture;

public final class Opcodes {
    private Opcodes() {
    }

    public static int tableSwitch(int value) {
        switch (value) {
        case 0:
            return 10;
        case 1:
            return 20;
        case 2:
            return 30;
        default:
            return 40;
        }
    }

    public static int lookupSwitch(int value) {
        switch (value) {
        case -100:
            return 1;
        case 7:
            return 2;
        case 999:
            return 3;
        default:
            return 4;
        }
    }

    public static long doubleToLong(double value) {
        return (long) value;
    }

    public static int floatCompare(float left, float right) {
        if (left < right) {
            return -1;
        }
        if (left == right) {
            return 0;
        }
        return 1;
    }

    public static double doubleArithmetic(double left, double right) {
        return (left + right) * (left - right) / right;
    }

    public static int narrowInt(int value) {
        return (byte) value + (char) value + (short) value;
    }

    public static int multiArray() {
        int[][][] values = new int[2][3][4];
        values[1][2][3] = 9;
        return values.length + values[0].length + values[0][0].length +
               values[1][2][3];
    }

    public static int classLiteral() {
        return Opcodes.class == Opcodes.class ? 60 : 0;
    }

    public static int arrayTypes() {
        Object value = new String[1][1];
        int score = value instanceof Object[] ? 1 : 0;
        score += value instanceof Cloneable ? 2 : 0;
        score += value instanceof java.io.Serializable ? 4 : 0;
        Object[] objects = (Object[]) value;
        return score + objects.length;
    }

    public static int narrowArraySemantics() {
        byte[] bytes = new byte[1];
        short[] shorts = new short[1];
        char[] chars = new char[1];
        boolean[] booleans = new boolean[1];
        bytes[0] = (byte) 255;
        shorts[0] = (short) 65535;
        chars[0] = (char) 65535;
        booleans[0] = true;
        return (bytes[0] == -1 ? 1 : 0)
            | (shorts[0] == -1 ? 2 : 0)
            | (chars[0] == 65535 ? 4 : 0)
            | (booleans[0] ? 8 : 0);
    }
}
