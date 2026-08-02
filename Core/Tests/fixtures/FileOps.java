package corefixture;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Enumeration;
import javax.microedition.io.Connector;
import javax.microedition.io.file.FileConnection;

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
        try {
            output.write(8);
            return 0;
        } catch (IOException expected) {
            FileConnection file = (FileConnection) Connector.open(
                "file:///closed.bin", Connector.READ_WRITE);
            file.delete();
            file.close();
            return 1;
        }
    }
}
