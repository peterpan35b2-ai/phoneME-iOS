package javax.microedition.io.file;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Enumeration;
import javax.microedition.io.StreamConnection;

public interface FileConnection extends StreamConnection {
    long fileSize() throws IOException;
    long directorySize(boolean includeSubDirs) throws IOException;
    long availableSize();
    long totalSize();
    long usedSize();
    boolean isOpen();
    boolean canRead();
    boolean canWrite();
    void setReadable(boolean readable) throws IOException;
    void setWritable(boolean writable) throws IOException;
    boolean isHidden();
    void setHidden(boolean hidden) throws IOException;
    String getName();
    String getPath();
    String getURL();
    boolean exists();
    boolean isDirectory();
    long lastModified();
    void create() throws IOException;
    void mkdir() throws IOException;
    void delete() throws IOException;
    void rename(String newName) throws IOException;
    void setFileConnection(String fileName) throws IOException;
    void truncate(long byteOffset) throws IOException;
    Enumeration list() throws IOException;
    Enumeration list(String filter, boolean includeHidden) throws IOException;
    InputStream openInputStream() throws IOException;
    DataInputStream openDataInputStream() throws IOException;
    OutputStream openOutputStream() throws IOException;
    OutputStream openOutputStream(long byteOffset) throws IOException;
    DataOutputStream openDataOutputStream() throws IOException;
}
