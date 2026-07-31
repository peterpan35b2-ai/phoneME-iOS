package com.sun.midp.io.j2me.file;

import com.sun.cldc.io.ConnectionBaseInterface;
import com.sun.midp.io.j2me.storage.File;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Enumeration;
import java.util.Vector;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.file.FileConnection;

/** iOS-backed JSR-75 file protocol exposed as file:///Phone/. */
public final class Protocol implements FileConnection, ConnectionBaseInterface {
    private static final String ROOT_NAME = "Phone";
    private static final String ROOT_VIRTUAL = "/Phone/";
    private static final String ROOT_NATIVE = buildNativeRoot();

    private String virtualPath;
    private int mode;
    private boolean open;
    private ManagedInputStream inputStream;
    private ManagedOutputStream outputStream;

    static {
        nEnsureDirectory(ROOT_NATIVE);
    }

    public Connection openPrim(String name, int requestedMode, boolean timeouts)
            throws IOException {
        if (requestedMode != Connector.READ && requestedMode != Connector.WRITE &&
                requestedMode != Connector.READ_WRITE) {
            throw new IllegalArgumentException("Invalid connection mode");
        }
        virtualPath = normalizeVirtualPath(name);
        mode = requestedMode;
        open = true;
        return this;
    }

    public synchronized long totalSize() {
        ensureOpenUnchecked();
        return nSpace(ROOT_NATIVE, 0);
    }

    public synchronized long availableSize() {
        ensureOpenUnchecked();
        return nSpace(ROOT_NATIVE, 1);
    }

    public synchronized long usedSize() {
        ensureOpenUnchecked();
        return nSpace(ROOT_NATIVE, 2);
    }

    public synchronized long directorySize(boolean includeSubDirs)
            throws IOException {
        ensureOpen();
        requireExists();
        if (!isDirectory()) {
            throw new IOException("Not a directory");
        }
        return nDirectorySize(nativePath(), includeSubDirs);
    }

    public synchronized long fileSize() throws IOException {
        ensureOpen();
        requireExists();
        if (isDirectory()) {
            throw new IOException("Connection refers to a directory");
        }
        return nSize(nativePath());
    }

    public synchronized boolean canRead() {
        ensureOpenUnchecked();
        return nCanRead(nativePath());
    }

    public synchronized boolean canWrite() {
        ensureOpenUnchecked();
        return nCanWrite(nativePath());
    }

    public synchronized boolean isHidden() {
        ensureOpenUnchecked();
        String name = getName();
        return name.length() > 0 && name.charAt(0) == '.';
    }

    public synchronized void setReadable(boolean readable) throws IOException {
        ensureOpen();
        requireExists();
        nSetReadable(nativePath(), readable);
    }

    public synchronized void setWritable(boolean writable) throws IOException {
        ensureOpen();
        requireExists();
        nSetWritable(nativePath(), writable);
    }

    public synchronized void setHidden(boolean hidden) throws IOException {
        ensureOpen();
        requireNoStreams();
        requireExists();
        if (isRoot()) {
            throw new IOException("Cannot hide filesystem root");
        }
        boolean current = isHidden();
        if (current == hidden) {
            return;
        }
        String name = getName();
        boolean directory = name.endsWith("/");
        if (directory) {
            name = name.substring(0, name.length() - 1);
        }
        String newName = hidden ? "." + name : name.substring(1);
        rename(newName + (directory ? "/" : ""));
    }

    public synchronized String getName() {
        ensureOpenUnchecked();
        if (isRoot()) {
            return "";
        }
        boolean directory = virtualPath.endsWith("/");
        int end = directory ? virtualPath.length() - 1 : virtualPath.length();
        int slash = virtualPath.lastIndexOf('/', end - 1);
        String result = virtualPath.substring(slash + 1, end);
        return directory ? result + "/" : result;
    }

    public synchronized String getPath() {
        ensureOpenUnchecked();
        if (isRoot()) {
            return ROOT_VIRTUAL;
        }
        boolean directory = virtualPath.endsWith("/");
        int end = directory ? virtualPath.length() - 1 : virtualPath.length();
        int slash = virtualPath.lastIndexOf('/', end - 1);
        return virtualPath.substring(0, slash + 1);
    }

    public synchronized String getURL() {
        ensureOpenUnchecked();
        return "file://" + virtualPath;
    }

    public synchronized boolean exists() {
        ensureOpenUnchecked();
        return nExists(nativePath());
    }

    public synchronized boolean isDirectory() {
        ensureOpenUnchecked();
        return nIsDirectory(nativePath());
    }

    public synchronized long lastModified() {
        ensureOpenUnchecked();
        return nLastModified(nativePath());
    }

    public Enumeration list() throws IOException {
        return list("*", false);
    }

    public synchronized Enumeration list(String filter, boolean includeHidden)
            throws IOException {
        ensureOpen();
        requireExists();
        if (!isDirectory()) {
            throw new IOException("Not a directory");
        }
        if (filter == null) {
            throw new NullPointerException("filter is null");
        }
        if (filter.indexOf('/') >= 0 || filter.indexOf(':') >= 0) {
            throw new IllegalArgumentException("Filter must contain a filename only");
        }

        byte[] encoded = nList(nativePath());
        Vector matches = new Vector();
        if (encoded == null || encoded.length == 0) {
            return matches.elements();
        }
        String names = new String(encoded, "UTF-8");
        int start = 0;
        while (start < names.length()) {
            int end = names.indexOf('\u0000', start);
            if (end < 0) {
                end = names.length();
            }
            String name = names.substring(start, end);
            if (name.length() > 0) {
                String plainName = name.endsWith("/")
                        ? name.substring(0, name.length() - 1) : name;
                if ((includeHidden || plainName.charAt(0) != '.') &&
                        wildcardMatch(filter, name)) {
                    matches.addElement(name);
                }
            }
            start = end + 1;
        }
        return matches.elements();
    }

    public synchronized void create() throws IOException {
        ensureOpen();
        requireWritableMode();
        requireNoStreams();
        if (virtualPath.endsWith("/")) {
            throw new IOException("File URL ends with a directory separator");
        }
        if (exists()) {
            throw new IOException("File already exists");
        }
        nCreateFile(nativePath());
    }

    public synchronized void mkdir() throws IOException {
        ensureOpen();
        requireWritableMode();
        requireNoStreams();
        if (exists()) {
            throw new IOException("File already exists");
        }
        nMkdir(nativePath());
        if (!virtualPath.endsWith("/")) {
            virtualPath += "/";
        }
    }

    public synchronized void delete() throws IOException {
        ensureOpen();
        requireWritableMode();
        requireNoStreams();
        requireExists();
        if (isRoot()) {
            throw new IOException("Cannot delete filesystem root");
        }
        nDelete(nativePath());
    }

    public synchronized void rename(String newName) throws IOException {
        ensureOpen();
        requireWritableMode();
        requireNoStreams();
        requireExists();
        if (isRoot()) {
            throw new IOException("Cannot rename filesystem root");
        }
        if (newName == null) {
            throw new NullPointerException("newName is null");
        }
        if (newName.length() == 0 || newName.indexOf('/') >= 0 ||
                newName.indexOf('\\') >= 0 || newName.indexOf(':') >= 0 ||
                ".".equals(newName) || "..".equals(newName)) {
            throw new IllegalArgumentException("Invalid new filename");
        }

        boolean directory = isDirectory();
        String parent = getPath();
        String normalizedName = decodePercent(newName);
        String newVirtualPath = parent + normalizedName + (directory ? "/" : "");
        String oldNativePath = nativePath();
        String newNativePath = nativePath(newVirtualPath);
        nRename(oldNativePath, newNativePath);
        virtualPath = newVirtualPath;
    }

    public synchronized void truncate(long byteOffset) throws IOException {
        ensureOpen();
        requireWritableMode();
        requireNoStreams();
        requireExists();
        if (isDirectory()) {
            throw new IOException("Cannot truncate a directory");
        }
        if (byteOffset < 0 || byteOffset > fileSize()) {
            throw new IllegalArgumentException("Invalid truncate offset");
        }
        nTruncate(nativePath(), byteOffset);
    }

    public synchronized InputStream openInputStream() throws IOException {
        ensureOpen();
        requireReadableMode();
        requireExists();
        if (isDirectory()) {
            throw new IOException("Cannot read a directory");
        }
        if (inputStream != null) {
            throw new IOException("Input stream already open");
        }
        byte[] data = nReadAll(nativePath());
        if (data == null) {
            throw new IOException("Unable to read file");
        }
        inputStream = new ManagedInputStream(data);
        return inputStream;
    }

    public synchronized DataInputStream openDataInputStream() throws IOException {
        return new DataInputStream(openInputStream());
    }

    public OutputStream openOutputStream() throws IOException {
        return openOutputStream(0);
    }

    public synchronized OutputStream openOutputStream(long byteOffset)
            throws IOException {
        ensureOpen();
        requireWritableMode();
        requireExists();
        if (isDirectory()) {
            throw new IOException("Cannot write a directory");
        }
        if (outputStream != null) {
            throw new IOException("Output stream already open");
        }
        if (byteOffset < 0 || byteOffset > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("Invalid output offset");
        }
        byte[] data = nReadAll(nativePath());
        if (data == null) {
            data = new byte[0];
        }
        outputStream = new ManagedOutputStream(data, (int)byteOffset);
        return outputStream;
    }

    public synchronized DataOutputStream openDataOutputStream()
            throws IOException {
        return new DataOutputStream(openOutputStream());
    }

    public synchronized void close() throws IOException {
        if (!open) {
            return;
        }
        IOException failure = null;
        if (inputStream != null) {
            try {
                inputStream.close();
            } catch (IOException error) {
                failure = error;
            }
        }
        if (outputStream != null) {
            try {
                outputStream.close();
            } catch (IOException error) {
                failure = error;
            }
        }
        open = false;
        if (failure != null) {
            throw failure;
        }
    }

    private static String buildNativeRoot() {
        String storage = File.getStorageRoot(0);
        if (!storage.endsWith("/")) {
            storage += "/";
        }
        return storage + "jsr75/Phone";
    }

    private String nativePath() {
        return nativePath(virtualPath);
    }

    private static String nativePath(String path) {
        String suffix = path.substring(("/" + ROOT_NAME).length());
        return ROOT_NATIVE + suffix;
    }

    private boolean isRoot() {
        return ROOT_VIRTUAL.equals(virtualPath);
    }

    private void ensureOpen() throws IOException {
        if (!open) {
            throw new IOException("Connection is closed");
        }
    }

    private void ensureOpenUnchecked() {
        if (!open) {
            throw new IllegalStateException("Connection is closed");
        }
    }

    private void requireExists() throws IOException {
        if (!exists()) {
            throw new IOException("File does not exist");
        }
    }

    private void requireNoStreams() throws IOException {
        if (inputStream != null || outputStream != null) {
            throw new IOException("A stream is still open");
        }
    }

    private void requireReadableMode() throws IOException {
        if (mode == Connector.WRITE) {
            throw new IOException("Connection is write-only");
        }
    }

    private void requireWritableMode() throws IOException {
        if (mode == Connector.READ) {
            throw new IOException("Connection is read-only");
        }
    }

    private static String normalizeVirtualPath(String original) {
        if (original == null) {
            throw new NullPointerException("file URL is null");
        }
        String path = original.replace('\\', '/');
        if (path.startsWith("//localhost/")) {
            path = path.substring(11);
        }
        while (path.startsWith("//")) {
            path = path.substring(1);
        }
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
        path = decodePercent(path);
        boolean trailingSlash = path.endsWith("/");
        Vector segments = new Vector();
        int start = 1;
        while (start <= path.length()) {
            int slash = path.indexOf('/', start);
            if (slash < 0) slash = path.length();
            String segment = path.substring(start, slash);
            if (segment.length() > 0 && !".".equals(segment)) {
                if ("..".equals(segment) || segment.indexOf('\u0000') >= 0) {
                    throw new IllegalArgumentException("Unsafe file path");
                }
                segments.addElement(segment);
            }
            if (slash == path.length()) break;
            start = slash + 1;
        }
        if (segments.size() == 0) {
            return ROOT_VIRTUAL;
        }
        if (!ROOT_NAME.equals(segments.elementAt(0))) {
            throw new IllegalArgumentException("Unknown filesystem root");
        }
        StringBuffer result = new StringBuffer();
        for (int i = 0; i < segments.size(); i++) {
            result.append('/').append((String)segments.elementAt(i));
        }
        if (trailingSlash || segments.size() == 1) {
            result.append('/');
        }
        return result.toString();
    }

    private static String decodePercent(String value) {
        StringBuffer result = new StringBuffer(value.length());
        for (int i = 0; i < value.length(); i++) {
            char character = value.charAt(i);
            if (character == '%' && i + 2 < value.length()) {
                int high = hex(value.charAt(i + 1));
                int low = hex(value.charAt(i + 2));
                if (high >= 0 && low >= 0) {
                    result.append((char)((high << 4) | low));
                    i += 2;
                    continue;
                }
            }
            result.append(character);
        }
        return result.toString();
    }

    private static int hex(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    private static boolean wildcardMatch(String pattern, String value) {
        int patternIndex = 0;
        int valueIndex = 0;
        int star = -1;
        int retry = -1;
        while (valueIndex < value.length()) {
            if (patternIndex < pattern.length() &&
                    (pattern.charAt(patternIndex) == '?' ||
                    pattern.charAt(patternIndex) == value.charAt(valueIndex))) {
                patternIndex++;
                valueIndex++;
            } else if (patternIndex < pattern.length() &&
                    pattern.charAt(patternIndex) == '*') {
                star = patternIndex++;
                retry = valueIndex;
            } else if (star >= 0) {
                patternIndex = star + 1;
                valueIndex = ++retry;
            } else {
                return false;
            }
        }
        while (patternIndex < pattern.length() &&
                pattern.charAt(patternIndex) == '*') {
            patternIndex++;
        }
        return patternIndex == pattern.length();
    }

    private final class ManagedInputStream extends ByteArrayInputStream {
        private boolean closed;

        ManagedInputStream(byte[] data) {
            super(data);
        }

        public void close() throws IOException {
            if (!closed) {
                closed = true;
                super.close();
                synchronized (Protocol.this) {
                    inputStream = null;
                }
            }
        }
    }

    private final class ManagedOutputStream extends OutputStream {
        private byte[] buffer;
        private int length;
        private int position;
        private boolean closed;
        private boolean dirty;

        ManagedOutputStream(byte[] existing, int byteOffset) {
            int capacity = existing.length > byteOffset ? existing.length : byteOffset;
            if (capacity < 32) capacity = 32;
            buffer = new byte[capacity];
            System.arraycopy(existing, 0, buffer, 0, existing.length);
            length = existing.length;
            position = byteOffset;
            if (position > length) {
                length = position;
                dirty = true;
            }
        }

        public void write(int value) throws IOException {
            ensureStreamOpen();
            ensureCapacity(position + 1);
            buffer[position++] = (byte)value;
            if (position > length) length = position;
            dirty = true;
        }

        public void write(byte[] data, int offset, int count)
                throws IOException {
            ensureStreamOpen();
            if (data == null) throw new NullPointerException("data is null");
            if (offset < 0 || count < 0 || offset + count > data.length) {
                throw new IndexOutOfBoundsException();
            }
            ensureCapacity(position + count);
            System.arraycopy(data, offset, buffer, position, count);
            position += count;
            if (position > length) length = position;
            dirty = true;
        }

        public void flush() throws IOException {
            ensureStreamOpen();
            commit();
        }

        public void close() throws IOException {
            if (closed) return;
            IOException failure = null;
            try {
                commit();
            } catch (IOException error) {
                failure = error;
            }
            closed = true;
            synchronized (Protocol.this) {
                outputStream = null;
            }
            if (failure != null) throw failure;
        }

        private void commit() throws IOException {
            if (!dirty) return;
            byte[] exact = new byte[length];
            System.arraycopy(buffer, 0, exact, 0, length);
            nWriteAll(nativePath(), exact);
            dirty = false;
        }

        private void ensureStreamOpen() throws IOException {
            if (closed) throw new IOException("Output stream is closed");
        }

        private void ensureCapacity(int required) throws IOException {
            if (required < 0) throw new IOException("File is too large");
            if (required <= buffer.length) return;
            int capacity = buffer.length;
            while (capacity < required) {
                int next = capacity < 1048576 ? capacity * 2 : capacity + 1048576;
                if (next <= capacity || next < required && next < 0) {
                    capacity = required;
                    break;
                }
                capacity = next;
            }
            byte[] expanded = new byte[capacity];
            System.arraycopy(buffer, 0, expanded, 0, length);
            buffer = expanded;
        }
    }

    private static native void nEnsureDirectory(String path);
    private static native boolean nExists(String path);
    private static native boolean nIsDirectory(String path);
    private static native long nSize(String path);
    private static native long nLastModified(String path);
    private static native boolean nCanRead(String path);
    private static native boolean nCanWrite(String path);
    private static native void nSetReadable(String path, boolean readable)
            throws IOException;
    private static native void nSetWritable(String path, boolean writable)
            throws IOException;
    private static native void nCreateFile(String path) throws IOException;
    private static native void nMkdir(String path) throws IOException;
    private static native void nDelete(String path) throws IOException;
    private static native void nRename(String oldPath, String newPath)
            throws IOException;
    private static native void nTruncate(String path, long size)
            throws IOException;
    private static native byte[] nReadAll(String path) throws IOException;
    private static native void nWriteAll(String path, byte[] data)
            throws IOException;
    private static native byte[] nList(String path) throws IOException;
    private static native long nDirectorySize(String path,
            boolean includeSubDirs) throws IOException;
    private static native long nSpace(String root, int kind);
}
