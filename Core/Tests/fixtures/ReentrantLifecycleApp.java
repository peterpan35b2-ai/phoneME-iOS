package corefixture;

import com.sun.midp.security.PermissionGate;
import javax.microedition.midlet.MIDlet;

public final class ReentrantLifecycleApp extends MIDlet {
    private static final String FILE_READ =
            "javax.microedition.io.Connector.file.read";

    private static void request(String phase) {
        int decision = PermissionGate.requestPermission(
                FILE_READ, "file:///lifecycle-" + phase, true);
        if (decision != 1) {
            throw new SecurityException("permission denied during " + phase);
        }
    }

    protected void startApp() {
        request("start");
    }

    protected void pauseApp() {
        request("pause");
    }

    protected void destroyApp(boolean unconditional) {
        request("destroy");
    }
}
