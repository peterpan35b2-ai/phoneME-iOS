package javax.microedition.io.file;

import java.util.Enumeration;
import java.util.Vector;

/** Registry for the stable iOS-backed JSR-75 filesystem roots. */
public final class FileSystemRegistry {
    private static final Vector listeners = new Vector();

    private FileSystemRegistry() {
    }

    public static Enumeration listRoots() {
        Vector roots = new Vector();
        roots.addElement("Phone/");
        return roots.elements();
    }

    public static boolean addFileSystemListener(FileSystemListener listener) {
        if (listener == null) {
            throw new NullPointerException("listener is null");
        }
        synchronized (listeners) {
            if (!listeners.contains(listener)) {
                listeners.addElement(listener);
            }
        }
        return true;
    }

    public static boolean removeFileSystemListener(FileSystemListener listener) {
        if (listener == null) {
            throw new NullPointerException("listener is null");
        }
        synchronized (listeners) {
            return listeners.removeElement(listener);
        }
    }
}
