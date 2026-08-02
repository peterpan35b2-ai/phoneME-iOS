package corefixture;

import com.sun.midp.security.PermissionGate;
import javax.microedition.midlet.MIDlet;

public final class SecurityOps {
    private static final String HTTP =
            "javax.microedition.io.Connector.http";
    private static final String FILE_READ =
            "javax.microedition.io.Connector.file.read";
    private static final String MEDIA_RECORD =
            "javax.microedition.media.control.RecordControl";
    private static final String UNKNOWN = "vendor.example.permission";

    private SecurityOps() {
    }

    private static final class Probe extends MIDlet {
        protected void startApp() {
        }

        protected void pauseApp() {
        }

        protected void destroyApp(boolean unconditional) {
        }

        int status(String permission) {
            return checkPermission(permission);
        }
    }

    public static int checkStatuses() {
        Probe probe = new Probe();
        int result = 0;
        if (probe.status(HTTP) == 1) {
            result |= 1;
        }
        if (probe.status(MEDIA_RECORD) == 0) {
            result |= 2;
        }
        if (probe.status(UNKNOWN) == -1) {
            result |= 4;
        }
        if (PermissionGate.checkPermission(HTTP) == 1) {
            result |= 8;
        }
        return result;
    }

    public static int requestAndRequire() {
        int result = 0;
        if (PermissionGate.requestPermission(
                FILE_READ, "file:///save.dat", true) == 1) {
            result |= 1;
        }
        try {
            PermissionGate.requirePermission(
                    MEDIA_RECORD, "capture://audio", true);
        } catch (SecurityException expected) {
            result |= 2;
        }
        return result;
    }
}
