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
        if (!"UTF-8".equals(writer.getEncoding())) return 101;
        writer.flush();

        InputStreamReader reader = new InputStreamReader(
                new ByteArrayInputStream(bytes.toByteArray()), "utf-8");
        if (!"UTF-8".equals(reader.getEncoding())) return 102;
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
        if (!"ISO-8859-1".equals(defaultReader.getEncoding()) ||
                defaultReader.read() != 0x00E9 ||
                defaultReader.read() != -1) return 114;
        ByteArrayOutputStream defaultBytes = new ByteArrayOutputStream();
        OutputStreamWriter defaultWriter = new OutputStreamWriter(defaultBytes);
        if (!"ISO-8859-1".equals(defaultWriter.getEncoding())) return 115;
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

        String[] unchanged = "plain".split(",");
        if (unchanged.length != 1 || !"plain".equals(unchanged[0])) return 122;
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
            result = wrapperCompatibility();
            if (result != 0) return result;
            result = objectAndThreadCompatibility();
            if (result != 0) return result;
            result = permissionCompatibility();
            if (result != 0) return result;
            return throwableSemantics();
        } catch (Throwable failure) {
            failure.printStackTrace();
            return 199;
        }
    }
}
