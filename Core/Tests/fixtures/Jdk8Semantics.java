package corefixture;

public final class Jdk8Semantics {
    private static final int INT_CONSTANT = 123;
    private static final long LONG_CONSTANT = 7L;
    private static final float FLOAT_CONSTANT = 2.0F;
    private static final double DOUBLE_CONSTANT = 3.0;
    private static final String TEXT_CONSTANT = "Việt";

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
                && Float.parseFloat("2.25") == 2.25F) {
            result |= 64;
        }
        if (doubleValue.intValue() == 2
                && Double.parseDouble("3.5") == 3.5) {
            result |= 128;
        }
        if (integer.toString().equals("123")
                && longValue.toString().equals("9")
                && character.toString().equals("G")) {
            result |= 256;
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
        if (new String(original.getBytes()).equals(original)) result |= 2;
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
        } catch (IllegalArgumentException expected) {
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
        if ("MIDP-2.0".equals(
                System.getProperty("microedition.profiles"))) result |= 2;
        if (System.getProperty("phoneME.missing") == null) result |= 4;
        if ("fallback".equals(
                System.getProperty("phoneME.missing", "fallback"))) result |= 8;
        if ("\n".equals(System.getProperty("line.separator"))) result |= 16;
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
            byte[] invalid = {0, 1, 0};
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
