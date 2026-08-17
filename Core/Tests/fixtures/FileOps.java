package corefixture;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Enumeration;
import javax.microedition.io.Connector;
import javax.microedition.io.file.ConnectionClosedException;
import javax.microedition.io.file.FileConnection;
import javax.microedition.io.file.FileSystemListener;
import javax.microedition.io.file.FileSystemRegistry;
import javax.microedition.io.file.IllegalModeException;

public final class FileOps {
    private FileOps() {
    }

    public static int resourceLookup() throws Exception {
        if (String[].class.getResourceAsStream("") != null) {
            return 0;
        }
        InputStream relative = FileOps.class.getResourceAsStream("data.bin");
        InputStream absolute =
            FileOps.class.getResourceAsStream("/corefixture/data.bin");
        InputStream packageFallback =
            FileOps.class.getResourceAsStream("/data.bin");
        if (relative == null || absolute == null || packageFallback == null) {
            return 0;
        }
        int relativeCount = 0;
        int absoluteCount = 0;
        int fallbackCount = 0;
        int first = relative.read();
        int value;
        while ((value = relative.read()) >= 0) {
            relativeCount++;
        }
        while (absolute.read() >= 0) {
            absoluteCount++;
        }
        while (packageFallback.read() >= 0) {
            fallbackCount++;
        }
        relative.close();
        absolute.close();
        packageFallback.close();
        return first == 'P' && relativeCount == 16 && absoluteCount == 17 &&
            fallbackCount == 17 ? 1 : 0;
    }

    public static int classLoaderResourceLookup() throws Exception {
        ClassLoader loader = FileOps.class.getClassLoader();
        if (loader == null) {
            return 0;
        }
        InputStream resource = loader.getResourceAsStream("corefixture/data.bin");
        if (resource == null) {
            return 0;
        }
        int first = resource.read();
        int count = first >= 0 ? 1 : 0;
        while (resource.read() >= 0) {
            count++;
        }
        resource.close();
        return first == 'P' && count == 17 ? 1 : 0;
    }

    public static int resourceTraversalBlocked() {
        try {
            FileOps.class.getResourceAsStream("../../escape.bin");
            return 0;
        } catch (SecurityException expected) {
            return 1;
        }
    }

    public static int fileRoundTrip() throws Exception {
        FileConnection directory = (FileConnection) Connector.open(
            "file:///save", Connector.READ_WRITE);
        if (directory.exists()) {
            directory.delete();
        }
        directory.mkdir();

        FileConnection file = (FileConnection) Connector.open(
            "file:///save/data.bin", Connector.READ_WRITE);
        file.create();
        DataOutputStream output = file.openDataOutputStream();
        output.writeInt(0x12345678);
        output.writeUTF("phoneME");
        output.close();

        if (file.fileSize() <= 4) {
            return 0;
        }
        DataInputStream input = file.openDataInputStream();
        int number = input.readInt();
        String text = input.readUTF();
        input.close();
        if (number != 0x12345678 || !"phoneME".equals(text)) {
            return 0;
        }

        file.rename("renamed.bin");
        if (!file.exists() || !"renamed.bin".equals(file.getName())) {
            return 0;
        }
        Enumeration entries = directory.list("*.bin", false);
        if (!entries.hasMoreElements() ||
            !"renamed.bin".equals((String) entries.nextElement()) ||
            entries.hasMoreElements()) {
            return 0;
        }

        file.delete();
        file.close();
        directory.delete();
        directory.close();
        return 1;
    }

    public static int javaIoFileCompatibility() throws Exception {
        File directory = new File("jdk8-file");
        if (directory.exists()) {
            directory.delete();
        }
        if (!directory.mkdir() || !directory.exists()
                || !directory.isDirectory()) {
            return 0;
        }
        File nested = new File("jdk8-file/nested/leaf");
        if (!nested.mkdirs() || !nested.isDirectory()) {
            return 0;
        }
        File nestedParent = nested.getParentFile();
        if (nestedParent == null || !nestedParent.isDirectory()
                || !"jdk8-file/nested".equals(nestedParent.getPath())) {
            return 0;
        }

        File file = new File("jdk8-file/data.bin");
        FileOutputStream output = new FileOutputStream(file);
        output.write(new byte[] {1, 2, 3, 4});
        output.close();
        if (!file.exists() || !file.isFile() || file.length() != 4L) {
            return 0;
        }
        FileInputStream input = new FileInputStream(file);
        int total = 0;
        int value;
        while ((value = input.read()) >= 0) {
            total += value;
        }
        input.close();
        if (total != 10) {
            return 0;
        }

        File renamed = new File("jdk8-file/renamed.bin");
        if (!file.renameTo(renamed) || file.exists()
                || !renamed.exists() || renamed.length() != 4L) {
            return 0;
        }

        FileConnection connection = (FileConnection) Connector.open(
            "file:///jdk8-file", Connector.READ_WRITE);
        connection.setFileConnection("renamed.bin");
        if (!"renamed.bin".equals(connection.getName())
                || connection.fileSize() != 4L) {
            return 0;
        }
        connection.setFileConnection("..");
        if (!connection.isDirectory()) {
            return 0;
        }
        connection.close();

        if (!renamed.delete() || !nested.delete()
                || !nestedParent.delete() || !directory.delete()) {
            return 0;
        }
        return 1;
    }

    public static int suppressedExceptions() {
        Throwable primary = new Exception("primary");
        Throwable first = new IOException("first");
        Throwable second = new IllegalStateException("second");
        primary.addSuppressed(first);
        primary.addSuppressed(second);
        Throwable[] suppressed = primary.getSuppressed();
        if (suppressed.length != 2 || suppressed[0] != first
                || suppressed[1] != second) {
            return 0;
        }
        suppressed[0] = second;
        if (primary.getSuppressed()[0] != first) {
            return 0;
        }
        try {
            primary.addSuppressed(null);
            return 0;
        } catch (NullPointerException expected) {
            // Expected.
        }
        try {
            primary.addSuppressed(primary);
            return 0;
        } catch (IllegalArgumentException expected) {
            // Expected.
        }
        return 1;
    }

    public static int traversalBlocked() {
        try {
            Connector.open("file:///../escape.bin", Connector.READ_WRITE);
            return 0;
        } catch (SecurityException expected) {
            return 1;
        } catch (IOException wrongException) {
            return -1;
        }
    }

    public static int closedHandleRejected() throws Exception {
        FileOutputStream output = new FileOutputStream("closed.bin");
        output.write(7);
        output.close();
        output.close();
        try {
            output.write(8);
            return 0;
        } catch (IOException expected) {
            FileConnection file = (FileConnection) Connector.open(
                "file:///closed.bin", Connector.READ_WRITE);
            file.delete();
            file.close();
            file.close();
            return 1;
        }
    }

    public static int offsetAndOpenHandlePolicy() throws Exception {
        FileConnection file = (FileConnection) Connector.open(
            "file:///offset.bin", Connector.READ_WRITE);
        if (file.exists()) {
            file.delete();
        }
        file.create();
        OutputStream initial = file.openOutputStream();
        for (int value = 0; value < 6; value++) {
            initial.write(value);
        }
        try {
            file.openOutputStream();
            return -1;
        } catch (IOException expected) {
            // Only one output stream may be open for a FileConnection.
        }
        initial.close();

        OutputStream patch = file.openOutputStream(3);
        patch.write(9);
        patch.write(8);
        patch.close();
        OutputStream append = file.openOutputStream(100);
        append.write(7);
        append.close();
        try {
            file.openOutputStream(-1);
            return -2;
        } catch (IllegalArgumentException expected) {
            // Negative offsets are invalid.
        }

        InputStream first = file.openInputStream();
        try {
            file.openInputStream();
            return -3;
        } catch (IOException expected) {
            // Only one input stream may be open for a FileConnection.
        }
        first.close();

        InputStream renamedInput = file.openInputStream();
        file.rename("offset-renamed.bin");
        try {
            renamedInput.read();
            return -4;
        } catch (IOException expected) {
            // rename() closes streams opened by the FileConnection.
        }

        InputStream deletedInput = file.openInputStream();
        int valid = deletedInput.read() == 0 && deletedInput.read() == 1 &&
            deletedInput.read() == 2 && deletedInput.read() == 9 &&
            deletedInput.read() == 8 && deletedInput.read() == 5 &&
            deletedInput.read() == 7 && deletedInput.read() == -1 ? 1 : 0;
        file.delete();
        try {
            deletedInput.read();
            return -5;
        } catch (IOException expected) {
            // delete() also closes streams opened by the connection.
        }
        file.close();
        return valid;
    }

    public static int connectorPermissionDenied() {
        try {
            Connector.open("file:///denied.bin", Connector.READ);
            return 0;
        } catch (SecurityException expected) {
            return 1;
        } catch (IOException wrongException) {
            return -1;
        }
    }

    public static int streamPermissionRechecked() throws Exception {
        FileConnection file = (FileConnection) Connector.open(
            "file:///gate.bin", Connector.READ);
        try {
            file.openInputStream();
            return 0;
        } catch (SecurityException expected) {
            file.close();
            return 1;
        }
    }

    public static int modeAndClosedExceptions() throws Exception {
        FileConnection writable = (FileConnection) Connector.open(
            "file:///mode.bin", Connector.READ_WRITE);
        if (!writable.exists()) {
            writable.create();
        }
        writable.close();
        try {
            writable.exists();
            return -1;
        } catch (ConnectionClosedException expected) {
            // Closed FileConnections use the JSR-75 exception type.
        }

        FileConnection writeOnly = (FileConnection) Connector.open(
            "file:///mode.bin", Connector.WRITE);
        try {
            writeOnly.exists();
            return -2;
        } catch (IllegalModeException expected) {
            // Metadata reads require a READ-capable connection.
        }
        writeOnly.close();

        FileConnection readOnly = (FileConnection) Connector.open(
            "file:///mode.bin", Connector.READ);
        try {
            readOnly.delete();
            return -3;
        } catch (IllegalModeException expected) {
            // Mutating a READ connection uses IllegalModeException.
        }
        readOnly.close();

        FileConnection cleanup = (FileConnection) Connector.open(
            "file:///mode.bin", Connector.READ_WRITE);
        cleanup.delete();
        cleanup.close();
        return 1;
    }

    public static int surfaceSemantics() throws Exception {
        FileConnection root = (FileConnection) Connector.open(
            "file:///", Connector.READ);
        if (!root.isOpen() || !root.exists() || !root.isDirectory() ||
            !"/".equals(root.getPath()) || !"file:///".equals(root.getURL())) {
            return -1;
        }
        root.close();
        if (root.isOpen()) {
            return -2;
        }

        FileConnection directory = (FileConnection) Connector.open(
            "file:///surface", Connector.READ_WRITE);
        if (!directory.exists()) {
            directory.mkdir();
        }
        if (!"surface/".equals(directory.getName()) ||
            !"file:///surface/".equals(directory.getURL())) {
            return -3;
        }

        FileConnection file = (FileConnection) Connector.open(
            "file:///surface/entry.bin", Connector.READ_WRITE);
        if (!file.exists()) {
            file.create();
        }
        OutputStream output = file.openOutputStream();
        for (int value = 0; value < 6; value++) {
            output.write(value);
        }
        file.truncate(3);
        output.write(9);
        output.close();
        if (file.fileSize() != 7) {
            return -4;
        }
        file.truncate(100);
        if (file.fileSize() != 7) {
            return -5;
        }

        file.setHidden(true);
        if (!file.isHidden() || !".entry.bin".equals(file.getName())) {
            return -6;
        }
        file.setHidden(false);
        if (file.isHidden() || !"entry.bin".equals(file.getName())) {
            return -7;
        }

        FileConnection collision = (FileConnection) Connector.open(
            "file:///surface/collision.bin", Connector.READ_WRITE);
        if (!collision.exists()) {
            collision.create();
        }
        try {
            file.rename("collision.bin");
            return -8;
        } catch (IOException expected) {
            // JSR-75 rename must not replace an existing target.
        }
        if (!file.exists() || !collision.exists()) {
            return -9;
        }
        if (file.availableSize() < 0 || file.totalSize() <= 0 ||
            file.usedSize() < 0) {
            return -10;
        }

        file.delete();
        collision.delete();
        file.close();
        collision.close();
        directory.delete();
        directory.close();
        return 1;
    }

    public static int rootEnumeration() {
        Enumeration roots = FileSystemRegistry.listRoots();
        return roots != null && roots.hasMoreElements() &&
            "root/".equals((String) roots.nextElement()) &&
            !roots.hasMoreElements() ? 1 : 0;
    }

    public static int rootListenerRegistry() {
        FileSystemListener listener = new FileSystemListener() {
            public void rootChanged(int state, String rootName) {
            }
        };
        if (FileSystemListener.ROOT_ADDED != 0 ||
                FileSystemListener.ROOT_REMOVED != 1) return 0;
        if (!FileSystemRegistry.addFileSystemListener(listener)) return 0;
        if (!FileSystemRegistry.addFileSystemListener(listener)) return 0;
        if (!FileSystemRegistry.removeFileSystemListener(listener)) return 0;
        if (FileSystemRegistry.removeFileSystemListener(listener)) return 0;
        try {
            FileSystemRegistry.addFileSystemListener(null);
            return 0;
        } catch (NullPointerException expected) {
        }
        return 1;
    }

    public static int hiddenListingPolicy() throws Exception {
        FileConnection directory = (FileConnection) Connector.open(
            "file:///listing", Connector.READ_WRITE);
        if (!directory.exists()) {
            directory.mkdir();
        }
        FileConnection hidden = (FileConnection) Connector.open(
            "file:///listing/.hidden", Connector.READ_WRITE);
        if (!hidden.exists()) {
            hidden.create();
        }
        Enumeration publicEntries = directory.list("*", false);
        if (publicEntries.hasMoreElements()) {
            return 0;
        }
        Enumeration allEntries = directory.list("*", true);
        int result = allEntries.hasMoreElements() &&
            ".hidden".equals((String) allEntries.nextElement()) &&
            !allEntries.hasMoreElements() ? 1 : 0;
        hidden.delete();
        hidden.close();
        directory.delete();
        directory.close();
        return result;
    }
}
