package corefixture;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintStream;

public final class CldcLibraryOps {
    private CldcLibraryOps() {
    }

    private static final class CountingOutputStream extends OutputStream {
        int count;
        int checksum;
        boolean flushed;
        boolean closed;

        public void write(int value) {
            count++;
            checksum = checksum * 31 + (value & 0xFF);
        }

        public void flush() {
            flushed = true;
        }

        public void close() {
            closed = true;
        }
    }

    private static int readerWriterRoundTrip() throws Exception {
        String expected = "Tiếng Việt \uD83D\uDE00";
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        OutputStreamWriter writer = new OutputStreamWriter(bytes, "UTF_8");
        writer.write(expected, 0, 5);
        writer.write(expected.substring(5));
        if (!"UTF8".equals(writer.getEncoding())) return 101;
        writer.flush();

        InputStreamReader reader = new InputStreamReader(
                new ByteArrayInputStream(bytes.toByteArray()), "utf-8");
        if (!"UTF8".equals(reader.getEncoding())) return 102;
        char[] buffer = new char[64];
        int count = reader.read(buffer, 0, buffer.length);
        if (count != expected.length()) return 103;
        if (!expected.equals(new String(buffer, 0, count))) return 104;
        if (reader.read() != -1) return 105;
        reader.close();
        if (reader.getEncoding() != null) return 106;
        writer.close();
        if (writer.getEncoding() != null) return 107;
        return 0;
    }

    private static int encodingAliases() throws Exception {
        ByteArrayOutputStream latinBytes = new ByteArrayOutputStream();
        OutputStreamWriter latin = new OutputStreamWriter(
                latinBytes, "ISO_8859-1");
        latin.write("A\u00E9\u20AC");
        latin.close();
        byte[] encoded = latinBytes.toByteArray();
        if (encoded.length != 3 || encoded[0] != 65 ||
                (encoded[1] & 0xFF) != 233 || encoded[2] != 63) return 111;

        InputStreamReader ascii = new InputStreamReader(
                new ByteArrayInputStream(new byte[] {65, (byte) 0xFF}),
                "US ASCII");
        if (ascii.read() != 'A' || ascii.read() != 0xFFFD) return 112;

        ByteArrayOutputStream utf16Bytes = new ByteArrayOutputStream();
        OutputStreamWriter utf16 = new OutputStreamWriter(
                utf16Bytes, "UnicodeBigUnmarked");
        utf16.write("A\u0110");
        utf16.close();
        InputStreamReader decoded = new InputStreamReader(
                new ByteArrayInputStream(utf16Bytes.toByteArray()),
                "UTF-16BE");
        if (decoded.read() != 'A' || decoded.read() != 0x0110 ||
                decoded.read() != -1) return 113;

        InputStreamReader defaultReader = new InputStreamReader(
                new ByteArrayInputStream(new byte[] {(byte) 0xE9}));
        if (!"ISO8859_1".equals(defaultReader.getEncoding())) return 114;
        if (defaultReader.read() != 0x00E9) return 1141;
        if (defaultReader.read() != -1) return 1142;
        ByteArrayOutputStream defaultBytes = new ByteArrayOutputStream();
        OutputStreamWriter defaultWriter = new OutputStreamWriter(defaultBytes);
        if (!"ISO8859_1".equals(defaultWriter.getEncoding())) return 115;
        defaultWriter.write('\u00E9');
        defaultWriter.close();
        if (defaultBytes.toByteArray().length != 1 ||
                (defaultBytes.toByteArray()[0] & 0xFF) != 0xE9) return 116;
        if (!"ISO8859_1".equals(
                System.getProperty("microedition.encoding"))) return 117;
        return 0;
    }

    private static int stringSplitSemantics() {
        String[] literal = "one_khoga_two_khoga_".split("_khoga_");
        if (literal.length != 2 || !"one".equals(literal[0]) ||
                !"two".equals(literal[1])) return 118;

        String[] consecutive = "a,,b,".split(",");
        if (consecutive.length != 3 || !"a".equals(consecutive[0]) ||
                !"".equals(consecutive[1]) ||
                !"b".equals(consecutive[2])) return 119;

        String[] escaped = "a|b|".split("\\|");
        if (escaped.length != 2 || !"a".equals(escaped[0]) ||
                !"b".equals(escaped[1])) return 120;

        String[] whitespace = " a\tb  c ".split("\\s+");
        if (whitespace.length != 4 || !"".equals(whitespace[0]) ||
                !"a".equals(whitespace[1]) ||
                !"b".equals(whitespace[2]) ||
                !"c".equals(whitespace[3])) return 121;

        String[] tsv = "runtime_id\tpet_key\t".split("\\t", -1);
        if (tsv.length != 3 || !"runtime_id".equals(tsv[0]) ||
                !"pet_key".equals(tsv[1]) || !"".equals(tsv[2])) return 1221;

        String[] unchanged = "plain".split(",");
        if (unchanged.length != 1 || !"plain".equals(unchanged[0])) return 122;
        if (!" \t\r\n".isBlank() || " x ".isBlank() || !"".isBlank()) {
            return 1231;
        }
        if (java.util.Locale.ROOT == null ||
                !"gift".equals("GiFt".toLowerCase(java.util.Locale.ROOT)) ||
                !"GIFT".equals("GiFt".toUpperCase(java.util.Locale.ROOT))) {
            return 1232;
        }
        java.util.List empty = java.util.List.of();
        if (!empty.isEmpty()) return 1233;
        try {
            empty.add("x");
            return 1234;
        } catch (UnsupportedOperationException expected) {
        }
        java.util.HashSet retained = new java.util.HashSet();
        retained.add("keep");
        retained.add("drop");
        java.util.ArrayList allowed = new java.util.ArrayList();
        allowed.add("keep");
        if (!retained.retainAll(allowed) || retained.size() != 1 ||
                !retained.contains("keep") || retained.contains("drop")) {
            return 1235;
        }
        return 0;
    }

    private static int customOutputDispatch() throws Exception {
        CountingOutputStream output = new CountingOutputStream();
        PrintStream stream = new PrintStream(output, true);
        stream.print("ABC");
        stream.println(12);
        stream.flush();
        if (output.count != 6) return 1200 + output.count;
        if (!output.flushed) return 122;
        stream.close();
        if (!output.closed) return 123;
        return 0;
    }

    private static int modifiedUtfRoundTrip() throws Exception {
        String expected = "A\u0000\u07FF\u0800\uD83D\uDE00";
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream output = new DataOutputStream(bytes);
        output.writeUTF(expected);
        DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(bytes.toByteArray()));
        if (!expected.equals(input.readUTF())) return 131;
        try {
            new DataInputStream(new ByteArrayInputStream(
                    new byte[] {0, 2, (byte) 0xC2, 0x20})).readUTF();
            return 132;
        } catch (java.io.UTFDataFormatException expectedFailure) {
        }
        return 0;
    }

    private static int dataInputPrimitiveSemantics() throws Exception {
        byte[] encoded = new byte[] {
            0x12, 0x34, 0x56, 0x78,
            (byte) 0xFE, (byte) 0xDC,
            0x01, 0x23, 0x45, 0x67,
            (byte) 0x89, (byte) 0xAB, (byte) 0xCD, (byte) 0xEF
        };
        DataInputStream input = new DataInputStream(
                new ByteArrayInputStream(encoded));
        if (input.readInt() != 0x12345678) return 133;
        if (input.readUnsignedShort() != 0xFEDC) return 134;
        if (input.readLong() != 0x0123456789ABCDEFL) return 135;

        ByteArrayInputStream truncatedSource = new ByteArrayInputStream(
                new byte[] {1, 2});
        DataInputStream truncated = new DataInputStream(truncatedSource);
        try {
            truncated.readInt();
            return 136;
        } catch (java.io.EOFException expected) {
        }
        if (truncatedSource.available() != 0) return 137;
        return 0;
    }

    private static int wrapperCompatibility() {
        if (!Float.isNaN(Float.intBitsToFloat(0x7fc00000))) return 151;
        if (Float.isNaN(1.0f)) return 152;
        if (Integer.valueOf("7fffffff", 16).intValue() != 0x7fffffff) {
            return 153;
        }
        try {
            Integer.valueOf("xyz", 10);
            return 154;
        } catch (NumberFormatException expected) {
        }
        return 0;
    }

    private static int objectAndThreadCompatibility() throws Exception {
        Object lock = new Object();
        synchronized (lock) {
            try {
                lock.wait(-1L, 0);
                return 161;
            } catch (IllegalArgumentException expected) {
            }
            try {
                lock.wait(0L, 1000000);
                return 162;
            } catch (IllegalArgumentException expected) {
            }
        }

        Thread named = new Thread("worker");
        if (!"worker".equals(named.getName())) return 163;
        named.checkAccess();
        if (!"Thread[worker,5]".equals(named.toString())) return 164;

        Thread generated = new Thread();
        if (generated.getName() == null ||
                !generated.getName().startsWith("Thread-")) return 165;
        return 0;
    }

    private static int permissionCompatibility() throws Exception {
        RuntimePermission wildcard = new RuntimePermission("loadLibrary.*");
        RuntimePermission requested = new RuntimePermission("loadLibrary.game");
        if (!wildcard.implies(requested)) return 166;
        if (wildcard.implies(new RuntimePermission("loadLibrary"))) return 167;
        if (!wildcard.equals(new RuntimePermission("loadLibrary.*"))) return 168;
        if (wildcard.hashCode() != new RuntimePermission("loadLibrary.*").hashCode()) {
            return 169;
        }
        if (!"".equals(wildcard.getActions())) return 170;

        java.security.PermissionCollection basic =
            wildcard.newPermissionCollection();
        basic.add(wildcard);
        if (!basic.implies(requested)) return 171;
        if (!basic.elements().hasMoreElements()) return 172;
        basic.setReadOnly();
        if (!basic.isReadOnly()) return 173;
        try {
            basic.add(new RuntimePermission("loadLibrary.other"));
            return 174;
        } catch (SecurityException expected) {
        }

        java.util.PropertyPermission read =
            new java.util.PropertyPermission("java.*", "read");
        java.util.PropertyPermission write =
            new java.util.PropertyPermission("java.home", "write");
        java.util.PropertyPermission both =
            new java.util.PropertyPermission("java.home", "read,write");
        if (!read.implies(new java.util.PropertyPermission("java.home", "read"))) {
            return 175;
        }
        if (read.implies(both)) return 176;
        if (!"read,write".equals(both.getActions())) return 177;

        java.security.PermissionCollection properties =
            read.newPermissionCollection();
        properties.add(read);
        properties.add(write);
        if (!properties.implies(both)) return 178;

        java.security.AccessControlException exception =
            new java.security.AccessControlException("denied", requested);
        if (exception.getPermission() != requested) return 179;
        if (!"denied".equals(exception.getMessage())) return 180;
        java.security.AccessController.checkPermission(requested);
        return 0;
    }

    private static int throwableSemantics() throws Exception {
        IOException outer = new IOException("hỏng");
        Exception inner = new Exception("gốc");
        if (!"hỏng".equals(outer.getMessage())) return 141;
        if (!"java.io.IOException: hỏng".equals(outer.toString())) return 142;
        if (outer.getCause() != null) return 143;
        if (outer.initCause(inner) != outer || outer.getCause() != inner) return 144;
        try {
            outer.initCause(null);
            return 145;
        } catch (IllegalStateException expected) {
        }
        try {
            inner.initCause(inner);
            return 146;
        } catch (IllegalArgumentException expected) {
        }
        return 0;
    }

    private static int standardCharsetsCompatibility() throws Exception {
        java.nio.charset.Charset utf8 = java.nio.charset.StandardCharsets.UTF_8;
        if (utf8 == null) return 210;
        if (!"UTF-8".equals(utf8.name())) return 211;
        if (!"UTF-8".equals(utf8.toString())) return 212;
        if (!"US-ASCII".equals(
                java.nio.charset.StandardCharsets.US_ASCII.name())) return 213;
        if (!"ISO-8859-1".equals(
                java.nio.charset.StandardCharsets.ISO_8859_1.name())) return 214;
        if (!"UTF-16BE".equals(
                java.nio.charset.StandardCharsets.UTF_16BE.name())) return 215;

        java.nio.charset.Charset lookedUp =
                java.nio.charset.Charset.forName("utf_8");
        if (!"UTF-8".equals(lookedUp.name())) return 216;
        java.nio.charset.Charset latin1 =
                java.nio.charset.Charset.forName("latin1");
        if (!"ISO-8859-1".equals(latin1.name())) return 217;
        ByteArrayOutputStream latinBytes = new ByteArrayOutputStream();
        OutputStreamWriter latinWriter =
                new OutputStreamWriter(latinBytes, latin1);
        latinWriter.write("\u00E9\u20AC");
        latinWriter.close();
        byte[] latinEncoded = latinBytes.toByteArray();
        if (latinEncoded.length != 2 ||
                (latinEncoded[0] & 0xFF) != 0xE9 ||
                latinEncoded[1] != 63) return 218;
        try {
            java.nio.charset.Charset.forName(null);
            return 219;
        } catch (NullPointerException expected) {
        }

        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        OutputStreamWriter writer = new OutputStreamWriter(bytes, utf8);
        writer.write("Tiếng Việt");
        writer.close();
        InputStreamReader reader = new InputStreamReader(
                new ByteArrayInputStream(bytes.toByteArray()), utf8);
        char[] decoded = new char[32];
        int count = reader.read(decoded, 0, decoded.length);
        if (!"Tiếng Việt".equals(new String(decoded, 0, count))) return 213;

        try {
            new InputStreamReader(new ByteArrayInputStream(new byte[0]),
                    (java.nio.charset.Charset)null);
            return 214;
        } catch (NullPointerException expected) {
        }
        try {
            new OutputStreamWriter(new ByteArrayOutputStream(),
                    (java.nio.charset.Charset)null);
            return 215;
        } catch (NullPointerException expected) {
        }
        return 0;
    }

    private static int messageDigestCompatibility() throws Exception {
        java.security.MessageDigest digest =
                java.security.MessageDigest.getInstance("SHA-256");
        if (!"SHA-256".equals(digest.getAlgorithm()) ||
                digest.getDigestLength() != 32) return 220;
        digest.update((byte)'a');
        digest.update(new byte[] {(byte)'b', (byte)'c'}, 0, 2);
        byte[] actual = digest.digest();
        int[] expected = new int[] {
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
            0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
            0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
            0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
        };
        if (actual.length != expected.length) return 221;
        for (int i = 0; i < expected.length; i++) {
            if ((actual[i] & 0xFF) != expected[i]) return 222;
        }
        byte[] oneShot = java.security.MessageDigest.getInstance("sha_256")
                .digest(new byte[] {(byte)'a', (byte)'b', (byte)'c'});
        if (!java.security.MessageDigest.isEqual(actual, oneShot)) return 223;
        if (java.security.MessageDigest.isEqual(actual, new byte[31])) return 224;
        digest.update((byte)7);
        digest.reset();
        if (digest.digest().length != 32) return 225;
        try {
            java.security.MessageDigest.getInstance("MD5");
            return 226;
        } catch (java.security.NoSuchAlgorithmException expectedFailure) {
        }
        return 0;
    }

    private static int cldcHierarchyAndTimeZone() throws Exception {
        Object integer = Integer.valueOf(7);
        if (integer instanceof Number) return 181;

        Object stream = new PrintStream(new ByteArrayOutputStream());
        if (stream instanceof java.io.FilterOutputStream) return 182;

        java.util.TimeZone zone = java.util.TimeZone.getTimeZone("GMT+07:00");
        if (!"GMT+07:00".equals(zone.getID())) return 183;
        if (zone.getRawOffset() != 7 * 60 * 60 * 1000) return 184;
        if (zone.getOffset(1, 2026, 7, 4, 3, 0) !=
                7 * 60 * 60 * 1000) return 185;
        try {
            zone.getOffset(1, 2026, 12, 1, 1, 0);
            return 186;
        } catch (IllegalArgumentException expected) {
        }
        String[] ids = java.util.TimeZone.getAvailableIDs();
        if (ids.length < 2 || !"GMT".equals(ids[0]) ||
                !"UTC".equals(ids[1])) return 187;
        return 0;
    }

    public static int versionProperties() {
        if (!"MIDP-2.1".equals(
                System.getProperty("microedition.profiles"))) return 191;
        if (!"CLDC-1.1.1".equals(
                System.getProperty("microedition.configuration"))) return 192;
        return 0;
    }

    public static int runAll() {
        try {
            int result = readerWriterRoundTrip();
            if (result != 0) return result;
            result = encodingAliases();
            if (result != 0) return result;
            result = stringSplitSemantics();
            if (result != 0) return result;
            result = customOutputDispatch();
            if (result != 0) return result;
            result = modifiedUtfRoundTrip();
            if (result != 0) return result;
            result = dataInputPrimitiveSemantics();
            if (result != 0) return result;
            result = wrapperCompatibility();
            if (result != 0) return result;
            result = objectAndThreadCompatibility();
            if (result != 0) return result;
            result = permissionCompatibility();
            if (result != 0) return result;
            result = throwableSemantics();
            if (result != 0) return result;
            result = standardCharsetsCompatibility();
            if (result != 0) return result;
            result = messageDigestCompatibility();
            if (result != 0) return result;
            return cldcHierarchyAndTimeZone();
        } catch (Throwable failure) {
            failure.printStackTrace();
            return 199;
        }
    }
}
