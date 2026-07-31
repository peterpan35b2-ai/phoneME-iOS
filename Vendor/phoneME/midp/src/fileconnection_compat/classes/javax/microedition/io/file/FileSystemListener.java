package javax.microedition.io.file;

/** Receives notifications when a filesystem root is added or removed. */
public interface FileSystemListener {
    int ROOT_ADDED = 0;
    int ROOT_REMOVED = 1;

    void rootChanged(int state, String rootName);
}
