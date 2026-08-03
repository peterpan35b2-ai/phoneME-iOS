package javax.microedition.io.file;

import java.util.Enumeration;

public final class FileSystemRegistry {
    private FileSystemRegistry() {
    }

    public static native Enumeration listRoots();
}
