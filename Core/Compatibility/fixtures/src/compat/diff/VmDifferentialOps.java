package compat.diff;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.Calendar;
import java.util.Date;
import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Random;
import java.util.Stack;
import java.util.StringTokenizer;
import java.util.TimeZone;
import java.util.Vector;

public final class VmDifferentialOps {
    private VmDifferentialOps() {
    }

    private interface IntSupplier {
        int get();
    }

    private static class Base {
        int value() {
            return 3;
        }
    }

    private static final class Derived extends Base implements IntSupplier {
        int value() {
            return 11;
        }

        public int get() {
            return 17;
        }
    }

    private static class InitializedBase {
        static int value = 5;
    }

    private static final class InitializedChild extends InitializedBase {
        static int child = value * 3;
    }

    private static final class BrokenInit {
        static int value = failInitialization();
    }

    private static int failInitialization() {
        throw new RuntimeException("expected init failure");
    }

    private static int threadCounter;

    private static final class CounterThread extends Thread {
        private final int iterations;

        CounterThread(int iterations) {
            this.iterations = iterations;
        }

        public void run() {
            for (int i = 0; i < iterations; i++) {
                synchronized (VmDifferentialOps.class) {
                    threadCounter++;
                }
            }
        }
    }

    private static final class AssignRunnable implements Runnable {
        public void run() {
            threadCounter = 73;
        }
    }

    public static int intOverflow() {
        int value = Integer.MAX_VALUE;
        return value + 1;
    }

    public static int intDivisionRemainder() {
        int left = -17;
        int right = 5;
        return (left / right) * 100 + (left % right);
    }

    public static int intShiftMasking() {
        int value = 0x81234567;
        return (value >>> 36) ^ (value << 35) ^ (value >> 34);
    }

    public static long longOverflow() {
        long value = Long.MAX_VALUE;
        return value + 1L;
    }

    public static long longShiftMasking() {
        long value = 0x8123456789ABCDEFL;
        return (value >>> 68) ^ (value << 67) ^ (value >> 66);
    }

    public static int floatNanBits() {
        float zero = 0.0f;
        return Float.floatToIntBits(zero / zero);
    }

    public static int floatNegativeZeroBits() {
        return Float.floatToIntBits(-0.0f);
    }

    public static long doubleNanBits() {
        double zero = 0.0;
        return Double.doubleToLongBits(zero / zero);
    }

    public static long doubleNegativeZeroBits() {
        return Double.doubleToLongBits(-0.0);
    }

    public static int floatingConversions() {
        int result = 0;
        result ^= (int) Float.NaN;
        result ^= (int) Float.POSITIVE_INFINITY;
        result ^= (int) Float.NEGATIVE_INFINITY;
        result ^= (int) 12345.875f;
        result ^= (int) -12345.875;
        return result;
    }

    public static long doubleToLongConversions() {
        long result = 0L;
        result ^= (long) Double.NaN;
        result ^= (long) Double.POSITIVE_INFINITY;
        result ^= (long) Double.NEGATIVE_INFINITY;
        result ^= (long) 9.223372036854776E18;
        result ^= (long) -9.223372036854776E18;
        return result;
    }

    public static int denseSwitch() {
        int total = 0;
        for (int value = -2; value <= 4; value++) {
            switch (value) {
            case -1: total += 3; break;
            case 0: total += 5; break;
            case 1: total += 7; break;
            case 2: total += 11; break;
            default: total += 13; break;
            }
        }
        return total;
    }

    public static int sparseSwitch() {
        int total = 0;
        int[] values = {-1000, 7, 4096, 99};
        for (int i = 0; i < values.length; i++) {
            switch (values[i]) {
            case -1000: total += 17; break;
            case 7: total += 19; break;
            case 4096: total += 23; break;
            default: total += 29; break;
            }
        }
        return total;
    }

    public static int primitiveArrays() {
        byte[] bytes = new byte[4];
        short[] shorts = new short[3];
        char[] chars = new char[2];
        boolean[] booleans = new boolean[2];
        bytes[0] = (byte) 0xFF;
        shorts[1] = (short) 0x8123;
        chars[0] = '\ufffe';
        booleans[1] = true;
        return bytes[0] + shorts[1] + chars[0] + (booleans[1] ? 31 : 0);
    }

    public static int multiArray() {
        int[][][] values = new int[2][3][4];
        values[1][2][3] = 97;
        return values.length * 1000 + values[0].length * 100
                + values[1][2].length * 10 + values[1][2][3];
    }

    public static int referenceArraysAndCasts() {
        Base[] values = new Derived[2];
        values[0] = new Derived();
        int result = values[0].value();
        try {
            values[1] = new Base();
            result = -1000;
        } catch (ArrayStoreException expected) {
            result += 100;
        }
        Object object = values[0];
        if (object instanceof IntSupplier) {
            result += ((IntSupplier) object).get();
        }
        try {
            String ignored = (String) object;
            result = ignored.length();
        } catch (ClassCastException expected) {
            result += 1000;
        }
        return result;
    }

    public static int exceptionAndFinally() {
        int result = 0;
        try {
            int value = 1 / result;
            result = value;
        } catch (ArithmeticException expected) {
            result = 41;
        } finally {
            result += 1;
        }
        try {
            Object object = null;
            result += object.hashCode();
        } catch (NullPointerException expected) {
            result += 2;
        }
        return result;
    }

    public static int dispatch() {
        Base base = new Derived();
        IntSupplier supplier = (IntSupplier) base;
        return base.value() * 100 + supplier.get();
    }

    public static int classInitialization() {
        return InitializedChild.value * 10 + InitializedChild.child;
    }

    public static int unicodeString() {
        String value = "Điện thoại \ud83d\udcf1";
        return value.length() * 100000
                + value.charAt(0) * 100
                + value.charAt(2);
    }

    public static int stringOperations() {
        String base = "prefix-middle-suffix";
        String middle = base.substring(7, 13);
        int result = middle.equals("middle") ? 1 : 0;
        result = result * 31 + base.indexOf("middle");
        result = result * 31 + base.lastIndexOf('f');
        result = result * 31 + "abc".compareTo("abd");
        result = result * 31 + "  trim  ".trim().length();
        return result;
    }

    public static int stringBufferOperations() {
        StringBuffer buffer = new StringBuffer();
        buffer.append('A').append(12).append(':').append(true);
        buffer.insert(1, "xy");
        buffer.deleteCharAt(3);
        buffer.reverse();
        return buffer.toString().hashCode();
    }

    public static int vectorOperations() {
        Vector values = new Vector();
        values.addElement("b");
        values.insertElementAt("a", 0);
        values.addElement("c");
        values.removeElementAt(1);
        return values.size() * 1000
                + ((String) values.elementAt(0)).charAt(0) * 10
                + ((String) values.lastElement()).charAt(0);
    }

    public static int hashtableOperations() {
        Hashtable values = new Hashtable();
        values.put("a", new Integer(7));
        values.put("b", new Integer(11));
        int result = ((Integer) values.get("a")).intValue();
        result = result * 31 + (values.containsKey("b") ? 1 : 0);
        values.remove("b");
        result = result * 31 + values.size();
        return result;
    }

    public static int tokenizerOperations() {
        StringTokenizer tokenizer = new StringTokenizer("one,two,,three", ",");
        int result = tokenizer.countTokens();
        while (tokenizer.hasMoreTokens()) {
            result = result * 31 + tokenizer.nextToken().length();
        }
        return result;
    }

    public static int tokenizerDelimiterChange() {
        StringTokenizer tokenizer = new StringTokenizer("a,b;c", ",");
        int result = tokenizer.countTokens();
        result = result * 31 + tokenizer.nextToken().hashCode();
        result = result * 31 + tokenizer.nextToken(";").hashCode();
        result = result * 31 + tokenizer.nextToken().hashCode();
        return result;
    }

    public static int tokenizerReturnDelimiters() {
        String delimiter = "\ud83d\ude00";
        StringTokenizer tokenizer = new StringTokenizer(
                "a\ud83d\ude00b\ud83d\ude00\ud83d\ude00c",
                delimiter,
                true);
        int result = tokenizer.countTokens();
        while (tokenizer.hasMoreElements()) {
            String token = (String) tokenizer.nextElement();
            result = result * 31 + token.length();
            result = result * 31 + token.hashCode();
        }
        return result;
    }

    public static void uncaughtTokenizerExhaustion() {
        StringTokenizer tokenizer = new StringTokenizer("one");
        tokenizer.nextToken();
        tokenizer.nextToken();
    }

    public static long dataStreamRoundTrip() {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeInt(0x81234567);
            output.writeLong(0x123456789ABCDEFL);
            output.writeUTF("Điện");
            output.flush();

            DataInputStream input = new DataInputStream(
                    new ByteArrayInputStream(bytes.toByteArray()));
            long result = input.readInt();
            result = result * 31L + input.readLong();
            result = result * 31L + input.readUTF().hashCode();
            return result;
        } catch (Exception error) {
            return Long.MIN_VALUE;
        }
    }

    public static int randomSequence() {
        Random random = new Random(0x123456789ABCDEFL);
        int result = 1;
        for (int i = 0; i < 8; i++) {
            result = result * 31 + random.nextInt();
        }
        return result;
    }

    public static int systemArrayCopy() {
        int[] values = {1, 2, 3, 4, 5};
        System.arraycopy(values, 0, values, 1, 4);
        System.arraycopy(values, 1, values, 0, 4);
        int result = 1;
        for (int i = 0; i < values.length; i++) {
            result = result * 31 + values[i];
        }
        Object[] objects = {"a", "b", "c"};
        System.arraycopy(objects, 0, objects, 1, 2);
        for (int i = 0; i < objects.length; i++) {
            result = result * 31 + objects[i].hashCode();
        }
        return result;
    }

    public static int wrapperSemantics() {
        int result = Integer.parseInt("7f", 16);
        result = result * 31 + Integer.valueOf(-123).intValue();
        result = result * 31 + Integer.toHexString(-1).hashCode();
        result = result * 31 + Long.toString(
                Long.parseLong("-1234567890123")).hashCode();
        result = result * 31 + (Boolean.valueOf("TrUe").booleanValue() ? 1 : 0);
        result = result * 31 + Character.toUpperCase('z');
        return result;
    }

    public static long mathSemantics() {
        long result = Double.doubleToLongBits(Math.sqrt(2.0));
        result ^= Double.doubleToLongBits(Math.floor(-1.25));
        result ^= Double.doubleToLongBits(Math.ceil(-1.25));
        result ^= Double.doubleToLongBits(Math.min(0.0, -0.0));
        result ^= Double.doubleToLongBits(Math.max(-0.0, 0.0));
        result ^= Math.round(12345.75);
        result ^= Math.abs(Long.MIN_VALUE);
        return result;
    }

    public static int stackOperations() {
        Stack stack = new Stack();
        stack.push("a");
        stack.push("b");
        stack.push("c");
        int result = ((String) stack.peek()).hashCode();
        result = result * 31 + stack.search("a");
        result = result * 31 + ((String) stack.pop()).hashCode();
        result = result * 31 + (stack.empty() ? 1 : 0);
        return result;
    }

    public static int enumerationOperations() {
        Vector values = new Vector();
        values.addElement("one");
        values.addElement("two");
        values.addElement("three");
        Enumeration enumeration = values.elements();
        int result = 1;
        while (enumeration.hasMoreElements()) {
            result = result * 31
                    + ((String) enumeration.nextElement()).hashCode();
        }
        return result;
    }

    public static long dateOperations() {
        Date first = new Date(-123456789L);
        Date second = new Date(987654321L);
        long result = first.getTime();
        result = result * 31L + second.getTime();
        result = result * 31L + (first.before(second) ? 1L : 0L);
        result = result * 31L + (second.after(first) ? 1L : 0L);
        result = result * 31L + first.compareTo(second);
        first.setTime(second.getTime());
        result = result * 31L + (first.equals(second) ? 1L : 0L);
        result = result * 31L + first.hashCode();
        return result;
    }

    public static int calendarUtcOperations() {
        Calendar calendar = Calendar.getInstance(TimeZone.getTimeZone("GMT"));
        calendar.setTimeInMillis(0L);
        int result = calendar.get(Calendar.YEAR);
        result = result * 31 + calendar.get(Calendar.MONTH);
        result = result * 31 + calendar.get(Calendar.DAY_OF_MONTH);
        result = result * 31 + calendar.get(Calendar.DAY_OF_WEEK);
        result = result * 31 + calendar.get(Calendar.HOUR_OF_DAY);
        result = result * 31 + calendar.get(Calendar.MINUTE);
        result = result * 31 + calendar.get(Calendar.SECOND);
        return result;
    }

    public static int timeZoneOperations() {
        TimeZone zone = TimeZone.getTimeZone("GMT+07:00");
        int result = zone.getRawOffset();
        result = result * 31 + zone.getID().hashCode();
        result = result * 31 + (zone.useDaylightTime() ? 1 : 0);
        TimeZone copy = TimeZone.getTimeZone(zone.getID());
        result = result * 31 + (zone.hasSameRules(copy) ? 1 : 0);
        return result;
    }

    public static int modifiedUtfRoundTrip() {
        try {
            String expected = "A\u0000\u07ff\u0800\ud83d\ude00";
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            DataOutputStream output = new DataOutputStream(bytes);
            output.writeUTF(expected);
            output.flush();
            DataInputStream input = new DataInputStream(
                    new ByteArrayInputStream(bytes.toByteArray()));
            String actual = input.readUTF();
            return actual.equals(expected) ? actual.hashCode() : -1;
        } catch (Exception error) {
            return -2;
        }
    }

    public static int readerWriterRoundTrip() {
        try {
            String expected = "Tiếng Việt \ud83d\ude00";
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            OutputStreamWriter writer = new OutputStreamWriter(bytes, "UTF-8");
            writer.write(expected);
            writer.close();
            InputStreamReader reader = new InputStreamReader(
                    new ByteArrayInputStream(bytes.toByteArray()), "UTF-8");
            char[] buffer = new char[64];
            int count = reader.read(buffer, 0, buffer.length);
            String actual = new String(buffer, 0, count);
            return actual.equals(expected)
                    ? actual.hashCode() * 31 + count
                    : -1;
        } catch (Exception error) {
            return -2;
        }
    }

    public static int classSemantics() {
        Class derived = Derived.class;
        Class base = Base.class;
        int result = derived.getName().hashCode();
        result = result * 31 + (base.isInstance(new Derived()) ? 1 : 0);
        result = result * 31 + (base.isAssignableFrom(derived) ? 1 : 0);
        result = result * 31 + (derived.getSuperclass() == base ? 1 : 0);
        result = result * 31 + (int[].class.isArray() ? 1 : 0);
        result = result * 31
                + (int[].class.getComponentType() == Integer.TYPE ? 1 : 0);
        return result;
    }

    public static int runnableThreadJoin() {
        threadCounter = 0;
        Thread thread = new Thread(new AssignRunnable());
        thread.start();
        try {
            thread.join();
        } catch (InterruptedException error) {
            return -1;
        }
        return threadCounter;
    }

    public static int synchronizedThreadCounters() {
        threadCounter = 0;
        Thread first = new CounterThread(40);
        Thread second = new CounterThread(60);
        first.start();
        second.start();
        try {
            first.join();
            second.join();
        } catch (InterruptedException error) {
            return -1;
        }
        return threadCounter;
    }

    public static void uncaughtNullPointer() {
        Object value = null;
        value.hashCode();
    }

    public static void uncaughtArrayBounds() {
        int[] values = new int[1];
        values[2] = 3;
    }

    public static void uncaughtClassCast() {
        Object value = new Base();
        String text = (String) value;
        text.length();
    }

    public static void uncaughtArithmetic() {
        int zero = 0;
        int value = 1 / zero;
        if (value == 0) {
            threadCounter = value;
        }
    }

    public static int brokenInitializationFirst() {
        return BrokenInit.value;
    }

    public static int brokenInitializationSecond() {
        return BrokenInit.value;
    }
}
