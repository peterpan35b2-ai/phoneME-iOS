package corefixture;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Enumeration;
import javax.microedition.io.Connector;
import javax.microedition.io.file.ConnectionClosedException;
import javax.microedition.io.file.FileConnection;
import javax.microedition.io.file.FileSystemRegistry;
import javax.microedition.io.file.IllegalModeException;

public final class FileOps {
    private FileOps() {
    }

    public static int resourceLookup() throws Exception {
        InputStream relative = FileOps.class.getResourceAsStream("data.bin");
        InputStream absolute =
            FileOps.class.getResourceAsStream("/corefixture/data.bin");
        if (relative == null || absolute == null) {
            return 0;
        }
        int relativeCount = 0;
        int absoluteCount = 0;
        int first = relative.read();
        int value;
        while ((value = relative.read()) >= 0) {
            relativeCount++;
        }
        while (absolute.read() >= 0) {
            absoluteCount++;
        }
        relative.close();
        absolute.close();
        return first == 'P' && relativeCount == 16 && absoluteCount == 17
            ? 1 : 0;
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
