package compat.diff;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FilterInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Hashtable;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Random;
import java.util.Set;
import java.util.Stack;
import java.util.StringTokenizer;
import java.util.TimeZone;
import java.util.Vector;

public final class VmDifferentialOps {
    private VmDifferentialOps() {
    }

    private static final class ExposedRandom extends Random {
        ExposedRandom(long seed) {
            super(seed);
        }

        int nextBits(int bits) {
            return super.next(bits);
        }
    }

    private static final class ExposedHashtable extends Hashtable {
        ExposedHashtable(int capacity, float loadFactor) {
            super(capacity, loadFactor);
        }

        void forceRehash() {
            rehash();
        }
    }

    private static final class ExposedPrintStream extends PrintStream {
        ExposedPrintStream(ByteArrayOutputStream output) {
            super(output);
        }

        void exposeSetError() {
            setError();
        }
    }

    private static final class ExposedFilterInputStream
            extends FilterInputStream {
        ExposedFilterInputStream(InputStream input) {
            super(input);
        }
    }

    private static final class ExposedInputStream extends InputStream {
        private final byte[] data;
        private int position;

        ExposedInputStream(byte[] input) {
            data = input;
        }

        public int read() {
            return position < data.length ? data[position++] & 0xFF : -1;
        }
    }

    private static final class ExposedReader extends Reader {
        private final char[] data;
        private int position;

        ExposedReader(String input) {
            super();
            data = input.toCharArray();
        }

        ExposedReader(String input, Object lock) {
            super(lock);
            data = input.toCharArray();
        }

        public int read(char[] output, int offset, int length) {
            if (position >= data.length) return -1;
            int count = Math.min(length, data.length - position);
            System.arraycopy(data, position, output, offset, count);
            position += count;
            return count;
        }

        public void close() {
        }
    }

    private static final class ExposedBasicPermission
            extends java.security.BasicPermission {
        ExposedBasicPermission(String name) {
            super(name);
        }

        ExposedBasicPermission(String name, String actions) {
            super(name, actions);
        }
    }

    private static String singleLine(String value) {
        StringBuffer escaped = new StringBuffer(value.length());
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            if (character == '\\') escaped.append("\\\\");
            else if (character == '\n') escaped.append("\\n");
            else if (character == '\r') escaped.append("\\r");
            else if (character == '\t') escaped.append("\\t");
            else escaped.append(character);
        }
        return escaped.toString();
    }

    private static void appendThrowable(StringBuffer trace, Throwable value) {
        trace.append(value.getClass().getName()).append(':')
                .append(String.valueOf(value.getMessage())).append(':')
                .append(value.toString()).append('|');
    }

    private static final class ConcreteVirtualMachineError
            extends VirtualMachineError {
        ConcreteVirtualMachineError() {
            super();
        }

        ConcreteVirtualMachineError(String message) {
            super(message);
        }
    }

    private static final class ConcretePermission
            extends java.security.Permission {
        ConcretePermission(String name) {
            super(name);
        }

        public boolean implies(java.security.Permission permission) {
            return equals(permission);
        }

        public boolean equals(Object other) {
            return other instanceof ConcretePermission
                    && getName().equals(((ConcretePermission) other).getName());
        }

        public int hashCode() {
            return getName().hashCode();
        }

        public String getActions() {
            return "";
        }
    }

    private static final class ExposedWriter extends java.io.Writer {
        private final StringBuffer output = new StringBuffer();

        ExposedWriter() {
            super();
        }

        ExposedWriter(Object lock) {
            super(lock);
        }

        public void write(char[] buffer, int offset, int length) {
            output.append(buffer, offset, length);
        }

        public void flush() {
        }

        public void close() {
        }

        String value() {
            return output.toString();
        }
    }

    private static final class ExposedOutputStream
            extends java.io.OutputStream {
        private final ByteArrayOutputStream output = new ByteArrayOutputStream();

        public void write(int value) {
            output.write(value);
        }

        byte[] value() {
            return output.toByteArray();
        }
    }

    private static final class ExposedFilterOutputStream
            extends java.io.FilterOutputStream {
        ExposedFilterOutputStream(java.io.OutputStream output) {
            super(output);
        }
    }

    private static final class ExposedCalendar extends Calendar {
        ExposedCalendar() {
            super();
        }

        protected void computeTime() {
            time = 0L;
        }

        protected void computeFields() {
            fields[YEAR] = 1970;
            fields[MONTH] = JANUARY;
            fields[DATE] = 1;
        }
    }

    private static final class ExposedGregorianCalendar
            extends java.util.GregorianCalendar {
        ExposedGregorianCalendar() {
            super();
        }

        ExposedGregorianCalendar(TimeZone zone) {
            super(zone);
        }

        void forceComputeFields() {
            super.computeFields();
        }

        void forceComputeTime() {
            super.computeTime();
        }
    }

    private static final class ExposedTimeZone extends TimeZone {
        private int offset;

        public int getOffset(int era, int year, int month, int day,
                             int dayOfWeek, int milliseconds) {
            return offset;
        }

        public void setRawOffset(int value) {
            offset = value;
        }

        public int getRawOffset() {
            return offset;
        }

        public boolean useDaylightTime() {
            return false;
        }
    }

    private static final class NoOpTimerTask extends java.util.TimerTask {
        public void run() {
        }
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

    public static String openJdkRemainingSurfaceTrace() {
        try {
            StringBuffer trace = new StringBuffer();

            ExposedOutputStream output = new ExposedOutputStream();
            java.io.OutputStream outputBase = output;
            outputBase.write(new byte[] {0, 1, 2, 3}, 1, 2);
            outputBase.flush();
            outputBase.close();
            byte[] outputValue = output.value();
            trace.append(outputValue.length).append(':')
                    .append(outputValue[0]).append(':')
                    .append(outputValue[1]).append('|');

            ByteArrayOutputStream filteredBytes = new ByteArrayOutputStream();
            ExposedFilterOutputStream filtered =
                    new ExposedFilterOutputStream(filteredBytes);
            filtered.write(65);
            filtered.write(new byte[] {66, 67, 68}, 1, 2);
            filtered.flush();
            filtered.close();
            trace.append(filteredBytes.toString("UTF-8")).append('|');

            BufferedInputStream bufferedInput = new BufferedInputStream(
                    new ByteArrayInputStream(new byte[] {9, 8}));
            trace.append(bufferedInput.read()).append(':')
                    .append(bufferedInput.read()).append('|');
            bufferedInput.close();
            ByteArrayOutputStream bufferedTarget = new ByteArrayOutputStream();
            BufferedOutputStream bufferedOutput =
                    new BufferedOutputStream(bufferedTarget);
            bufferedOutput.write(new byte[] {7, 6});
            bufferedOutput.flush();
            bufferedOutput.close();
            trace.append(bufferedTarget.size()).append(':')
                    .append(bufferedTarget.toByteArray()[1]).append('|');

            ByteArrayOutputStream byteOutput = new ByteArrayOutputStream();
            byteOutput.write(new byte[] {5, 4, 3});
            DataOutputStream dataOutput = new DataOutputStream(byteOutput);
            dataOutput.write(new byte[] {2, 1});
            dataOutput.flush();
            trace.append(byteOutput.size()).append(':')
                    .append(byteOutput.toByteArray()[4]).append('|');

            ExposedWriter writer = new ExposedWriter();
            java.io.Writer writerBase = writer;
            writerBase.write('A');
            writerBase.write(new char[] {'B', 'C'});
            writerBase.write("wxyz", 1, 2);
            writerBase.flush();
            writerBase.close();
            ExposedWriter lockedWriter = new ExposedWriter(new Object());
            lockedWriter.write('Q');
            trace.append(writer.value()).append(':')
                    .append(lockedWriter.value()).append('|');

            File file = new File("vm-diff-file.bin");
            file.delete();
            FileOutputStream firstOutput =
                    new FileOutputStream(file.getPath(), true);
            firstOutput.write(new byte[] {1, 2, 3}, 0, 3);
            firstOutput.flush();
            firstOutput.close();
            FileOutputStream secondOutput = new FileOutputStream(file, true);
            secondOutput.write(new byte[] {4, 5}, 0, 2);
            secondOutput.flush();
            secondOutput.close();
            FileInputStream fileInput = new FileInputStream(file.getPath());
            byte[] filePrefix = new byte[2];
            int firstRead = fileInput.read(filePrefix);
            byte[] fileMiddle = new byte[4];
            int secondRead = fileInput.read(fileMiddle, 1, 2);
            long skipped = fileInput.skip(1L);
            int available = fileInput.available();
            fileInput.close();
            trace.append(firstRead).append(':')
                    .append(filePrefix[0]).append(':').append(filePrefix[1])
                    .append(':').append(secondRead).append(':')
                    .append(fileMiddle[1]).append(':').append(fileMiddle[2])
                    .append(':').append(skipped).append(':')
                    .append(available).append(':').append(file.length())
                    .append(':').append(file.delete()).append('|');

            Object object = new Object();
            Object other = new Object();
            int hash = object.hashCode();
            int identity = System.identityHashCode(object);
            synchronized (object) {
                object.notify();
                object.wait(1L, 500000);
            }
            trace.append(object.equals(object)).append(':')
                    .append(object.equals(other)).append(':')
                    .append(hash == identity).append('|');

            Object referent = new Object();
            java.lang.ref.WeakReference reference =
                    new java.lang.ref.WeakReference(referent);
            trace.append(reference.get() == referent).append(':');
            reference.clear();
            trace.append(reference.get() == null).append('|');

            Runtime.getRuntime().gc();
            trace.append(System.identityHashCode(null)).append('|');

            ConcretePermission permission = new ConcretePermission("surface");
            trace.append(permission.newPermissionCollection() == null)
                    .append(':').append(permission.toString()).append('|');
            ExposedBasicPermission basic =
                    new ExposedBasicPermission("surface.*");
            java.security.PermissionCollection collection =
                    basic.newPermissionCollection();
            collection.add(basic);
            int permissionCount = 0;
            Enumeration permissionElements = collection.elements();
            while (permissionElements.hasMoreElements()) {
                permissionElements.nextElement();
                permissionCount++;
            }
            collection.setReadOnly();
            trace.append(permissionCount).append(':')
                    .append(collection.isReadOnly()).append('|');
            java.security.AccessController.checkPermission(
                    new RuntimePermission("surface"));

            ExposedCalendar calendar = new ExposedCalendar();
            calendar.clear();
            calendar.setTimeZone(TimeZone.getTimeZone("GMT"));
            trace.append(calendar.getTimeZone().getID()).append('|');
            ExposedTimeZone zone = new ExposedTimeZone();
            zone.setID("Custom/Zone");
            zone.setRawOffset(1234);
            int zoneOffset = zone.getOffset(
                    1, 2020, Calendar.JANUARY, 1, Calendar.WEDNESDAY, 0);
            TimeZone previousZone = TimeZone.getDefault();
            TimeZone.setDefault(zone);
            trace.append(zone.getID()).append(':')
                    .append(zone.getRawOffset()).append(':')
                    .append(zoneOffset).append(':')
                    .append(zone.useDaylightTime()).append(':')
                    .append(TimeZone.getDefault().getID()).append('|');
            TimeZone.setDefault(previousZone);

            ExposedGregorianCalendar defaultGregorian =
                    new ExposedGregorianCalendar();
            ExposedGregorianCalendar zonedGregorian =
                    new ExposedGregorianCalendar(TimeZone.getTimeZone("GMT"));
            defaultGregorian.forceComputeFields();
            zonedGregorian.clear();
            zonedGregorian.set(Calendar.YEAR, 1970);
            zonedGregorian.set(Calendar.MONTH, Calendar.JANUARY);
            zonedGregorian.set(Calendar.DATE, 1);
            zonedGregorian.forceComputeTime();
            trace.append(zonedGregorian.getTimeZone().getID()).append('|');

            HashMap map = new HashMap();
            map.put("a", "1");
            HashSet set = new HashSet();
            set.add("a");
            trace.append(map.toString()).append(':')
                    .append(set.toString()).append('|');

            java.util.Timer timer = new java.util.Timer();
            java.util.TimerTask delayed = new NoOpTimerTask();
            timer.schedule(delayed,
                    new Date(System.currentTimeMillis() + 60000L), 60000L);
            timer.cancel();
            java.util.Timer fixedTimer = new java.util.Timer();
            java.util.TimerTask fixed = new NoOpTimerTask();
            fixedTimer.scheduleAtFixedRate(fixed,
                    new Date(System.currentTimeMillis() + 60000L), 60000L);
            fixedTimer.cancel();
            trace.append("timers");
            return trace.toString();
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ':'
                    + error.getMessage();
        }
    }

    public static String exceptionConstructorSurfaceTrace() {
        StringBuffer trace = new StringBuffer();
        Throwable cause = new Throwable("cause");

        appendThrowable(trace, new java.io.EOFException());
        appendThrowable(trace, new java.io.EOFException("m"));
        appendThrowable(trace, new java.io.IOException());
        appendThrowable(trace, new java.io.InterruptedIOException());
        appendThrowable(trace, new java.io.InterruptedIOException("m"));
        appendThrowable(trace, new java.io.UTFDataFormatException());
        appendThrowable(trace, new java.io.UTFDataFormatException("m"));
        appendThrowable(trace, new java.io.UnsupportedEncodingException());
        appendThrowable(trace, new java.io.UnsupportedEncodingException("m"));

        appendThrowable(trace, new AbstractMethodError());
        appendThrowable(trace, new AbstractMethodError("m"));
        appendThrowable(trace, new ArithmeticException());
        appendThrowable(trace, new ArithmeticException("m"));
        appendThrowable(trace, new ArrayIndexOutOfBoundsException());
        appendThrowable(trace, new ArrayIndexOutOfBoundsException(7));
        appendThrowable(trace, new ArrayIndexOutOfBoundsException("m"));
        appendThrowable(trace, new ArrayStoreException());
        appendThrowable(trace, new ArrayStoreException("m"));
        appendThrowable(trace, new BootstrapMethodError());
        appendThrowable(trace, new BootstrapMethodError("m"));
        appendThrowable(trace, new ClassCastException());
        appendThrowable(trace, new ClassCastException("m"));
        appendThrowable(trace, new ClassFormatError());
        appendThrowable(trace, new ClassFormatError("m"));
        appendThrowable(trace, new ClassNotFoundException());
        appendThrowable(trace, new ClassNotFoundException("m"));
        appendThrowable(trace, new CloneNotSupportedException());
        appendThrowable(trace, new CloneNotSupportedException("m"));
        appendThrowable(trace, new Error());
        appendThrowable(trace, new Error("m"));
        appendThrowable(trace, new Exception());
        appendThrowable(trace, new ExceptionInInitializerError());
        appendThrowable(trace, new ExceptionInInitializerError("m"));
        ExceptionInInitializerError initializer =
                new ExceptionInInitializerError(cause);
        appendThrowable(trace, initializer);
        trace.append(initializer.getException() == cause).append('|');
        appendThrowable(trace, new IllegalAccessException());
        appendThrowable(trace, new IllegalAccessException("m"));
        appendThrowable(trace, new IllegalArgumentException());
        appendThrowable(trace, new IllegalArgumentException("m"));
        appendThrowable(trace, new IllegalMonitorStateException());
        appendThrowable(trace, new IllegalMonitorStateException("m"));
        appendThrowable(trace, new IllegalThreadStateException());
        appendThrowable(trace, new IllegalThreadStateException("m"));
        appendThrowable(trace, new IncompatibleClassChangeError());
        appendThrowable(trace, new IncompatibleClassChangeError("m"));
        appendThrowable(trace, new IndexOutOfBoundsException());
        appendThrowable(trace, new IndexOutOfBoundsException("m"));
        appendThrowable(trace, new InstantiationError());
        appendThrowable(trace, new InstantiationError("m"));
        appendThrowable(trace, new InstantiationException());
        appendThrowable(trace, new InstantiationException("m"));
        appendThrowable(trace, new InterruptedException());
        appendThrowable(trace, new InterruptedException("m"));
        appendThrowable(trace, new LinkageError());
        appendThrowable(trace, new LinkageError("m"));
        appendThrowable(trace, new NegativeArraySizeException());
        appendThrowable(trace, new NegativeArraySizeException("m"));
        appendThrowable(trace, new NoClassDefFoundError());
        appendThrowable(trace, new NoClassDefFoundError("m"));
        appendThrowable(trace, new NoSuchFieldError());
        appendThrowable(trace, new NoSuchFieldError("m"));
        appendThrowable(trace, new NoSuchMethodError());
        appendThrowable(trace, new NoSuchMethodError("m"));
        appendThrowable(trace, new NullPointerException());
        appendThrowable(trace, new NullPointerException("m"));
        appendThrowable(trace, new NumberFormatException());
        appendThrowable(trace, new NumberFormatException("m"));
        appendThrowable(trace, new OutOfMemoryError());
        appendThrowable(trace, new OutOfMemoryError("m"));
        appendThrowable(trace, new RuntimeException());
        appendThrowable(trace, new SecurityException());
        appendThrowable(trace, new SecurityException("m"));
        appendThrowable(trace, new StackOverflowError());
        appendThrowable(trace, new StackOverflowError("m"));
        appendThrowable(trace, new StringIndexOutOfBoundsException());
        appendThrowable(trace, new StringIndexOutOfBoundsException(7));
        appendThrowable(trace, new StringIndexOutOfBoundsException("m"));
        appendThrowable(trace, new UnsatisfiedLinkError());
        appendThrowable(trace, new UnsatisfiedLinkError("m"));
        appendThrowable(trace, new UnsupportedOperationException());
        appendThrowable(trace, new UnsupportedOperationException("m"));
        appendThrowable(trace, new VerifyError());
        appendThrowable(trace, new VerifyError("m"));
        appendThrowable(trace, new ConcreteVirtualMachineError());
        appendThrowable(trace, new ConcreteVirtualMachineError("m"));
        appendThrowable(trace, new java.util.NoSuchElementException());
        appendThrowable(trace, new java.util.NoSuchElementException("m"));

        RuntimePermission runtimePermission =
                new RuntimePermission("loadLibrary.game");
        RuntimePermission runtimePermissionWithActions =
                new RuntimePermission("loadLibrary.game", "ignored");
        trace.append(runtimePermission.getName()).append(':')
                .append(runtimePermission.getActions()).append(':')
                .append(runtimePermissionWithActions.equals(runtimePermission))
                .append('|');
        ConcretePermission permission = new ConcretePermission("custom");
        trace.append(permission.getName()).append(':')
                .append(permission.getActions()).append(':')
                .append(permission.implies(new ConcretePermission("custom")))
                .append('|');
        java.security.AccessControlException access =
                new java.security.AccessControlException("denied");
        java.security.AccessControlException accessWithPermission =
                new java.security.AccessControlException(
                        "denied", runtimePermission);
        appendThrowable(trace, access);
        appendThrowable(trace, accessWithPermission);
        trace.append(accessWithPermission.getPermission() == runtimePermission);
        return trace.toString();
    }

    public static String printWriterSurfaceTrace() {
        try {
            ByteArrayOutputStream printBytes = new ByteArrayOutputStream();
            ExposedPrintStream stream = new ExposedPrintStream(printBytes);
            stream.print(true);
            stream.print('|');
            stream.print(1234567890123L);
            stream.print('|');
            stream.print(1.25f);
            stream.print('|');
            stream.print(-2.5d);
            stream.print('|');
            stream.print(new char[] {'A', 'B'});
            stream.println('C');
            stream.println(3.5d);
            stream.println(4.25f);
            stream.println(77L);
            stream.println((Object) Integer.valueOf(9));
            stream.println(new char[] {'X', 'Y'});
            stream.write('!');
            boolean initiallyClean = !stream.checkError();
            stream.exposeSetError();
            boolean becameDirty = stream.checkError();
            stream.close();

            ByteArrayOutputStream defaultWriterBytes =
                    new ByteArrayOutputStream();
            OutputStreamWriter defaultWriter =
                    new OutputStreamWriter(defaultWriterBytes);
            String defaultEncoding = defaultWriter.getEncoding();
            defaultWriter.write('D');
            defaultWriter.write("wxyz", 1, 2);
            defaultWriter.flush();
            defaultWriter.close();

            ByteArrayOutputStream utfWriterBytes =
                    new ByteArrayOutputStream();
            OutputStreamWriter utfWriter = new OutputStreamWriter(
                    utfWriterBytes, StandardCharsets.UTF_8);
            String utfEncoding = utfWriter.getEncoding();
            utfWriter.write('U');
            utfWriter.write("abcd", 1, 2);
            utfWriter.flush();
            utfWriter.close();

            ByteArrayOutputStream writerBytes = new ByteArrayOutputStream();
            PrintWriter writer = new PrintWriter(
                    new OutputStreamWriter(writerBytes, StandardCharsets.UTF_8));
            writer.print("left");
            writer.println();
            writer.println("right");
            writer.flush();
            boolean writerClean = !writer.checkError();
            writer.close();

            ByteArrayOutputStream autoBytes = new ByteArrayOutputStream();
            PrintWriter autoWriter = new PrintWriter(
                    new OutputStreamWriter(autoBytes, StandardCharsets.UTF_8),
                    true);
            autoWriter.println("auto");
            boolean autoClean = !autoWriter.checkError();
            autoWriter.close();

            return singleLine(printBytes.toString("UTF-8")) + "|"
                    + initiallyClean + ':' + becameDirty + '|'
                    + defaultEncoding + ':'
                    + singleLine(defaultWriterBytes.toString("UTF-8")) + '|'
                    + utfEncoding + ':'
                    + singleLine(utfWriterBytes.toString("UTF-8")) + '|'
                    + singleLine(writerBytes.toString("UTF-8")) + ':'
                    + writerClean + '|'
                    + singleLine(autoBytes.toString("UTF-8")) + ':' + autoClean;
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ':'
                    + error.getMessage();
        }
    }

    public static String readerInputSurfaceTrace() {
        try {
            StringBuilder trace = new StringBuilder();

            ExposedInputStream input = new ExposedInputStream(
                    new byte[] {10, 11, 12, 13, 14});
            byte[] first = new byte[2];
            trace.append(input.available()).append(':')
                    .append(input.read(first)).append(':')
                    .append(first[0]).append(':').append(first[1]).append(':');
            byte[] second = new byte[4];
            trace.append(input.read(second, 1, 2)).append(':')
                    .append(second[1]).append(':').append(second[2]).append(':')
                    .append(input.skip(1L)).append(':')
                    .append(input.markSupported()).append(':');
            input.mark(4);
            try {
                input.reset();
                trace.append("reset-ok");
            } catch (java.io.IOException expected) {
                trace.append("reset-ioe");
            }
            input.close();
            trace.append('|');

            ExposedFilterInputStream filter = new ExposedFilterInputStream(
                    new ByteArrayInputStream(new byte[] {1, 2, 3, 4, 5}));
            byte[] filtered = new byte[3];
            trace.append(filter.markSupported()).append(':');
            filter.mark(5);
            trace.append(filter.read()).append(':')
                    .append(filter.read(filtered)).append(':')
                    .append(filtered[0]).append(':').append(filtered[1]).append(':');
            filter.reset();
            trace.append("reset-ok");
            filter.mark(5);
            byte[] filteredRange = new byte[4];
            trace.append(':').append(filter.read(filteredRange, 1, 2))
                    .append(':').append(filteredRange[1])
                    .append(':').append(filteredRange[2])
                    .append(':').append(filter.skip(1L));
            filter.close();
            trace.append('|');

            ExposedReader reader = new ExposedReader("abcdef");
            char[] readerBuffer = new char[3];
            trace.append(reader.read()).append(':')
                    .append(reader.read(readerBuffer)).append(':')
                    .append(new String(readerBuffer)).append(':')
                    .append(reader.skip(1L)).append(':')
                    .append(reader.ready()).append(':')
                    .append(reader.markSupported()).append(':');
            try {
                reader.mark(2);
                trace.append("mark-ok");
            } catch (java.io.IOException expected) {
                trace.append("mark-ioe");
            }
            try {
                reader.reset();
                trace.append(":reset-ok");
            } catch (java.io.IOException expected) {
                trace.append(":reset-ioe");
            }
            reader.close();
            ExposedReader lockedReader = new ExposedReader("z", new Object());
            trace.append(':').append(lockedReader.read());
            lockedReader.close();
            trace.append('|');

            InputStreamReader defaultReader = new InputStreamReader(
                    new ByteArrayInputStream(new byte[] {65, 66, 67}));
            String defaultEncoding = defaultReader.getEncoding();
            trace.append(defaultEncoding).append(':')
                    .append(defaultReader.ready()).append(':')
                    .append(defaultReader.read()).append(':')
                    .append(defaultReader.skip(1L)).append(':')
                    .append(defaultReader.read()).append(':')
                    .append(defaultReader.markSupported()).append(':');
            try {
                defaultReader.mark(1);
                trace.append("mark-ok");
            } catch (java.io.IOException expected) {
                trace.append("mark-ioe");
            }
            try {
                defaultReader.reset();
                trace.append(":reset-ok");
            } catch (java.io.IOException expected) {
                trace.append(":reset-ioe");
            }
            defaultReader.close();
            InputStreamReader utfReader = new InputStreamReader(
                    new ByteArrayInputStream(new byte[] {68, 69}),
                    StandardCharsets.UTF_8);
            trace.append(':').append(utfReader.getEncoding())
                    .append(':').append(utfReader.read());
            utfReader.close();
            trace.append('|');

            BufferedReader buffered = new BufferedReader(
                    new ExposedReader("one\ntwo\n"));
            char[] bufferedChars = new char[2];
            trace.append(buffered.ready()).append(':')
                    .append(buffered.read()).append(':')
                    .append(buffered.read(bufferedChars, 0, 2)).append(':')
                    .append(new String(bufferedChars)).append(':')
                    .append(buffered.readLine()).append(':')
                    .append(buffered.readLine());
            buffered.close();
            trace.append('|');

            ByteArrayOutputStream encoded = new ByteArrayOutputStream();
            DataOutputStream dataOutput = new DataOutputStream(encoded);
            dataOutput.writeUTF("hello");
            dataOutput.writeShort(0xFEDC);
            dataOutput.write(new byte[] {9, 8, 7, 6});
            dataOutput.flush();
            DataInputStream dataInput = new DataInputStream(
                    new ByteArrayInputStream(encoded.toByteArray()));
            trace.append(DataInputStream.readUTF(dataInput)).append(':')
                    .append(dataInput.readUnsignedShort()).append(':');
            byte[] exact = new byte[2];
            dataInput.readFully(exact);
            trace.append(exact[0]).append(':').append(exact[1]).append(':');
            dataInput.mark(4);
            byte[] ranged = new byte[4];
            trace.append(dataInput.read(ranged, 1, 2)).append(':')
                    .append(ranged[1]).append(':').append(ranged[2]).append(':')
                    .append(dataInput.skip(1L)).append(':');
            dataInput.reset();
            trace.append(dataInput.read());
            dataInput.close();
            return trace.toString();
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ':'
                    + error.getMessage();
        }
    }

    public static String throwableThreadPermissionFileTrace() {
        try {
            StringBuilder trace = new StringBuilder();
            Throwable empty = new Throwable();
            Throwable message = new Throwable("message");
            Throwable cause = new Throwable("cause");
            Throwable combined = new Throwable("combined", cause);
            Throwable fromCause = new Throwable(cause);
            empty.initCause(cause);
            trace.append(empty.getLocalizedMessage()).append(':')
                    .append(empty.getCause() == cause).append(':')
                    .append(message.getLocalizedMessage()).append(':')
                    .append(combined.getCause() == cause).append(':')
                    .append(fromCause.getCause() == cause).append(':')
                    .append(message.toString()).append(':');
            try {
                empty.initCause(new Throwable("second"));
                trace.append("cause-reset-ok");
            } catch (IllegalStateException expected) {
                trace.append("cause-reset-ise");
            }
            message.printStackTrace();
            trace.append('|');

            Thread thread = new Thread("coverage-thread");
            trace.append(thread.getName()).append(':')
                    .append(thread.getPriority()).append(':');
            thread.setPriority(Thread.MAX_PRIORITY);
            thread.checkAccess();
            trace.append(thread.getPriority()).append(':')
                    .append(thread.isInterrupted()).append(':')
                    .append(Thread.interrupted()).append(':')
                    .append(Thread.activeCount() > 0).append(':')
                    .append(thread.toString().indexOf("coverage-thread") >= 0)
                    .append('|');

            ExposedBasicPermission wildcard =
                    new ExposedBasicPermission("alpha.*");
            ExposedBasicPermission exact =
                    new ExposedBasicPermission("alpha.beta", "ignored");
            java.security.PermissionCollection basicCollection =
                    wildcard.newPermissionCollection();
            basicCollection.add(wildcard);
            trace.append(wildcard.implies(exact)).append(':')
                    .append(wildcard.equals(new ExposedBasicPermission("alpha.*")))
                    .append(':').append(wildcard.getActions()).append(':')
                    .append(wildcard.hashCode()).append(':')
                    .append(basicCollection.implies(exact)).append('|');

            java.util.PropertyPermission property =
                    new java.util.PropertyPermission("java.*", "read,write");
            java.util.PropertyPermission requested =
                    new java.util.PropertyPermission("java.home", "read");
            java.security.PermissionCollection propertyCollection =
                    property.newPermissionCollection();
            propertyCollection.add(property);
            trace.append(property.implies(requested)).append(':')
                    .append(property.equals(new java.util.PropertyPermission(
                            "java.*", "write,read"))).append(':')
                    .append(property.getActions()).append(':')
                    .append(property.hashCode()).append(':')
                    .append(propertyCollection.implies(requested)).append('|');

            File file = new File("coverage-missing/child.txt");
            trace.append(file.getName()).append(':')
                    .append(file.getParent()).append(':')
                    .append(file.getPath()).append(':')
                    .append(file.isAbsolute()).append(':')
                    .append(file.lastModified()).append(':')
                    .append(file.toString());
            return trace.toString();
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ':'
                    + error.getMessage();
        }
    }

    public static String legacyUtilFullTrace() {
        StringBuilder trace = new StringBuilder();

        Vector vector = new Vector(2);
        Vector incremented = new Vector(1, 3);
        vector.addElement("b");
        vector.addElement("a");
        vector.addElement("b");
        trace.append(vector.capacity()).append('|')
                .append(vector.contains("a")).append('|')
                .append(vector.firstElement()).append('|')
                .append(vector.indexOf("b")).append('|')
                .append(vector.indexOf("b", 1)).append('|')
                .append(vector.isEmpty()).append('|')
                .append(vector.lastIndexOf("b")).append('|')
                .append(vector.lastIndexOf("b", 1)).append('|');
        Object[] vectorCopy = new Object[5];
        vector.copyInto(vectorCopy);
        vector.setElementAt("c", 1);
        vector.setSize(5);
        vector.ensureCapacity(12);
        int grownCapacity = vector.capacity();
        vector.trimToSize();
        trace.append(vectorCopy[0]).append(':').append(vectorCopy[2]).append('|')
                .append(vector.size()).append('|').append(grownCapacity).append('|')
                .append(vector.capacity()).append('|')
                .append(vector.removeElement("b")).append('|')
                .append(vector.toString()).append('|');
        vector.removeAllElements();
        incremented.addElement("x");
        incremented.addElement("y");
        trace.append(vector.isEmpty()).append('|')
                .append(incremented.capacity()).append('|');

        Hashtable table = new Hashtable(2);
        ExposedHashtable exposed = new ExposedHashtable(1, 0.75f);
        table.put("b", "2");
        table.put("a", "1");
        table.put("c", "3");
        trace.append(table.contains("2")).append('|')
                .append(table.isEmpty()).append('|')
                .append(table.toString()).append('|');
        List tableKeys = new ArrayList();
        Enumeration keyEnumeration = table.keys();
        while (keyEnumeration.hasMoreElements()) {
            tableKeys.add(keyEnumeration.nextElement());
        }
        Collections.sort(tableKeys);
        List tableValues = new ArrayList();
        Enumeration valueEnumeration = table.elements();
        while (valueEnumeration.hasMoreElements()) {
            tableValues.add(valueEnumeration.nextElement());
        }
        Collections.sort(tableValues);
        exposed.put("z", "9");
        exposed.forceRehash();
        trace.append(tableKeys.toString()).append('|')
                .append(tableValues.toString()).append('|')
                .append(exposed.get("z")).append('|');
        table.clear();
        trace.append(table.isEmpty()).append('|');

        Random defaultRandom = new Random();
        defaultRandom.setSeed(123456789L);
        Random seeded = new Random(123456789L);
        trace.append(defaultRandom.nextInt() == seeded.nextInt()).append('|')
                .append(defaultRandom.nextInt(1000) == seeded.nextInt(1000)).append('|')
                .append(defaultRandom.nextLong() == seeded.nextLong()).append('|')
                .append(defaultRandom.nextBoolean() == seeded.nextBoolean()).append('|')
                .append(Float.floatToIntBits(defaultRandom.nextFloat()))
                .append(':').append(Float.floatToIntBits(seeded.nextFloat())).append('|')
                .append(Double.doubleToLongBits(defaultRandom.nextDouble()))
                .append(':').append(Double.doubleToLongBits(seeded.nextDouble())).append('|');
        ExposedRandom exposedRandom = new ExposedRandom(77L);
        trace.append(exposedRandom.nextBits(17)).append('|');

        Date now = new Date();
        Date fixed = new Date(1234567890000L);
        Comparable dateComparable = fixed;
        trace.append(Math.abs(now.getTime() - System.currentTimeMillis()) < 5000L)
                .append('|').append(dateComparable.compareTo(new Date(1234567890001L)))
                .append('|').append(fixed.toString().length() > 0);
        return trace.toString();
    }

    public static String localTimeTrace() {
        java.time.LocalTime base = java.time.LocalTime.of(12, 34);
        java.time.LocalTime changed = base.withSecond(56).withNano(789);
        java.time.LocalTime equal = java.time.LocalTime.of(12, 34)
                .withSecond(56).withNano(789);
        java.time.LocalTime current = java.time.LocalTime.now();
        return base.toString() + "|" + changed.toString() + "|"
                + changed.equals(equal) + "|" + changed.hashCode() + "|"
                + (current != null && current.toString().length() > 0);
    }

    public static String wrapperFullTrace() {
        StringBuilder trace = new StringBuilder();
        Boolean bool = new Boolean(true);
        trace.append(bool.booleanValue()).append('|')
                .append(bool.equals(Boolean.TRUE)).append('|')
                .append(bool.hashCode()).append('|')
                .append(Boolean.parseBoolean("TrUe")).append('|')
                .append(bool.toString()).append('|')
                .append(Boolean.toString(false)).append('|')
                .append(Boolean.valueOf(false) == Boolean.FALSE).append('|');

        Byte byteValue = new Byte((byte) -12);
        trace.append(byteValue.byteValue()).append('|')
                .append(byteValue.shortValue()).append('|')
                .append(byteValue.intValue()).append('|')
                .append(byteValue.longValue()).append('|')
                .append(Float.floatToIntBits(byteValue.floatValue())).append('|')
                .append(Double.doubleToLongBits(byteValue.doubleValue())).append('|')
                .append(byteValue.equals(Byte.valueOf((byte) -12))).append('|')
                .append(byteValue.hashCode()).append('|')
                .append(Byte.parseByte("12")).append('|')
                .append(Byte.parseByte("7f", 16)).append('|')
                .append(byteValue.toString()).append('|')
                .append(Byte.toString((byte) 5)).append('|')
                .append(Byte.valueOf((byte) 6).intValue()).append('|');

        Short shortValue = new Short((short) -1234);
        trace.append(shortValue.byteValue()).append('|')
                .append(shortValue.shortValue()).append('|')
                .append(shortValue.intValue()).append('|')
                .append(shortValue.longValue()).append('|')
                .append(Float.floatToIntBits(shortValue.floatValue())).append('|')
                .append(Double.doubleToLongBits(shortValue.doubleValue())).append('|')
                .append(shortValue.equals(Short.valueOf((short) -1234))).append('|')
                .append(shortValue.hashCode()).append('|')
                .append(Short.parseShort("1234")).append('|')
                .append(Short.parseShort("7fff", 16)).append('|')
                .append(shortValue.toString()).append('|')
                .append(Short.toString((short) 7)).append('|')
                .append(Short.valueOf((short) 8).intValue()).append('|');

        Integer integer = new Integer(-123456);
        Comparable integerComparable = integer;
        trace.append(integer.byteValue()).append('|')
                .append(integer.shortValue()).append('|')
                .append(integer.intValue()).append('|')
                .append(integer.longValue()).append('|')
                .append(Float.floatToIntBits(integer.floatValue())).append('|')
                .append(Double.doubleToLongBits(integer.doubleValue())).append('|')
                .append(integer.equals(Integer.valueOf(-123456))).append('|')
                .append(integer.hashCode()).append('|')
                .append(integer.compareTo(Integer.valueOf(-123455))).append('|')
                .append(integerComparable.compareTo(Integer.valueOf(-123456))).append('|')
                .append(Integer.parseInt("123456")).append('|')
                .append(Integer.parseInt("7fffffff", 16)).append('|')
                .append(integer.toString()).append('|')
                .append(Integer.toString(10)).append('|')
                .append(Integer.toString(31, 16)).append('|')
                .append(Integer.toHexString(-1)).append('|')
                .append(Integer.toOctalString(8)).append('|')
                .append(Integer.toBinaryString(5)).append('|')
                .append(Integer.valueOf("42").intValue()).append('|')
                .append(Integer.valueOf("2a", 16).intValue()).append('|');

        Long longValue = new Long(-1234567890123L);
        Comparable longComparable = longValue;
        trace.append(longValue.byteValue()).append('|')
                .append(longValue.shortValue()).append('|')
                .append(longValue.intValue()).append('|')
                .append(longValue.longValue()).append('|')
                .append(Float.floatToIntBits(longValue.floatValue())).append('|')
                .append(Double.doubleToLongBits(longValue.doubleValue())).append('|')
                .append(longValue.equals(Long.valueOf(-1234567890123L))).append('|')
                .append(longValue.hashCode()).append('|')
                .append(longValue.compareTo(Long.valueOf(-1234567890122L))).append('|')
                .append(longComparable.compareTo(Long.valueOf(-1234567890123L))).append('|')
                .append(Long.parseLong("1234567890123")).append('|')
                .append(Long.parseLong("7fffffffffffffff", 16)).append('|')
                .append(longValue.toString()).append('|')
                .append(Long.toString(31L, 16)).append('|')
                .append(Long.toHexString(-1L)).append('|')
                .append(Long.toOctalString(8L)).append('|')
                .append(Long.toBinaryString(5L)).append('|')
                .append(Long.valueOf(9L).longValue()).append('|');

        Character character = new Character('Q');
        trace.append(character.charValue()).append('|')
                .append(character.equals(Character.valueOf('Q'))).append('|')
                .append(character.hashCode()).append('|')
                .append(Character.digit('f', 16)).append('|')
                .append(Character.isDigit('9')).append('|')
                .append(Character.isLetter('x')).append('|')
                .append(Character.isLetterOrDigit('7')).append('|')
                .append(Character.isLowerCase('x')).append('|')
                .append(Character.isUpperCase('X')).append('|')
                .append(Character.isWhitespace(' ')).append('|')
                .append(Character.toLowerCase('X')).append('|')
                .append(Character.toUpperCase('x')).append('|')
                .append(character.toString()).append('|')
                .append(Character.valueOf('R').charValue()).append('|');

        Float floatFromDouble = new Float(1.25d);
        Float floatValue = new Float(-2.5f);
        trace.append(floatValue.byteValue()).append('|')
                .append(floatValue.shortValue()).append('|')
                .append(floatValue.intValue()).append('|')
                .append(floatValue.longValue()).append('|')
                .append(Float.floatToIntBits(floatValue.floatValue())).append('|')
                .append(Double.doubleToLongBits(floatValue.doubleValue())).append('|')
                .append(floatValue.equals(Float.valueOf(-2.5f))).append('|')
                .append(floatValue.hashCode()).append('|')
                .append(Float.floatToIntBits(Float.intBitsToFloat(0x3f800000))).append('|')
                .append(floatValue.isInfinite()).append('|')
                .append(Float.isInfinite(Float.POSITIVE_INFINITY)).append('|')
                .append(floatValue.isNaN()).append('|')
                .append(Float.isNaN(Float.NaN)).append('|')
                .append(Float.floatToIntBits(Float.parseFloat("3.5"))).append('|')
                .append(floatValue.toString()).append('|')
                .append(Float.toString(4.5f)).append('|')
                .append(Float.floatToIntBits(Float.valueOf(5.5f).floatValue())).append('|')
                .append(Float.floatToIntBits(Float.valueOf("6.5").floatValue())).append('|')
                .append(Float.floatToIntBits(floatFromDouble.floatValue())).append('|');

        Double doubleValue = new Double(-2.5d);
        trace.append(doubleValue.byteValue()).append('|')
                .append(doubleValue.shortValue()).append('|')
                .append(doubleValue.intValue()).append('|')
                .append(doubleValue.longValue()).append('|')
                .append(Float.floatToIntBits(doubleValue.floatValue())).append('|')
                .append(Double.doubleToLongBits(doubleValue.doubleValue())).append('|')
                .append(doubleValue.equals(Double.valueOf(-2.5d))).append('|')
                .append(doubleValue.hashCode()).append('|')
                .append(Double.doubleToLongBits(Double.longBitsToDouble(0x3ff0000000000000L))).append('|')
                .append(doubleValue.isInfinite()).append('|')
                .append(Double.isInfinite(Double.POSITIVE_INFINITY)).append('|')
                .append(doubleValue.isNaN()).append('|')
                .append(Double.isNaN(Double.NaN)).append('|')
                .append(Double.doubleToLongBits(Double.parseDouble("3.5"))).append('|')
                .append(doubleValue.toString()).append('|')
                .append(Double.toString(4.5d)).append('|')
                .append(Double.doubleToLongBits(Double.valueOf(5.5d).doubleValue())).append('|')
                .append(Double.doubleToLongBits(Double.valueOf("6.5").doubleValue()));
        return trace.toString();
    }

    public static String mathFullTrace() {
        StringBuilder trace = new StringBuilder();
        trace.append(Double.doubleToLongBits(Math.IEEEremainder(7.0, 2.0))).append('|')
                .append(Double.doubleToLongBits(Math.abs(-2.5d))).append('|')
                .append(Float.floatToIntBits(Math.abs(-2.5f))).append('|')
                .append(Math.abs(-7)).append('|')
                .append(Double.doubleToLongBits(Math.acos(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.asin(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.atan(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.atan2(1.0d, -1.0d))).append('|')
                .append(Double.doubleToLongBits(Math.cos(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.exp(1.0d))).append('|')
                .append(Double.doubleToLongBits(Math.log(2.0d))).append('|')
                .append(Float.floatToIntBits(Math.max(-0.0f, 0.0f))).append('|')
                .append(Math.max(-7, 3)).append('|')
                .append(Math.max(-7L, 3L)).append('|')
                .append(Float.floatToIntBits(Math.min(-0.0f, 0.0f))).append('|')
                .append(Math.min(-7, 3)).append('|')
                .append(Math.min(-7L, 3L)).append('|')
                .append(Double.doubleToLongBits(Math.pow(2.0d, 10.0d))).append('|');
        double random = Math.random();
        trace.append(random >= 0.0d && random < 1.0d).append('|')
                .append(Double.doubleToLongBits(Math.rint(2.5d))).append('|')
                .append(Math.round(2.5f)).append('|')
                .append(Double.doubleToLongBits(Math.sin(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.tan(0.5d))).append('|')
                .append(Double.doubleToLongBits(Math.toDegrees(Math.PI))).append('|')
                .append(Double.doubleToLongBits(Math.toRadians(180.0d)));
        return trace.toString();
    }

    public static String stringApiTrace() {
        try {
            String empty = new String();
            String copied = new String("copy");
            String fromBuffer = new String(new StringBuffer("buffer"));
            char[] chars = new char[] {'a', 'b', 'c', 'd'};
            String fromChars = new String(chars);
            String fromCharsRange = new String(chars, 1, 2);
            byte[] bytes = new byte[] {65, 66, 67, 68};
            String fromBytes = new String(bytes);
            String fromBytesRange = new String(bytes, 1, 2);
            String fromCharset = new String(bytes, "UTF-8");
            String fromCharsetRange = new String(bytes, 1, 2, "UTF-8");
            String value = "AbcaBCabc";
            char[] destination = new char[5];
            value.getChars(1, 4, destination, 1);
            String[] split = "a,b,,c".split(",");
            char[] array = value.toCharArray();
            byte[] plainBytes = value.getBytes();
            byte[] utf8Bytes = value.getBytes("UTF-8");
            Comparable comparable = value;
            StringBuilder trace = new StringBuilder();
            trace.append(empty.length()).append('|')
                    .append(copied).append('|').append(fromBuffer).append('|')
                    .append(fromChars).append('|').append(fromCharsRange).append('|')
                    .append(fromBytes).append('|').append(fromBytesRange).append('|')
                    .append(fromCharset).append('|').append(fromCharsetRange).append('|')
                    .append(value.charAt(2)).append('|')
                    .append(value.compareTo("AbcaBCabd")).append('|')
                    .append(comparable.compareTo("AbcaBCabc")).append('|')
                    .append(value.concat("!")).append('|')
                    .append(value.endsWith("abc")).append('|')
                    .append(value.equals(new String(value))).append('|')
                    .append(value.equalsIgnoreCase("ABCABCABC")).append('|')
                    .append(plainBytes.length).append(':').append(plainBytes[0]).append('|')
                    .append(utf8Bytes.length).append(':').append(utf8Bytes[8]).append('|')
                    .append(destination[0]).append(destination[1]).append(destination[2])
                    .append(destination[3]).append(destination[4]).append('|')
                    .append(value.hashCode()).append('|')
                    .append(value.indexOf('B')).append('|')
                    .append(value.indexOf('B', 3)).append('|')
                    .append(value.indexOf("BC")).append('|')
                    .append(value.indexOf("abc", 3)).append('|')
                    .append(value.intern() == value).append('|')
                    .append(empty.isEmpty()).append('|')
                    .append(value.lastIndexOf('a')).append('|')
                    .append(value.lastIndexOf('a', 5)).append('|')
                    .append(value.lastIndexOf("BC")).append('|')
                    .append(value.lastIndexOf("abc", 7)).append('|')
                    .append(value.length()).append('|')
                    .append(value.regionMatches(true, 0, "abc", 0, 3)).append('|')
                    .append(value.replace('a', 'x')).append('|')
                    .append(split.length).append(':').append(split[2]).append('|')
                    .append(value.startsWith("Abc")).append('|')
                    .append(value.startsWith("BC", 4)).append('|')
                    .append(value.substring(4)).append('|')
                    .append(value.substring(1, 4)).append('|')
                    .append(array.length).append(':').append(array[8]).append('|')
                    .append(value.toLowerCase()).append('|')
                    .append(value.toString()).append('|')
                    .append(value.toUpperCase()).append('|')
                    .append("  x  ".trim()).append('|')
                    .append(String.valueOf('Q')).append('|')
                    .append(String.valueOf(1.25d)).append('|')
                    .append(String.valueOf(2.5f)).append('|')
                    .append(String.valueOf(123)).append('|')
                    .append(String.valueOf(456L)).append('|')
                    .append(String.valueOf((Object) null)).append('|')
                    .append(String.valueOf(true)).append('|')
                    .append(String.valueOf(new char[] {'x', 'y'})).append('|')
                    .append(String.valueOf(new char[] {'w', 'x', 'y', 'z'}, 1, 2));
            return trace.toString();
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ":" + error.getMessage();
        }
    }

    public static String stringBuilderTrace() {
        StringBuilder empty = new StringBuilder();
        StringBuilder sized = new StringBuilder(2);
        StringBuilder builder = new StringBuilder("A");
        char[] chars = new char[] {'x', 'y', 'z'};
        builder.append(true).append('B').append(12).append(34L)
                .append(1.5f).append(2.25d).append((Object) "O")
                .append("S").append(chars).append(chars, 1, 2);
        int beforeCapacity = builder.capacity();
        builder.ensureCapacity(beforeCapacity + 20);
        int afterCapacity = builder.capacity();
        char first = builder.charAt(0);
        char[] copied = new char[3];
        builder.getChars(0, 3, copied, 0);
        builder.insert(0, true).insert(0, 'C').insert(0, 7)
                .insert(0, 8L).insert(0, 1.25f).insert(0, 2.5d)
                .insert(0, (Object) "P").insert(0, "Q")
                .insert(0, new char[] {'R', 'S'});
        builder.delete(0, 2);
        builder.deleteCharAt(0);
        builder.setCharAt(0, 'T');
        int lengthBefore = builder.length();
        builder.setLength(lengthBefore + 2);
        int lengthExpanded = builder.length();
        builder.setLength(lengthBefore);
        builder.reverse();
        return empty.length() + "|" + sized.capacity() + "|"
                + beforeCapacity + "|" + afterCapacity + "|" + first + "|"
                + new String(copied) + "|" + lengthExpanded + "|"
                + builder.toString();
    }

    public static String stringBufferExtendedTrace() {
        StringBuffer empty = new StringBuffer();
        StringBuffer sized = new StringBuffer(2);
        StringBuffer buffer = new StringBuffer("A");
        char[] chars = new char[] {'x', 'y', 'z'};
        buffer.append(true).append('B').append(12).append(34L)
                .append(1.5f).append(2.25d).append((Object) "O")
                .append("S").append(chars).append(chars, 1, 2);
        int beforeCapacity = buffer.capacity();
        buffer.ensureCapacity(beforeCapacity + 20);
        int afterCapacity = buffer.capacity();
        char first = buffer.charAt(0);
        char[] copied = new char[3];
        buffer.getChars(0, 3, copied, 0);
        buffer.insert(0, true).insert(0, 'C').insert(0, 7)
                .insert(0, 8L).insert(0, 1.25f).insert(0, 2.5d)
                .insert(0, (Object) "P").insert(0, "Q")
                .insert(0, new char[] {'R', 'S'});
        buffer.delete(0, 2);
        buffer.deleteCharAt(0);
        buffer.setCharAt(0, 'T');
        int lengthBefore = buffer.length();
        buffer.setLength(lengthBefore + 2);
        int lengthExpanded = buffer.length();
        buffer.setLength(lengthBefore);
        buffer.reverse();
        return empty.length() + "|" + sized.capacity() + "|"
                + beforeCapacity + "|" + afterCapacity + "|" + first + "|"
                + new String(copied) + "|" + lengthExpanded + "|"
                + buffer.toString();
    }

    public static String headlessCollectionsTrace() {
        StringBuffer trace = new StringBuffer();

        ArrayList list = new ArrayList();
        trace.append(list.isEmpty()).append(':');
        list.add("b");
        list.add("a");
        list.add("b");
        list.add(1, "c");
        trace.append(list.size()).append(':')
                .append(list.contains("a")).append(':')
                .append(list.indexOf("b")).append(':')
                .append(list.lastIndexOf("b")).append(':')
                .append(list.get(1)).append(':')
                .append(list.set(0, "d")).append(':')
                .append(list.toString()).append(':');

        ArrayList copy = new ArrayList(list);
        copy.addAll(Collections.singletonList("e"));
        Iterator iterator = copy.iterator();
        while (iterator.hasNext()) {
            trace.append(iterator.next());
        }
        try {
            iterator.remove();
            trace.append("remove-ok");
        } catch (UnsupportedOperationException expected) {
            trace.append("remove-uoe");
        }
        Object[] copied = copy.toArray();
        trace.append(':').append(copied.length).append(':')
                .append(copy.remove(1)).append(':')
                .append(copy.remove("b")).append(':');
        Collections.sort(copy);
        Collections.reverse(copy);
        Collections.swap(copy, 0, copy.size() - 1);
        Collections.shuffle(copy);
        Collections.sort(copy);
        trace.append(copy.toString()).append(':');
        Collections.fill(copy, "x");
        trace.append(copy.toString()).append(':')
                .append(Collections.emptyList().size()).append(':')
                .append(Collections.singletonList("s").get(0)).append(':');
        copy.clear();
        trace.append(copy.isEmpty()).append(':');

        HashMap map = new HashMap();
        trace.append(map.isEmpty()).append(':')
                .append(map.put("a", "1")).append(':')
                .append(map.put(null, "n")).append(':')
                .append(map.put("a", "2")).append(':')
                .append(map.size()).append(':')
                .append(map.containsKey(null)).append(':')
                .append(map.containsValue("2")).append(':')
                .append(map.get("a")).append(':');
        HashMap second = new HashMap(2);
        second.put("b", "3");
        second.putAll(map);
        HashMap third = new HashMap(second);
        List keys = new ArrayList(third.keySet());
        keys.remove(null);
        Collections.sort(keys);
        List values = new ArrayList(third.values());
        Collections.sort(values);
        trace.append(keys.toString()).append(':')
                .append(values.toString()).append(':')
                .append(third.remove("b")).append(':');
        third.clear();
        trace.append(third.isEmpty()).append(':');

        HashSet set = new HashSet();
        trace.append(set.isEmpty()).append(':')
                .append(set.add("b")).append(':')
                .append(set.add("b")).append(':')
                .append(set.add(null)).append(':')
                .append(set.contains(null)).append(':')
                .append(set.size()).append(':');
        HashSet sized = new HashSet(2);
        sized.addAll(set);
        HashSet cloned = new HashSet(sized);
        List setValues = new ArrayList(cloned);
        setValues.remove(null);
        Collections.sort(setValues);
        trace.append(setValues.toString()).append(':')
                .append(cloned.toArray().length).append(':')
                .append(cloned.remove("b")).append(':');
        cloned.clear();
        trace.append(cloned.isEmpty());
        return trace.toString();
    }

    public static String headlessArraysTrace() {
        StringBuffer trace = new StringBuffer();
        byte[] bytes = new byte[] {3, 1, 2};
        byte[] sameBytes = new byte[] {3, 1, 2};
        int[] ints = new int[] {9, 3, 7, 1};
        int[] sameInts = new int[] {9, 3, 7, 1};
        Object[] objects = new Object[] {"a", null, "c"};
        Object[] sameObjects = new Object[] {"a", null, "c"};
        trace.append(Arrays.equals(bytes, sameBytes)).append(':')
                .append(Arrays.equals(ints, sameInts)).append(':')
                .append(Arrays.equals(objects, sameObjects)).append(':');
        Arrays.fill(bytes, (byte) 4);
        Arrays.fill(ints, 6);
        Arrays.fill(objects, "z");
        trace.append(bytes[1]).append(':').append(ints[2]).append(':')
                .append(objects[0]).append(':');
        byte[] byteCopy = Arrays.copyOf(bytes, 5);
        int[] intCopy = Arrays.copyOf(ints, 2);
        Object[] objectCopy = Arrays.copyOf(objects, 4);
        byte[] byteRange = Arrays.copyOfRange(byteCopy, 1, 4);
        int[] intRange = Arrays.copyOfRange(new int[] {0, 1, 2, 3}, 1, 3);
        trace.append(byteCopy.length).append(':').append(byteCopy[4]).append(':')
                .append(intCopy.length).append(':').append(objectCopy[3]).append(':')
                .append(byteRange.length).append(':').append(intRange[0]).append(':');
        byte[] sortableBytes = new byte[] {5, -1, 3};
        int[] sortableInts = new int[] {5, -1, 3, 7};
        Arrays.sort(sortableBytes);
        Arrays.sort(sortableInts);
        trace.append(sortableBytes[0]).append(':').append(sortableBytes[2]).append(':')
                .append(sortableInts[0]).append(':').append(sortableInts[3]).append(':')
                .append(Arrays.binarySearch(sortableInts, 5)).append(':')
                .append(Arrays.binarySearch(sortableInts, 4)).append(':');
        List arrayList = Arrays.asList(new Object[] {"q", "w"});
        trace.append(arrayList.size()).append(':').append(arrayList.get(1));
        return trace.toString();
    }

    public static String headlessBase64ObjectsTrace() {
        byte[] input = new byte[] {0, 1, 2, 3, 4, 127, -1};
        Base64.Encoder encoder = Base64.getEncoder();
        Base64.Decoder decoder = Base64.getDecoder();
        byte[] encoded = encoder.encode(input);
        String encodedText = encoder.encodeToString(input);
        byte[] decodedBytes = decoder.decode(encoded);
        byte[] decodedText = decoder.decode(encodedText);
        StringBuffer trace = new StringBuffer();
        trace.append(encodedText).append(':')
                .append(Arrays.equals(input, decodedBytes)).append(':')
                .append(Arrays.equals(input, decodedText)).append(':')
                .append(Objects.equals("a", new String("a"))).append(':')
                .append(Objects.equals(null, null)).append(':')
                .append(Objects.hashCode(null)).append(':')
                .append(Objects.toString(null)).append(':')
                .append(Objects.toString(null, "fallback")).append(':')
                .append(Objects.requireNonNull("ok")).append(':')
                .append(Objects.requireNonNull("ok2", "message"));
        try {
            Objects.requireNonNull(null, "missing");
            trace.append(":no-error");
        } catch (NullPointerException expected) {
            trace.append(':').append(expected.getMessage());
        }
        return trace.toString();
    }

    public static String headlessIoTrace() {
        try {
            ByteArrayOutputStream raw = new ByteArrayOutputStream(2);
            BufferedOutputStream buffered = new BufferedOutputStream(raw, 8);
            DataOutputStream output = new DataOutputStream(buffered);
            output.writeBoolean(true);
            output.writeByte(0xFE);
            output.writeShort(0x8123);
            output.writeChar('Z');
            output.writeInt(0x12345678);
            output.writeLong(0x1020304050607080L);
            output.writeFloat(1.5f);
            output.writeDouble(-2.25);
            output.writeBytes("AB");
            output.writeChars("CD");
            output.writeUTF("Hi\u0000");
            output.write(new byte[] {9, 8, 7});
            output.write(new byte[] {6, 5, 4}, 1, 2);
            output.write(3);
            int written = output.size();
            output.flush();
            buffered.flush();
            output.close();

            byte[] payload = raw.toByteArray();
            ByteArrayInputStream byteInput = new ByteArrayInputStream(
                    payload, 0, payload.length);
            BufferedInputStream bufferedInput = new BufferedInputStream(byteInput, 7);
            DataInputStream input = new DataInputStream(bufferedInput);
            StringBuffer trace = new StringBuffer();
            trace.append(written).append(':')
                    .append(input.readBoolean()).append(':')
                    .append(input.readUnsignedByte()).append(':')
                    .append(input.readShort()).append(':')
                    .append(input.readChar()).append(':')
                    .append(input.readInt()).append(':')
                    .append(input.readLong()).append(':')
                    .append(Float.floatToIntBits(input.readFloat())).append(':')
                    .append(Double.doubleToLongBits(input.readDouble())).append(':');
            byte[] ascii = new byte[6];
            input.readFully(ascii, 0, 6);
            trace.append(ascii[0]).append(':').append(ascii[5]).append(':')
                    .append(input.readUTF()).append(':');
            byte[] tail = new byte[4];
            int read = input.read(tail);
            trace.append(read).append(':').append(tail[0]).append(':')
                    .append(input.skipBytes(1)).append(':')
                    .append(input.read()).append(':')
                    .append(input.available()).append(':')
                    .append(input.markSupported());
            input.close();

            ByteArrayInputStream state = new ByteArrayInputStream(
                    new byte[] {1, 2, 3, 4});
            trace.append(':').append(state.available());
            state.mark(4);
            trace.append(':').append(state.read());
            byte[] pair = new byte[2];
            trace.append(':').append(state.read(pair, 0, 2));
            state.reset();
            trace.append(':').append(state.skip(2)).append(':')
                    .append(state.read()).append(':').append(state.markSupported());
            state.close();

            ByteArrayOutputStream copy = new ByteArrayOutputStream();
            copy.write(65);
            copy.write(new byte[] {66, 67});
            copy.write(new byte[] {68, 69, 70}, 1, 2);
            ByteArrayOutputStream destination = new ByteArrayOutputStream();
            copy.writeTo(destination);
            trace.append(':').append(copy.size()).append(':')
                    .append(copy.toString()).append(':')
                    .append(destination.toByteArray().length);
            copy.reset();
            trace.append(':').append(copy.size());
            copy.close();
            return trace.toString();
        } catch (Exception error) {
            return "ERR:" + error.getClass().getName() + ":" + error.getMessage();
        }
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
