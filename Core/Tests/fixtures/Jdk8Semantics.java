package corefixture;

public final class Jdk8Semantics {
    private static final int INT_CONSTANT = 123;
    private static final long LONG_CONSTANT = 7L;
    private static final float FLOAT_CONSTANT = 2.0F;
    private static final double DOUBLE_CONSTANT = 3.0;
    private static final String TEXT_CONSTANT = "Việt";
    private static final Jdk8Semantics REFLECTED_INSTANCE =
        new Jdk8Semantics();

    public static final class Initializer {
        private static int state;

        private static int next(int digit) {
            state = state * 10 + digit;
            return state;
        }
    }

    public interface ParentDefault {
        int PARENT_INITIALIZED = Initializer.next(1);

        default int parentValue() {
            return PARENT_INITIALIZED;
        }
    }

    public interface ChildDefault extends ParentDefault {
        int CHILD_INITIALIZED = Initializer.next(2);

        default int childValue() {
            return CHILD_INITIALIZED;
        }
    }

    public static final class DefaultImpl implements ChildDefault {
    }

    public static final class PrintableValue {
        public String toString() {
            return "custom";
        }
    }

    public static final class ReflectTarget {
        public int value;

        public ReflectTarget() {
            value = 37;
        }
    }

    public abstract static class AbstractReflect {
        public AbstractReflect() {
        }
    }

    public static final class PrivateReflect {
        private PrivateReflect() {
        }
    }

    public static final class NoDefaultReflect {
        public NoDefaultReflect(int value) {
        }
    }

    public static final class CloneTarget implements Cloneable {
        public int value = 19;
        public int[] shared = {4, 5};

        public CloneTarget copy() throws CloneNotSupportedException {
            return (CloneTarget) super.clone();
        }
    }

    public static final class NonCloneTarget {
        public Object copy() throws CloneNotSupportedException {
            return super.clone();
        }
    }

    public static final class EqualKey {
        private final int value;

        EqualKey(int value) {
            this.value = value;
        }

        public boolean equals(Object other) {
            return other instanceof EqualKey
                && ((EqualKey) other).value == value;
        }

        public int hashCode() {
            return value;
        }
    }

    private enum CompatColor {
        RED,
        GREEN,
        BLUE
    }

    private Jdk8Semantics() {
    }

    public static int constantValues() {
        return INT_CONSTANT
            + (int) LONG_CONSTANT
            + (int) FLOAT_CONSTANT
            + (int) DOUBLE_CONSTANT
            + TEXT_CONSTANT.length();
    }

    public static int defaultInterfaceInitializationOrder() {
        new DefaultImpl();
        return Initializer.state;
    }

    public static int javacStringConcat() {
        String value = "Level " + 12 + ':' + 3L + ':' + true + ':' + 1.5F;
        return value.equals("Level 12:3:true:1.5") ? value.length() : -1;
    }

    public static int stringBufferOperations() {
        StringBuffer buffer = new StringBuffer("A");
        buffer.append(12).append('Z').append(false);
        return buffer.toString().equals("A12Zfalse") ? buffer.length() : -1;
    }

    public static int stringBufferExtendedOperations() {
        int result = 0;
        StringBuffer buffer = new StringBuffer("abcdef");
        buffer.ensureCapacity(128);
        buffer.delete(1, 3).deleteCharAt(1);
        if (buffer.toString().equals("aef")) result |= 1;
        buffer.insert(1, "BC").insert(3, 'X').insert(4, 12);
        if (buffer.toString().equals("aBCX12ef")) result |= 2;
        buffer.setCharAt(0, 'A');
        char[] copied = new char[4];
        buffer.getChars(0, 4, copied, 0);
        if (new String(copied).equals("ABCX")) result |= 4;
        buffer.append(new char[] {'Y', 'Z'});
        buffer.append(new char[] {'0', '1', '2'}, 1, 2);
        if (buffer.toString().endsWith("YZ12")) result |= 8;
        StringBuffer reversed = new StringBuffer("A\ud83d\ude00B").reverse();
        if (reversed.toString().equals("B\ud83d\ude00A")) result |= 16;
        String copiedBuffer = new String(new StringBuffer("copied"));
        if (copiedBuffer.equals("copied")) result |= 32;
        return result;
    }

    public static int multiArrayDefaults() {
        int[][] integers = new int[2][3];
        long[][] longs = new long[1][2];
        float[][] floats = new float[1][1];
        Object[][] objects = new Object[1][1];
        return integers[1][2] == 0 && longs[0][1] == 0L
                && floats[0][0] == 0.0F && objects[0][0] == null ? 1 : 0;
    }

    public static int builderObjectAppend() {
        Object value = TEXT_CONSTANT;
        String result = new StringBuilder().append(value).append((Object) null)
            .toString();
        return result.equals("Việtnull") ? result.length() : -1;
    }

    public static int arraycopyOverlap() {
        int[] values = {1, 2, 3, 4};
        System.arraycopy(values, 0, values, 1, 3);
        return values[0] * 1000 + values[1] * 100
            + values[2] * 10 + values[3];
    }

    public static int arraycopyReferences() {
        Object[] source = {"A", "Việt"};
        String[] destination = new String[2];
        System.arraycopy(source, 0, destination, 0, source.length);
        return destination[0].length() + destination[1].length();
    }

    public static int arraycopyExceptions() {
        int result = 0;
        try {
            System.arraycopy(new int[1], 0, new long[1], 0, 1);
        } catch (ArrayStoreException expected) {
            result += 10;
        }
        try {
            System.arraycopy(new int[1], 1, new int[1], 0, 1);
        } catch (ArrayIndexOutOfBoundsException expected) {
            result += 20;
        }
        try {
            System.arraycopy(null, 0, new int[1], 0, 1);
        } catch (NullPointerException expected) {
            result += 30;
        }
        return result;
    }

    public static int stringApi() {
        char[] source = {'x', 'V', 'i', 'ệ', 't', 'y'};
        String value = new String(source, 1, 4);
        char[] copied = value.toCharArray();
        char[] destination = {'_', '_', '_', '_', '_'};
        value.getChars(1, 3, destination, 2);
        int result = 0;
        if (value.equals("Việt")) result |= 1;
        if (value.startsWith("Vi")) result |= 2;
        if (value.endsWith("ệt")) result |= 4;
        if (value.indexOf('ệ') == 2) result |= 8;
        if (value.lastIndexOf("i") == 1) result |= 16;
        if (value.substring(1, 3).equals("iệ")) result |= 32;
        if (value.concat("!").equals("Việt!")) result |= 64;
        if ("  game \n".trim().equals("game")) result |= 128;
        if ("banana".replace('a', 'o').equals("bonono")) result |= 256;
        if (String.valueOf(123).equals("123")) result |= 512;
        if ("GAME".equalsIgnoreCase("game")) result |= 1024;
        if (copied.length == 4 && copied[2] == 'ệ'
                && destination[2] == 'i' && destination[3] == 'ệ') {
            result |= 2048;
        }
        if ("MiDp-2.0".toLowerCase().equals("midp-2.0")
                && "MiDp-2.0".toUpperCase().equals("MIDP-2.0")
                && "already".toLowerCase() == "already") {
            result |= 4096;
        }
        if ("xxGaMezz".regionMatches(true, 2, "GAME", 0, 4)
                && !"short".regionMatches(false, 2, "longer", 0, 6)) {
            result |= 8192;
        }
        return result;
    }

    public static int nativeStringExceptions() {
        int result = 0;
        try {
            "x".charAt(2);
        } catch (StringIndexOutOfBoundsException expected) {
            result += 1;
        }
        try {
            "x".substring(-1);
        } catch (StringIndexOutOfBoundsException expected) {
            result += 2;
        }
        try {
            java.util.Objects.requireNonNull(null);
        } catch (NullPointerException expected) {
            result += 4;
        }
        try {
            new StringBuilder(-1);
        } catch (NegativeArraySizeException expected) {
            result += 8;
        }
        return result;
    }

    public static int wrapperApi() {
        int result = 0;
        Integer integer = 123;
        Long longValue = 9L;
        Byte byteValue = (byte) -7;
        Short shortValue = (short) 300;
        Boolean booleanValue = true;
        Character character = 'G';
        Float floatValue = 1.5F;
        Double doubleValue = 2.5;
        if (integer.intValue() == 123 && integer.equals(Integer.valueOf(123))) {
            result |= 1;
        }
        if (Integer.parseInt("-7") == -7
                && Integer.toHexString(-1).equals("ffffffff")) {
            result |= 2;
        }
        if (longValue.longValue() == 9L
                && Long.parseLong("7f", 16) == 127L) {
            result |= 4;
        }
        if (byteValue.intValue() == -7 && shortValue.intValue() == 300) {
            result |= 8;
        }
        if (booleanValue.booleanValue() && Boolean.parseBoolean("TRUE")) {
            result |= 16;
        }
        if (character.charValue() == 'G' && Character.toLowerCase('G') == 'g'
                && Character.isLetterOrDigit('7')) {
            result |= 32;
        }
        if (floatValue.doubleValue() == 1.5
                && Float.parseFloat("2.25") == 2.25F
                && Float.valueOf("1.75").floatValue() == 1.75F) {
            result |= 64;
        }
        if (doubleValue.intValue() == 2
                && Double.parseDouble("3.5") == 3.5
                && Double.valueOf("4.25").doubleValue() == 4.25) {
            result |= 128;
        }
        if (integer.toString().equals("123")
                && longValue.toString().equals("9")
                && character.toString().equals("G")) {
            result |= 256;
        }
        if (Boolean.TRUE.booleanValue()
                && !Boolean.FALSE.booleanValue()
                && Boolean.valueOf(true) == Boolean.TRUE
                && Boolean.valueOf(false) == Boolean.FALSE
                && booleanValue == Boolean.TRUE) {
            result |= 512;
        }
        if (Integer.valueOf("321").intValue() == 321
                && Character.digit('F', 16) == 15
                && Character.digit('z', 36) == 35
                && Character.digit('9', 8) == -1) {
            result |= 1024;
        }
        Object integerObject = integer;
        Object booleanObject = booleanValue;
        Object printableObject = new PrintableValue();
        String appended = new StringBuilder()
                .append(integerObject)
                .append(',')
                .append(booleanObject)
                .append(',')
                .append(printableObject)
                .toString();
        if (String.valueOf(integerObject).equals("123")
                && String.valueOf(booleanObject).equals("true")
                && String.valueOf(printableObject).equals("custom")
                && appended.equals("123,true,custom")) {
            result |= 2048;
        }
        return result;
    }

    public static int wrapperExceptions() {
        int result = 0;
        try {
            Integer.parseInt("999999999999");
        } catch (NumberFormatException expected) {
            result += 1;
        }
        try {
            Byte.parseByte("128");
        } catch (NumberFormatException expected) {
            result += 2;
        }
        try {
            Long.parseLong("x", 10);
        } catch (NumberFormatException expected) {
            result += 4;
        }
        try {
            Double.parseDouble("broken");
        } catch (NumberFormatException expected) {
            result += 8;
        }
        return result;
    }

    public static int throwableSuppressedApi() {
        int result = 0;
        Throwable primary = new Exception("primary");
        Throwable first = new java.io.IOException("first");
        Throwable second = new IllegalStateException("second");
        primary.addSuppressed(first);
        primary.addSuppressed(second);
        Throwable[] suppressed = primary.getSuppressed();
        if (suppressed.length == 2
                && suppressed[0] == first
                && suppressed[1] == second) result |= 1;
        suppressed[0] = second;
        if (primary.getSuppressed()[0] == first) result |= 2;
        try {
            primary.addSuppressed(null);
        } catch (NullPointerException expected) {
            result |= 4;
        }
        try {
            primary.addSuppressed(primary);
        } catch (IllegalArgumentException expected) {
            result |= 8;
        }
        return result;
    }

    public static int mathApi() {
        int result = 0;
        if (Math.abs(Integer.MIN_VALUE) == Integer.MIN_VALUE
                && Math.abs(-7) == 7) result |= 1;
        if (Math.min(5, -2) == -2 && Math.max(5L, 9L) == 9L) result |= 2;
        if (Double.doubleToLongBits(Math.min(0.0, -0.0))
                == Double.doubleToLongBits(-0.0)
                && Double.doubleToLongBits(Math.max(-0.0, 0.0))
                == Double.doubleToLongBits(0.0)) result |= 4;
        if (Math.round(1.5F) == 2 && Math.round(-1.5) == -1L) result |= 8;
        if (Math.sqrt(81.0) == 9.0 && Math.pow(2.0, 5.0) == 32.0) result |= 16;
        if (Math.sin(0.0) == 0.0 && Math.cos(0.0) == 1.0) result |= 32;
        if (Math.round(Math.toDegrees(Math.toRadians(90.0))) == 90L) result |= 64;
        double random = Math.random();
        if (random >= 0.0 && random < 1.0) result |= 128;
        return result;
    }

    public static int utilApi() {
        int result = 0;
        java.util.Vector vector = new java.util.Vector(1);
        vector.addElement("A");
        vector.addElement("C");
        vector.insertElementAt("B", 1);
        if (vector.size() == 3 && vector.capacity() >= 3
                && vector.elementAt(1).equals("B")
                && vector.indexOf("C") == 2) result |= 1;
        vector.removeElement("B");
        java.util.Enumeration enumeration = vector.elements();
        String joined = "";
        while (enumeration.hasMoreElements()) {
            joined = joined + enumeration.nextElement();
        }
        if (joined.equals("AC") && vector.lastElement().equals("C")) result |= 2;

        java.util.Stack stack = new java.util.Stack();
        stack.push("first");
        stack.push("second");
        if (stack.search("first") == 2 && stack.peek().equals("second")
                && stack.pop().equals("second") && !stack.empty()) result |= 4;

        java.util.Hashtable table = new java.util.Hashtable(1);
        if (table.put("a", "1") == null
                && table.put("b", "2") == null
                && table.put("a", "3").equals("1")
                && table.size() == 2
                && table.get("a").equals("3")
                && table.containsKey("b")
                && table.contains("2")) result |= 8;
        java.util.Enumeration keys = table.keys();
        int keyCount = 0;
        while (keys.hasMoreElements()) {
            if (keys.nextElement() != null) keyCount++;
        }
        if (keyCount == 2 && table.remove("b").equals("2")
                && table.size() == 1) result |= 16;

        java.util.Random first = new java.util.Random(123456L);
        java.util.Random second = new java.util.Random(123456L);
        if (first.nextInt() == second.nextInt()
                && first.nextInt(1000) == second.nextInt(1000)
                && first.nextLong() == second.nextLong()
                && first.nextBoolean() == second.nextBoolean()
                && first.nextFloat() == second.nextFloat()
                && first.nextDouble() == second.nextDouble()) result |= 32;

        EqualKey stored = new EqualKey(7);
        EqualKey equivalent = new EqualKey(7);
        java.util.Vector customVector = new java.util.Vector();
        customVector.addElement(stored);
        java.util.Hashtable customTable = new java.util.Hashtable();
        customTable.put(stored, "custom");
        if (customVector.contains(equivalent)
                && customVector.indexOf(equivalent) == 0
                && "custom".equals(customTable.get(equivalent))
                && customTable.containsKey(equivalent)) result |= 64;

        java.util.Vector resized = new java.util.Vector(1);
        resized.addElement("one");
        resized.setSize(3);
        Object[] copied = new Object[3];
        resized.copyInto(copied);
        resized.setSize(1);
        resized.ensureCapacity(8);
        resized.trimToSize();

        java.util.List modernList = new java.util.ArrayList(1);
        java.time.LocalTime noon = java.time.LocalTime.of(12, 30);
        modernList.add(noon);
        modernList.add(null);
        Object replaced = modernList.set(1, "tail");
        modernList.add(1, "middle");
        boolean removedMiddle = modernList.remove("middle");
        Object removedTail = modernList.remove(1);
        java.time.LocalTime normalized = java.time.LocalTime.of(12, 30)
            .withSecond(0).withNano(0);
        if (copied[0].equals("one") && copied[1] == null
                && copied[2] == null && resized.size() == 1
                && resized.capacity() == 1
                && replaced == null && removedMiddle
                && removedTail.equals("tail")
                && modernList.size() == 1
                && modernList.contains(normalized)
                && modernList.get(0).equals(normalized)) result |= 128;
        return result;
    }

    public static int arrayDequeApi() {
        int result = 0;
        java.util.Deque deque = new java.util.ArrayDeque(1);
        deque.addLast("b");
        deque.addFirst("a");
        if (deque.offerLast("c") && deque.size() == 3
                && deque.getFirst().equals("a")
                && deque.getLast().equals("c")) result |= 1;

        if (deque.removeFirst().equals("a")
                && deque.removeLast().equals("c")
                && deque.peek().equals("b")) result |= 2;

        deque.push("front");
        if (deque.pop().equals("front")
                && deque.element().equals("b")) result |= 4;

        deque.addLast("x");
        deque.addLast("b");
        if (deque.removeLastOccurrence("b")
                && deque.size() == 2
                && deque.contains("b")
                && deque.contains("x")) result |= 8;

        java.util.Iterator descending = deque.descendingIterator();
        String reverse = "";
        while (descending.hasNext()) reverse += descending.next();
        if (reverse.equals("xb")) result |= 16;

        java.util.Iterator forward = deque.iterator();
        String ordered = "";
        while (forward.hasNext()) ordered += forward.next();
        if (ordered.equals("bx")) result |= 32;

        java.util.ArrayDeque copied = (java.util.ArrayDeque)
            ((java.util.ArrayDeque)deque).clone();
        copied.addAll(java.util.Arrays.asList(new String[] {"m", "n"}));
        Object[] values = copied.toArray();
        String[] typed = (String[])copied.toArray(new String[5]);
        if (copied.size() == 4 && deque.size() == 2
                && values.length == 4
                && typed[0].equals("b") && typed[3].equals("n")
                && typed[4] == null) result |= 64;

        copied.clear();
        boolean emptyMethods = copied.isEmpty()
            && copied.pollFirst() == null
            && copied.pollLast() == null
            && copied.peekFirst() == null
            && copied.peekLast() == null;
        boolean emptyThrows = false;
        boolean nullThrows = false;
        try {
            copied.removeFirst();
        } catch (java.util.NoSuchElementException expected) {
            emptyThrows = true;
        }
        try {
            copied.add(null);
        } catch (NullPointerException expected) {
            nullThrows = true;
        }
        if (emptyMethods && emptyThrows && nullThrows) result |= 128;
        return result;
    }

    public static int jdk8CoreCompatApi() throws Exception {
        int result = 0;
        if (CompatColor.GREEN.name().equals("GREEN")
                && CompatColor.GREEN.ordinal() == 1
                && CompatColor.valueOf("BLUE") == CompatColor.BLUE
                && CompatColor.RED.compareTo(CompatColor.GREEN) < 0) {
            result |= 1;
        }

        java.util.LinkedHashMap ordered = new java.util.LinkedHashMap();
        ordered.put("a", Integer.valueOf(1));
        ordered.put("b", Integer.valueOf(2));
        ordered.computeIfAbsent("c", key -> Integer.valueOf(3));
        ordered.merge("b", Integer.valueOf(4),
            (left, right) -> Integer.valueOf(
                ((Integer)left).intValue() + ((Integer)right).intValue()));
        String entryOrder = "";
        java.util.Iterator entries = ordered.entrySet().iterator();
        while (entries.hasNext()) {
            java.util.Map.Entry entry = (java.util.Map.Entry)entries.next();
            entryOrder += entry.getKey();
            if (entry.getKey().equals("a")) entry.setValue(Integer.valueOf(5));
        }
        if (entryOrder.equals("abc")
                && ordered.get("a").equals(Integer.valueOf(5))
                && ordered.get("b").equals(Integer.valueOf(6))
                && ordered.getOrDefault("missing", "fallback").equals("fallback")) {
            result |= 2;
        }

        java.util.LinkedHashSet linkedSet = new java.util.LinkedHashSet();
        linkedSet.add("x");
        linkedSet.add("y");
        linkedSet.add("x");
        String setOrder = "";
        java.util.Iterator setIterator = linkedSet.iterator();
        while (setIterator.hasNext()) setOrder += setIterator.next();
        if (linkedSet.size() == 2 && setOrder.equals("xy")) result |= 4;

        java.util.LinkedList linked = new java.util.LinkedList();
        linked.add("middle");
        linked.addFirst("first");
        linked.addLast("last");
        if (linked.removeFirst().equals("first")
                && linked.removeLast().equals("last")
                && linked.size() == 1 && linked.get(0).equals("middle")) {
            result |= 8;
        }

        java.util.EnumMap enumMap = new java.util.EnumMap(CompatColor.class);
        enumMap.put(CompatColor.RED, "r");
        enumMap.put(CompatColor.BLUE, "b");
        java.util.EnumSet enumSet = java.util.EnumSet.noneOf(CompatColor.class);
        enumSet.add(CompatColor.GREEN);
        java.util.EnumSet enumCopy = java.util.EnumSet.copyOf(enumSet);
        if (enumMap.size() == 2 && enumMap.get(CompatColor.BLUE).equals("b")
                && enumSet.contains(CompatColor.GREEN)
                && enumCopy.equals(enumSet)) result |= 16;

        java.util.TreeSet sorted = new java.util.TreeSet();
        sorted.add("c");
        sorted.add("a");
        sorted.add("b");
        String treeOrder = "";
        java.util.Iterator treeIterator = sorted.iterator();
        while (treeIterator.hasNext()) treeOrder += treeIterator.next();
        if (treeOrder.equals("abc") && sorted.contains("b")
                && sorted.remove("b") && !sorted.contains("b")) result |= 32;
        java.util.Properties properties = new java.util.Properties();
        properties.load(new java.io.ByteArrayInputStream(
            "alpha=1\nbeta\\:key=two\n".getBytes("ISO-8859-1")));
        properties.setProperty("gamma", "3");
        java.io.ByteArrayOutputStream storedProperties =
            new java.io.ByteArrayOutputStream();
        properties.store(storedProperties, "compat");
        String storedText = new String(storedProperties.toByteArray(),
            "ISO-8859-1");
        if (properties.getProperty("alpha").equals("1")
                && properties.getProperty("beta:key").equals("two")
                && properties.getProperty("missing", "d").equals("d")
                && properties.stringPropertyNames().size() == 3
                && storedText.indexOf("gamma=3") >= 0) result |= 64;

        java.nio.ByteBuffer buffer = java.nio.ByteBuffer.allocate(12);
        buffer.putInt(0x12345678).put((byte)9).put(new byte[] {10, 11});
        buffer.position(0);
        byte[] tail = new byte[3];
        int restored = buffer.getInt();
        buffer.get(tail);
        if (restored == 0x12345678 && tail[0] == 9
                && tail[1] == 10 && tail[2] == 11
                && buffer.remaining() == 5 && buffer.array().length == 12) {
            result |= 128;
        }

        java.security.SecureRandom secure = new java.security.SecureRandom();
        byte[] randomBytes = new byte[16];
        secure.nextBytes(randomBytes);
        if (randomBytes.length == 16 && secure.nextLong() != secure.nextLong()) {
            result |= 256;
        }

        if (java.util.concurrent.TimeUnit.SECONDS.toMillis(3L) == 3000L
                && java.util.concurrent.TimeUnit.MILLISECONDS.convert(
                    2L, java.util.concurrent.TimeUnit.SECONDS) == 2000L
                && java.util.concurrent.TimeUnit.NANOSECONDS.toSeconds(
                    1000000000L) == 1L) result |= 512;

        java.util.List words = new java.util.ArrayList();
        words.add("bbb");
        words.add("a");
        words.add("cc");
        java.util.Comparator lengthComparator =
            java.util.Comparator.comparingInt(value -> ((String)value).length());
        words.sort(lengthComparator.thenComparing(
            (left, right) -> ((String)left).compareTo((String)right)));
        if (words.get(0).equals("a") && words.get(2).equals("bbb")) {
            result |= 1024;
        }

        java.util.List removable = new java.util.ArrayList(
            java.util.Arrays.asList(new Integer[] {
                Integer.valueOf(1), Integer.valueOf(2), Integer.valueOf(3)
            }));
        boolean removed = removable.removeIf(value ->
            ((Integer)value).intValue() % 2 == 1);
        if (removed && removable.size() == 1
                && removable.get(0).equals(Integer.valueOf(2))) result |= 2048;
        EqualKey first = new EqualKey(7);
        EqualKey equalButDistinct = new EqualKey(7);
        java.util.IdentityHashMap identity = new java.util.IdentityHashMap();
        identity.put(first, "first");
        identity.put(equalButDistinct, "second");
        if (identity.size() == 2 && identity.get(first).equals("first")
                && identity.get(equalButDistinct).equals("second")) result |= 4096;

        java.util.WeakHashMap weak = new java.util.WeakHashMap();
        weak.put(first, "weak");
        if (weak.get(equalButDistinct).equals("weak")
                && weak.size() == 1) result |= 8192;

        int objectHash = java.util.Objects.hash("a", Integer.valueOf(2));
        if (Integer.compare(1, 2) < 0 && Integer.sum(4, 5) == 9
                && Long.sum(7L, 8L) == 15L
                && Long.remainderUnsigned(-1L, 2L) == 1L
                && objectHash != 0) result |= 16384;
        return result;
    }

    public static int arraysRangeSortApi() {
        int[] values = new int[] {9, 4, 3, 2, 8};
        java.util.Arrays.sort(values, 1, 4);
        if (values[0] != 9 || values[1] != 2 || values[2] != 3
                || values[3] != 4 || values[4] != 8) return 0;
        boolean reversed = false;
        boolean outOfBounds = false;
        try {
            java.util.Arrays.sort(values, 3, 2);
        } catch (IllegalArgumentException expected) {
            reversed = true;
        }
        try {
            java.util.Arrays.sort(values, -1, values.length);
        } catch (ArrayIndexOutOfBoundsException expected) {
            outOfBounds = true;
        }
        return reversed && outOfBounds ? 1 : 0;
    }

    public static int regexApi() {
        int result = 0;
        java.util.regex.Matcher integers = java.util.regex.Pattern.compile(
            "\\\"([a-z_]+)\\\"\\s*:\\s*(-?\\d+)").matcher(
                "{\"hp\": 12, \"mp_value\": -7}");
        if (integers.find() && integers.group(1).equals("hp")
                && integers.group(2).equals("12") && integers.find()
                && integers.group(1).equals("mp_value")
                && integers.group(2).equals("-7")) result |= 1;

        java.util.regex.Matcher asset = java.util.regex.Pattern.compile(
            "img_(\\d+)\\.mid").matcher("prefix img_321.mid suffix");
        if (asset.find() && asset.group(0).equals("img_321.mid")
                && asset.group(1).equals("321")) result |= 2;

        java.util.regex.Matcher bracket = java.util.regex.Pattern.compile(
            "\\[([^\\]]+)\\]").matcher("task [target_name]");
        if (bracket.find() && bracket.group(1).equals("target_name")) {
            result |= 4;
        }

        java.util.regex.Matcher fixed = java.util.regex.Pattern.compile(
            "\\\"title\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"").matcher(
                "{\"title\": \"phoneME\"}");
        if (fixed.find() && fixed.group(1).equals("phoneME")) result |= 8;
        return result;
    }

    public static int stringRegexMatchesApi() {
        int result = 0;
        if ("AB-05".matches("[A-Z]{2}-\\d{2}")) result |= 1;
        if ("alpha-12".matches("(?:alpha|beta)-\\d{2}")) result |= 2;
        if ("gamma-05".matches("(?:alpha|beta|gamma)-\\d{2}")) result |= 4;
        if ("item-123-09".matches("item-\\d+-\\d{2}")) result |= 8;
        if ("CODE_9-X".matches("[A-Z0-9_-]{4,32}")) result |= 16;
        return result;
    }

    public static int generalRegexApi() {
        int result = 0;
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(
            "(foo|bar)-(\\d+)").matcher("x bar-42 y");
        if (matcher.find() && matcher.group().equals("bar-42")
                && matcher.group(1).equals("bar")
                && matcher.group(2).equals("42")) result |= 1;

        String[] parts = "one, two;three".split("\\s*[,;]\\s*");
        if (parts.length == 3 && parts[0].equals("one")
                && parts[1].equals("two") && parts[2].equals("three")) {
            result |= 2;
        }

        if ("foo-12 bar-7".replaceAll("([a-z]+)-(\\d+)", "$1:$2")
                .equals("foo:12 bar:7")) result |= 4;
        if ("aaaa".matches("a{2,4}") && "abbb".matches("ab+")) result |= 8;

        try {
            java.util.regex.Pattern.compile("([a-z]");
        } catch (java.util.regex.PatternSyntaxException expected) {
            result |= 16;
        }
        return result;
    }

    public static int bigDecimalApi() {
        int result = 0;
        java.math.BigDecimal half = new java.math.BigDecimal("0.5");
        if (half.compareTo(java.math.BigDecimal.valueOf(0L)) > 0
                && half.compareTo(java.math.BigDecimal.valueOf(10L)) < 0) {
            result |= 1;
        }
        if (half.movePointRight(2).intValueExact() == 50) result |= 2;
        if (new java.math.BigDecimal("0.50").compareTo(half) == 0) result |= 4;
        if (new java.math.BigDecimal("-1.25").compareTo(
                java.math.BigDecimal.valueOf(0L)) < 0) result |= 8;
        try {
            half.intValueExact();
        } catch (ArithmeticException expected) {
            result |= 16;
        }
        return result;
    }

    public static int streamApi() {
        int result = 0;
        java.util.ArrayList values = new java.util.ArrayList();
        values.add("a");
        values.add("bbb");
        values.add("cc");
        int[] lengths = values.stream().mapToInt(
            value -> ((String)value).length()).toArray();
        if (lengths.length == 3 && lengths[0] == 1
                && lengths[1] == 3 && lengths[2] == 2) result |= 1;

        java.util.LinkedHashSet ordered = new java.util.LinkedHashSet();
        ordered.add("xx");
        ordered.add("z");
        int[] setLengths = ordered.stream().mapToInt(
            value -> ((String)value).length()).toArray();
        if (setLengths.length == 2 && setLengths[0] == 2
                && setLengths[1] == 1) result |= 2;
        return result;
    }

    public static int gzipApi() throws Exception {
        byte[] source = "phoneME gzip round trip".getBytes("UTF-8");
        java.io.ByteArrayOutputStream compressedOutput =
            new java.io.ByteArrayOutputStream();
        java.util.zip.GZIPOutputStream gzipOutput =
            new java.util.zip.GZIPOutputStream(compressedOutput);
        gzipOutput.write(source);
        gzipOutput.close();
        byte[] compressed = compressedOutput.toByteArray();
        if (compressed.length < 10 || compressed[0] != (byte)0x1f
                || compressed[1] != (byte)0x8b) return 0;

        java.util.zip.GZIPInputStream gzipInput =
            new java.util.zip.GZIPInputStream(
                new java.io.ByteArrayInputStream(compressed));
        byte[] restored = new byte[source.length];
        int count = gzipInput.read(restored, 0, restored.length);
        int eof = gzipInput.read();
        gzipInput.close();
        return count == source.length && eof == -1
            && java.util.Arrays.equals(source, restored) ? 1 : 0;
    }

    public static int reflectionAndUrlApi() throws Exception {
        java.lang.reflect.Field field = Jdk8Semantics.class.getDeclaredField(
            "REFLECTED_INSTANCE");
        if (!java.lang.reflect.Modifier.isStatic(field.getModifiers())
                || field.getType() != Jdk8Semantics.class
                || field.get(null) != REFLECTED_INSTANCE) return 0;
        java.net.URL resource = Jdk8Semantics.class.getResource(
            "Jdk8Semantics.class");
        if (resource == null || resource.toString().indexOf(
                "Jdk8Semantics.class") < 0) return 0;
        java.io.InputStream input = resource.openStream();
        int first = input.read();
        int second = input.read();
        input.close();
        return first == 0xca && second == 0xfe ? 1 : 0;
    }

    public static int threadLocalRandomApi() {
        int result = 0;
        java.util.concurrent.ThreadLocalRandom first =
            java.util.concurrent.ThreadLocalRandom.current();
        java.util.concurrent.ThreadLocalRandom second =
            java.util.concurrent.ThreadLocalRandom.current();
        if (first != null && first == second) result |= 1;

        int inherited = first.nextInt(17);
        if (inherited >= 0 && inherited < 17) result |= 2;

        int rangedInt = first.nextInt(-9, 12);
        if (rangedInt >= -9 && rangedInt < 12) result |= 4;

        long boundedLong = first.nextLong(100000L);
        if (boundedLong >= 0L && boundedLong < 100000L) result |= 8;

        long rangedLong = first.nextLong(-5000000000L, 7000000000L);
        if (rangedLong >= -5000000000L && rangedLong < 7000000000L) {
            result |= 16;
        }

        double boundedDouble = first.nextDouble(5.5);
        double rangedDouble = first.nextDouble(-3.0, 2.0);
        if (boundedDouble >= 0.0 && boundedDouble < 5.5
                && rangedDouble >= -3.0 && rangedDouble < 2.0) {
            result |= 32;
        }

        try {
            first.nextInt(4, 4);
        } catch (IllegalArgumentException expected) {
            try {
                first.nextLong(0L);
            } catch (IllegalArgumentException expectedLong) {
                result |= 64;
            }
        }
        return result;
    }

    public static int headlessCollectionsApi() throws Exception {
        int result = 0;

        java.util.List list = new java.util.ArrayList(
            java.util.Arrays.asList(new String[] {"c", "a", "b"}));
        java.util.Collections.sort(list);
        java.util.Iterator iterator = list.iterator();
        String joined = "";
        while (iterator.hasNext()) joined += iterator.next();
        if (joined.equals("abc") && list.toArray().length == 3) result |= 1;

        java.util.Map map = new java.util.HashMap();
        map.put(null, "zero");
        map.put("one", Integer.valueOf(1));
        map.put("nullable", null);
        Object previous = map.put("one", Integer.valueOf(2));
        if (map.size() == 3 && map.containsKey(null)
                && map.containsKey("nullable") && map.containsValue(null)
                && previous.equals(Integer.valueOf(1))
                && map.get("one").equals(Integer.valueOf(2))) result |= 2;

        java.util.Set set = new java.util.HashSet(list);
        boolean addedNull = set.add(null);
        boolean duplicate = set.add("a");
        if (addedNull && !duplicate && set.contains(null)
                && set.size() == 4 && set.toArray().length == 4) result |= 4;

        int[] numbers = new int[] {7, -2, 4, 4};
        java.util.Arrays.sort(numbers);
        int[] middle = java.util.Arrays.copyOfRange(numbers, 1, 3);
        byte[] bytes = new byte[] {3, -1, 2};
        java.util.Arrays.sort(bytes);
        byte[] padded = java.util.Arrays.copyOf(bytes, 5);
        if (numbers[0] == -2 && numbers[3] == 7
                && java.util.Arrays.binarySearch(numbers, 4) >= 1
                && java.util.Arrays.equals(middle, new int[] {4, 4})
                && padded[0] == -1 && padded[2] == 3 && padded[4] == 0) {
            result |= 8;
        }

        java.util.Collections.reverse(list);
        java.util.Collections.swap(list, 0, 2);
        java.util.Collections.fill(list, "x");
        if (list.get(0).equals("x") && list.get(2).equals("x")
                && java.util.Collections.emptyList().isEmpty()
                && java.util.Collections.singletonList("s").size() == 1) {
            result |= 16;
        }

        String encoded = java.util.Base64.getEncoder().encodeToString(
            "mod-game".getBytes("UTF-8"));
        byte[] decoded = java.util.Base64.getDecoder().decode(encoded);
        byte[] encodedBytes = java.util.Base64.getEncoder().encode(decoded);
        if (encoded.equals("bW9kLWdhbWU=")
                && new String(decoded, "UTF-8").equals("mod-game")
                && new String(encodedBytes, "US-ASCII").equals(encoded)) {
            result |= 32;
        }

        Comparable comparable = "b";
        if (comparable.compareTo("a") > 0
                && java.util.Objects.equals("v", new String("v"))
                && java.util.Objects.hashCode(null) == 0
                && java.util.Objects.toString(null).equals("null")
                && java.util.Objects.toString(null, "fallback").equals("fallback")
                && java.util.Objects.requireNonNull("ok", "missing").equals("ok")) {
            result |= 64;
        }

        java.util.Map copiedMap = new java.util.HashMap(map);
        java.util.Map mergedMap = new java.util.HashMap();
        mergedMap.putAll(copiedMap);
        java.util.Collection values = mergedMap.values();
        java.util.Set keys = mergedMap.keySet();
        java.util.Set copiedKeys = copiedMap.keySet();
        java.util.Set originalKeys = map.keySet();
        java.util.Set emptyKeysA = new java.util.HashMap().keySet();
        java.util.Set emptyKeysB = new java.util.HashMap().keySet();
        if (copiedMap.size() == 3 && mergedMap.size() == 3
                && values.size() == 3 && keys.contains("one")
                && copiedKeys.equals(originalKeys)
                && originalKeys.equals(copiedKeys)
                && copiedKeys.hashCode() == originalKeys.hashCode()
                && emptyKeysA.equals(emptyKeysB)
                && mergedMap.remove("one").equals(Integer.valueOf(2))
                && mergedMap.size() == 2) result |= 128;
        return result;
    }

    public static int headlessIoApi() throws Exception {
        int result = 0;
        java.io.ByteArrayOutputStream sink = new java.io.ByteArrayOutputStream();
        java.io.BufferedOutputStream output =
            new java.io.BufferedOutputStream(sink, 4);
        output.write(new byte[] {65, 66, 67});
        output.flush();
        if (java.util.Arrays.equals(sink.toByteArray(),
                new byte[] {65, 66, 67})) result |= 1;

        java.io.BufferedInputStream input = new java.io.BufferedInputStream(
            new java.io.ByteArrayInputStream(sink.toByteArray()), 4);
        if (input.read() == 65 && input.available() == 2) result |= 2;
        java.lang.AutoCloseable automatic = input;
        automatic.close();
        java.io.Closeable closeable = output;
        closeable.close();
        result |= 4;

        try {
            new java.io.BufferedInputStream(
                new java.io.ByteArrayInputStream(new byte[0]), 0);
        } catch (IllegalArgumentException expected) {
            result |= 8;
        }

        java.io.BufferedReader utf8Lines = new java.io.BufferedReader(
            new java.io.InputStreamReader(
                new java.io.ByteArrayInputStream(
                    "một\r\nhai\nba\rbon".getBytes("UTF-8")),
                "UTF-8"));
        if ("một".equals(utf8Lines.readLine())
                && "hai".equals(utf8Lines.readLine())
                && "ba".equals(utf8Lines.readLine())
                && "bon".equals(utf8Lines.readLine())
                && utf8Lines.readLine() == null) result |= 16;

        java.io.BufferedReader utf16Lines = new java.io.BufferedReader(
            new java.io.InputStreamReader(
                new java.io.ByteArrayInputStream(new byte[] {
                    0, 65, 0, 13, 0, 10, 1, 16, 0, 10
                }), "UTF-16BE"));
        if ("A".equals(utf16Lines.readLine())
                && "Đ".equals(utf16Lines.readLine())
                && utf16Lines.readLine() == null) result |= 32;
        return result;
    }

    public static int headlessCompatibilityExceptions() {
        int result = 0;
        try {
            new java.util.HashMap(-1);
        } catch (IllegalArgumentException expected) {
            result |= 1;
        }
        try {
            java.util.Base64.getDecoder().decode("broken");
        } catch (IllegalArgumentException expected) {
            result |= 2;
        }
        java.util.Iterator iterator =
            java.util.Collections.emptyList().iterator();
        try {
            iterator.next();
        } catch (java.util.NoSuchElementException expected) {
            result |= 4;
        }
        try {
            iterator.remove();
        } catch (UnsupportedOperationException expected) {
            result |= 8;
        }
        return result;
    }

    public static int utilExceptions() {
        int result = 0;
        try {
            new java.util.Stack().pop();
        } catch (java.util.EmptyStackException expected) {
            result += 1;
        }
        try {
            java.util.Vector vector = new java.util.Vector();
            java.util.Enumeration enumeration = vector.elements();
            enumeration.nextElement();
        } catch (java.util.NoSuchElementException expected) {
            result += 2;
        }
        try {
            new java.util.Hashtable().put(null, "x");
        } catch (NullPointerException expected) {
            result += 4;
        }
        try {
            new java.util.Random(1L).nextInt(0);
        } catch (IllegalArgumentException expected) {
            result += 8;
        }
        return result;
    }

    public static int choiceGroupApi() {
        int result = 0;
        javax.microedition.lcdui.ChoiceGroup group =
            new javax.microedition.lcdui.ChoiceGroup(
                "Mode", javax.microedition.lcdui.Choice.MULTIPLE,
                new String[] {"A", "B"}, null);
        if (group.size() == 2 && group.getString(0).equals("A")
                && group.getImage(1) == null) result |= 1;
        group.setSelectedIndex(1, true);
        if (!group.isSelected(0) && group.isSelected(1)) result |= 2;
        group.append("C", null);
        group.setSelectedFlags(new boolean[] {true, false, true});
        boolean[] flags = new boolean[3];
        if (group.getSelectedFlags(flags) == 2
                && flags[0] && !flags[1] && flags[2]) result |= 4;
        group.insert(1, "X", null);
        group.set(1, "Y", null);
        group.delete(0);
        if (group.size() == 3 && group.getString(0).equals("Y")) result |= 8;
        group.setFitPolicy(javax.microedition.lcdui.Choice.TEXT_WRAP_ON);
        if (group.getFitPolicy()
                == javax.microedition.lcdui.Choice.TEXT_WRAP_ON) result |= 16;
        return result;
    }

    public static int listApi() {
        int result = 0;
        javax.microedition.lcdui.List list =
            new javax.microedition.lcdui.List(
                "Menu", javax.microedition.lcdui.Choice.IMPLICIT,
                new String[] {"One", "Two"}, null);
        if (javax.microedition.lcdui.List.SELECT_COMMAND != null
                && javax.microedition.lcdui.List.SELECT_COMMAND.getCommandType()
                    == javax.microedition.lcdui.Command.SCREEN) result |= 1;
        if (list.getSelectedIndex() == 0 && list.isSelected(0)) result |= 2;
        list.setSelectedIndex(1, true);
        if (list.getSelectedIndex() == 1 && list.isSelected(1)
                && !list.isSelected(0)) result |= 4;
        return result;
    }

    public static int choiceApi() {
        return choiceGroupApi() | (listApi() << 5);
    }

    public static int consoleApi() throws Exception {
        java.io.ByteArrayOutputStream buffer =
            new java.io.ByteArrayOutputStream();
        java.io.PrintStream stream = new java.io.PrintStream(buffer, true);
        stream.print("value=");
        stream.print(42);
        stream.print(',');
        stream.println(true);
        stream.print((Object) null);
        stream.println();
        stream.write(new byte[] {65, 66, 67}, 1, 2);
        stream.flush();
        int result = buffer.toString().equals(
            "value=42,true\nnull\nBC") ? 1 : 0;
        if (!stream.checkError()) result |= 2;
        System.out.print("OUT:");
        System.out.println(7);
        System.err.println(false);
        if (System.nanoTime() != 0L) result |= 4;
        System.gc();
        return result;
    }

    public static int stringEncodingApi() throws Exception {
        int result = 0;
        String original = "A\u0000Việt\u20ac";
        byte[] utf8 = original.getBytes("UTF-8");
        if (new String(utf8, "UTF-8").equals(original)) result |= 1;
        byte[] defaultEncoded = "\u00e5".getBytes();
        if (defaultEncoded.length == 1
                && (defaultEncoded[0] & 0xff) == 0xe5
                && new String(defaultEncoded).equals("\u00e5")) result |= 2;
        byte[] sliced = {0, 65, 66, 0};
        if (new String(sliced, 1, 2, "US-ASCII").equals("AB")) result |= 4;
        String latin = new String(new byte[] {(byte) 0xe9}, "ISO-8859-1");
        if (latin.charAt(0) == '\u00e9'
                && (latin.getBytes("ISO-8859-1")[0] & 0xff) == 0xe9) {
            result |= 8;
        }
        byte[] utf16be = {0, 65, 0x20, (byte) 0xac};
        String wide = new String(utf16be, "UTF-16BE");
        byte[] wideEncoded = wide.getBytes("UTF-16BE");
        if (wide.equals("A\u20ac") && wideEncoded.length == 4
                && (wideEncoded[2] & 0xff) == 0x20
                && (wideEncoded[3] & 0xff) == 0xac) result |= 16;
        byte[] withBom = {(byte) 0xfe, (byte) 0xff, 0, 90};
        if (new String(withBom, "UTF-16").equals("Z")) result |= 32;
        if (new String(new byte[] {(byte) 0xff}, "US-ASCII").charAt(0)
                == '\ufffd') result |= 64;
        return result;
    }

    public static int stringEncodingExceptions() throws Exception {
        int result = 0;
        try {
            "x".getBytes("phoneME-unknown");
        } catch (java.io.UnsupportedEncodingException expected) {
            result |= 1;
        }
        try {
            new String(new byte[1], 1, 1, "UTF-8");
        } catch (StringIndexOutOfBoundsException expected) {
            result |= 2;
        }
        try {
            new String(new byte[1], (String) null);
        } catch (NullPointerException expected) {
            result |= 4;
        }
        return result;
    }

    public static int timeApi() {
        int result = 0;
        java.util.Date epoch = new java.util.Date(0L);
        java.util.Date later = new java.util.Date(1000L);
        if (epoch.getTime() == 0L) result |= 1;
        if (epoch.before(later) && later.after(epoch)
                && epoch.compareTo(later) < 0) result |= 2;
        if (epoch.toString().equals("Thu Jan 01 00:00:00 GMT 1970")) result |= 4;

        java.util.TimeZone zone =
            java.util.TimeZone.getTimeZone("GMT+07:00");
        if (zone.getRawOffset() == 7 * 60 * 60 * 1000
                && zone.getID().equals("GMT+07:00")
                && !zone.useDaylightTime()) result |= 8;

        java.util.Calendar calendar = java.util.Calendar.getInstance(zone);
        calendar.setTimeInMillis(0L);
        if (calendar.get(java.util.Calendar.YEAR) == 1970
                && calendar.get(java.util.Calendar.MONTH) == java.util.Calendar.JANUARY
                && calendar.get(java.util.Calendar.DATE) == 1) result |= 16;
        if (calendar.get(java.util.Calendar.HOUR_OF_DAY) == 7
                && calendar.get(java.util.Calendar.DAY_OF_WEEK)
                    == java.util.Calendar.THURSDAY) result |= 32;

        calendar.set(java.util.Calendar.YEAR, 2000);
        calendar.set(java.util.Calendar.MONTH, java.util.Calendar.JANUARY);
        calendar.set(java.util.Calendar.DATE, 1);
        calendar.set(java.util.Calendar.HOUR_OF_DAY, 0);
        calendar.set(java.util.Calendar.MINUTE, 0);
        calendar.set(java.util.Calendar.SECOND, 0);
        calendar.set(java.util.Calendar.MILLISECOND, 0);
        if (calendar.get(java.util.Calendar.YEAR) == 2000
                && calendar.get(java.util.Calendar.MONTH) == 0
                && calendar.get(java.util.Calendar.DATE) == 1
                && calendar.get(java.util.Calendar.HOUR_OF_DAY) == 0) result |= 64;
        if (calendar.getTime().getTime() == calendar.getTimeInMillis()) result |= 128;

        java.util.Calendar copy = java.util.Calendar.getInstance(zone);
        copy.setTime(calendar.getTime());
        java.util.Calendar future = java.util.Calendar.getInstance(zone);
        future.setTimeInMillis(copy.getTimeInMillis() + 1L);
        if (calendar.equals(copy) && calendar.before(future)
                && future.after(calendar)) result |= 256;
        if (java.util.TimeZone.getAvailableIDs().length >= 2
                && java.util.TimeZone.getTimeZone("Unknown").getID().equals("GMT")) {
            result |= 512;
        }
        return result;
    }

    public static int timeExceptions() {
        int result = 0;
        try {
            java.util.Calendar.getInstance((java.util.TimeZone) null);
        } catch (NullPointerException expected) {
            result |= 1;
        }
        try {
            java.util.Calendar.getInstance().get(99);
        } catch (ArrayIndexOutOfBoundsException expected) {
            result |= 2;
        }
        try {
            java.util.TimeZone.getTimeZone((String) null);
        } catch (NullPointerException expected) {
            result |= 4;
        }
        try {
            new java.util.Date(0L).compareTo((java.util.Date) null);
        } catch (NullPointerException expected) {
            result |= 8;
        }
        return result;
    }

    public static int rmsCreate() throws Exception {
        final String name = "core-rms";
        try {
            javax.microedition.rms.RecordStore.deleteRecordStore(name);
        } catch (javax.microedition.rms.RecordStoreNotFoundException ignored) {
        }
        javax.microedition.rms.RecordStore store =
            javax.microedition.rms.RecordStore.openRecordStore(name, true);
        int result = store.getName().equals(name) ? 1 : 0;
        int first = store.addRecord(new byte[] {1, 2, 3}, 0, 3);
        int second = store.addRecord(new byte[] {4, 5}, 0, 2);
        if (first == 1 && second == 2 && store.getNumRecords() == 2) result |= 2;
        if (store.getSize() == 5 && store.getNextRecordID() == 3
                && store.getSizeAvailable() > 0) result |= 4;
        byte[] firstBytes = store.getRecord(first);
        if (firstBytes.length == 3 && firstBytes[0] == 1
                && firstBytes[2] == 3) result |= 8;
        byte[] destination = new byte[5];
        if (store.getRecord(second, destination, 2) == 2
                && destination[2] == 4 && destination[3] == 5) result |= 16;
        store.setRecord(first, new byte[] {9, 8, 7}, 0, 2);
        if (store.getRecordSize(first) == 2
                && store.getRecord(first)[1] == 8) result |= 32;
        javax.microedition.rms.RecordEnumeration enumeration =
            store.enumerateRecords(null, null, false);
        if (enumeration.numRecords() == 2 && enumeration.hasNextElement()
                && enumeration.nextRecordId() == first
                && enumeration.nextRecord()[0] == 4) result |= 64;
        enumeration.reset();
        if (enumeration.nextRecord()[0] == 9) result |= 128;
        enumeration.destroy();
        store.deleteRecord(second);
        if (store.getNumRecords() == 1 && store.getVersion() >= 4
                && store.getLastModified() > 0L) result |= 256;
        store.closeRecordStore();
        String[] names = javax.microedition.rms.RecordStore.listRecordStores();
        if (names != null && names.length == 1 && names[0].equals(name)) {
            result |= 512;
        }
        return result;
    }

    public static int rmsReadPersistent() throws Exception {
        javax.microedition.rms.RecordStore store =
            javax.microedition.rms.RecordStore.openRecordStore("core-rms", false);
        byte[] value = store.getRecord(1);
        int result = value.length == 2 && value[0] == 9 && value[1] == 8
            ? 1 : 0;
        store.closeRecordStore();
        return result;
    }

    public static int rmsDeletePersistent() throws Exception {
        javax.microedition.rms.RecordStore.deleteRecordStore("core-rms");
        return javax.microedition.rms.RecordStore.listRecordStores() == null ? 1 : 0;
    }

    public static int rmsExceptions() throws Exception {
        int result = 0;
        try {
            javax.microedition.rms.RecordStore.openRecordStore("missing", false);
        } catch (javax.microedition.rms.RecordStoreNotFoundException expected) {
            result |= 1;
        }
        javax.microedition.rms.RecordStore store =
            javax.microedition.rms.RecordStore.openRecordStore("errors", true);
        try {
            store.getRecord(99);
        } catch (javax.microedition.rms.InvalidRecordIDException expected) {
            result |= 2;
        }
        try {
            javax.microedition.rms.RecordStore.deleteRecordStore("errors");
        } catch (javax.microedition.rms.RecordStoreException expected) {
            result |= 4;
        }
        store.closeRecordStore();
        try {
            store.closeRecordStore();
        } catch (javax.microedition.rms.RecordStoreNotOpenException expected) {
            result |= 8;
        }
        javax.microedition.rms.RecordStore.deleteRecordStore("errors");
        return result;
    }

    public static int ioRoundTrip() throws java.io.IOException {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream(1);
        java.io.DataOutputStream output = new java.io.DataOutputStream(bytes);
        output.writeBoolean(true);
        output.writeByte(-2);
        output.writeShort(0xFEDC);
        output.writeChar('ệ');
        output.writeInt(0x12345678);
        output.writeLong(0x1020304050607080L);
        output.writeFloat(1.5F);
        output.writeDouble(-2.25);
        output.writeUTF("A\u0000Việt");
        int written = output.size();
        byte[] encoded = bytes.toByteArray();

        java.io.ByteArrayInputStream raw = new java.io.ByteArrayInputStream(encoded);
        raw.mark(1000);
        int first = raw.read();
        raw.reset();
        if (raw.read() != first) return -1;
        raw.reset();
        java.io.DataInputStream input = new java.io.DataInputStream(raw);
        int result = 0;
        if (input.readBoolean()) result |= 1;
        if (input.readByte() == -2) result |= 2;
        if (input.readShort() == (short) 0xFEDC) result |= 4;
        if (input.readChar() == 'ệ') result |= 8;
        if (input.readInt() == 0x12345678) result |= 16;
        if (input.readLong() == 0x1020304050607080L) result |= 32;
        if (input.readFloat() == 1.5F) result |= 64;
        if (input.readDouble() == -2.25) result |= 128;
        if (input.readUTF().equals("A\u0000Việt")) result |= 256;
        if (input.available() == 0 && written == encoded.length) result |= 512;
        return result;
    }

    public static int legacyRawNullUtf() throws java.io.IOException {
        byte[] encoded = new byte[] {0, 2, 0x17, 0};
        java.io.DataInputStream input = new java.io.DataInputStream(
                new java.io.ByteArrayInputStream(encoded));
        String value = input.readUTF();
        return value.length() == 2
                && value.charAt(0) == 0x17
                && value.charAt(1) == 0 ? 1 : 0;
    }

    public static int byteArrayStreams() throws java.io.IOException {
        java.io.ByteArrayOutputStream first = new java.io.ByteArrayOutputStream();
        first.write(new byte[] {'A', 'B', 'C'}, 0, 3);
        java.io.ByteArrayOutputStream second = new java.io.ByteArrayOutputStream();
        first.writeTo(second);
        int result = second.toString().equals("ABC") ? 1 : 0;
        second.reset();
        second.write('Z');
        if (second.size() == 1 && second.toByteArray()[0] == 'Z') result |= 2;

        byte[] source = {10, 20, 30, 40};
        java.io.ByteArrayInputStream input =
            new java.io.ByteArrayInputStream(source, 1, 2);
        input.mark(10);
        if (input.available() == 2 && input.read() == 20
                && input.skip(1) == 1 && input.read() == -1) result |= 4;
        input.reset();
        byte[] destination = new byte[3];
        if (input.read(destination, 1, 2) == 2
                && destination[1] == 20 && destination[2] == 30) result |= 8;
        return result;
    }

    public static int classApi() throws Exception {
        Object value = "abc";
        Class stringClass = value.getClass();
        int result = 0;
        if (stringClass == String.class
                && stringClass.getName().equals("java.lang.String")) result |= 1;
        if (!stringClass.isArray() && !stringClass.isInterface()) result |= 2;
        if (stringClass.isInstance("x")) result |= 4;
        if (!stringClass.isInstance(new Object())) result |= 8;
        if (stringClass.getSuperclass() == Object.class) result |= 16;
        if (Runnable.class.isInterface()) result |= 32;
        if (String[].class.isArray()) result |= 64;
        if (Class.forName("java.lang.String") == String.class) result |= 128;
        java.io.InputStream resource =
            Jdk8Semantics.class.getResourceAsStream("Jdk8Semantics.class");
        if (resource != null && resource.read() == 0xCA) result |= 256;
        return result;
    }

    public static int systemRuntimeApi() {
        int result = 0;
        if ("CLDC-1.1".equals(
                System.getProperty("microedition.configuration"))) result |= 1;
        if ("MIDP-2.1".equals(
                System.getProperty("microedition.profiles"))) result |= 2;
        if (System.getProperty("phoneME.missing") == null) result |= 4;
        if ("fallback".equals(
                System.getProperty("phoneME.missing", "fallback"))) result |= 8;
        if ("\n".equals(System.getProperty("line.separator"))) result |= 16;
        String platform = System.getProperty("microedition.platform");
        if ("j2me".equals(platform)) result |= 256;
        Runtime first = Runtime.getRuntime();
        Runtime second = Runtime.getRuntime();
        if (first == second) result |= 32;
        if (first.totalMemory() >= first.freeMemory()
                && first.totalMemory() > 0L) result |= 64;
        String objectText = new Object().toString();
        if (objectText.length() >= 16
                && objectText.substring(0, 16).equals("java.lang.Object")) {
            result |= 128;
        }
        return result;
    }

    public static int classExtendedApi() {
        int result = 0;
        if (Object.class.isAssignableFrom(String.class)
                && !String.class.isAssignableFrom(Object.class)) result |= 1;
        if (String[].class.getComponentType() == String.class
                && int[].class.getComponentType() == int.class) result |= 2;
        if (String.class.getComponentType() == null
                && int.class.getSuperclass() == null) result |= 4;
        if (int.class.getName().equals("int")
                && int.class.toString().equals("int")) result |= 8;
        if (String.class.toString().equals("class java.lang.String")
                && Runnable.class.toString().equals("interface java.lang.Runnable")) {
            result |= 16;
        }
        if (!String.class.desiredAssertionStatus()
                && !int.class.isInstance(Integer.valueOf(1))) result |= 32;
        return result;
    }

    public static int cloneApi() throws Exception {
        int result = 0;
        CloneTarget source = new CloneTarget();
        CloneTarget copy = source.copy();
        if (copy != source && copy.value == source.value) result |= 1;
        if (copy.shared == source.shared && copy.shared[1] == 5) result |= 2;
        int[] values = {1, 2, 3};
        int[] valuesCopy = values.clone();
        if (valuesCopy != values && valuesCopy.length == 3
                && valuesCopy[2] == 3) result |= 4;
        String[][] matrix = {{"A"}, {"B"}};
        String[][] matrixCopy = matrix.clone();
        if (matrixCopy != matrix && matrixCopy[0] == matrix[0]
                && matrixCopy[1][0].equals("B")) result |= 8;
        return result;
    }

    public static int cloneExceptions() {
        try {
            new NonCloneTarget().copy();
        } catch (CloneNotSupportedException expected) {
            return 1;
        }
        return 0;
    }

    public static int reflectiveNewInstance() throws Exception {
        Object created = Class.forName(
            "corefixture.Jdk8Semantics$ReflectTarget").newInstance();
        return created instanceof ReflectTarget
                && ((ReflectTarget) created).value == 37 ? 1 : 0;
    }

    public static int reflectiveNewInstanceExceptions() {
        int result = 0;
        try {
            Runnable.class.newInstance();
        } catch (InstantiationException expected) {
            result |= 1;
        } catch (Exception unexpected) {
        }
        try {
            PrivateReflect.class.newInstance();
        } catch (IllegalAccessException expected) {
            result |= 2;
        } catch (Exception unexpected) {
        }
        try {
            int.class.newInstance();
        } catch (InstantiationException expected) {
            result |= 4;
        } catch (Exception unexpected) {
        }
        try {
            NoDefaultReflect.class.newInstance();
        } catch (InstantiationException expected) {
            result |= 8;
        } catch (Exception unexpected) {
        }
        return result;
    }

    public static int classExceptions() {
        int result = 0;
        try {
            Class.forName("corefixture.DoesNotExist");
        } catch (ClassNotFoundException expected) {
            result |= 1;
        }
        if (Jdk8Semantics.class.getResourceAsStream("missing.bin") == null) {
            result |= 2;
        }
        return result;
    }

    public static int ioExceptions() throws java.io.IOException {
        int result = 0;
        try {
            new java.io.DataInputStream(
                new java.io.ByteArrayInputStream(new byte[0])).readInt();
        } catch (java.io.EOFException expected) {
            result += 1;
        }
        try {
            byte[] invalid = {0, 2, (byte) 0xC2, 0x20};
            new java.io.DataInputStream(
                new java.io.ByteArrayInputStream(invalid)).readUTF();
        } catch (java.io.UTFDataFormatException expected) {
            result += 2;
        }
        try {
            new java.io.ByteArrayOutputStream(-1);
        } catch (IllegalArgumentException expected) {
            result += 4;
        }
        try {
            new java.io.ByteArrayInputStream(new byte[1]).read(new byte[1], 1, 1);
        } catch (IndexOutOfBoundsException expected) {
            result += 8;
        }
        return result;
    }

    public static int classAndResources() throws Exception {
        int result = 0;
        Class type = Class.forName("corefixture.Jdk8Semantics");
        if (type.getName().equals("corefixture.Jdk8Semantics")
                && type.getSuperclass().getName().equals("java.lang.Object")) {
            result |= 1;
        }
        if (LambdaOps.IntUnary.class.isInterface()
                && !String.class.isInterface()) result |= 2;
        Class arrayType = Class.forName("[Ljava.lang.String;");
        if (arrayType.isArray()
                && arrayType.getName().equals("[Ljava.lang.String;")) result |= 4;
        Object value = "game";
        if (String.class.isInstance(value)
                && value.getClass() == String.class) result |= 8;

        java.io.InputStream relative =
            Jdk8Semantics.class.getResourceAsStream("data.bin");
        java.io.InputStream absolute =
            Jdk8Semantics.class.getResourceAsStream("/corefixture/data.bin");
        if (relative != null && absolute != null
                && relative.read() == 'P' && absolute.read() == 'P') result |= 16;
        if (Jdk8Semantics.class.getResourceAsStream("missing.bin") == null) {
            result |= 32;
        }
        try {
            Class.forName("corefixture.DoesNotExist");
        } catch (ClassNotFoundException expected) {
            result |= 64;
        }
        return result;
    }
}
