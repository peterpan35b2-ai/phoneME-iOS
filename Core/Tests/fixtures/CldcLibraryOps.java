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
            result = customOutputDispatch();
            if (result != 0) return result;
            result = modifiedUtfRoundTrip();
            if (result != 0) return result;
            return throwableSemantics();
        } catch (Throwable failure) {
            failure.printStackTrace();
            return 199;
        }
    }
}
