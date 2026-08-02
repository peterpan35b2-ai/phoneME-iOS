package javax.microedition.io;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public final class Connector {
    public static final int READ = 1;
    public static final int WRITE = 2;
    public static final int READ_WRITE = 3;
    public static native Connection open(String name) throws IOException;
    public static native Connection open(String name, int mode) throws IOException;
    public static native Connection open(String name, int mode, boolean timeouts) throws IOException;
    public static native InputStream openInputStream(String name) throws IOException;
    public static native DataInputStream openDataInputStream(String name) throws IOException;
    public static native OutputStream openOutputStream(String name) throws IOException;
    public static native DataOutputStream openDataOutputStream(String name) throws IOException;
}
