package javax.microedition.io;

import java.io.IOException;

public final class PushRegistry {
    private PushRegistry() {
    }

    public static native void registerConnection(String connection,
                                                 String midlet,
                                                 String filter)
            throws ClassNotFoundException, IOException;

    public static native boolean unregisterConnection(String connection);

    public static native String[] listConnections(boolean available);

    public static native String getMIDlet(String connection);

    public static native String getFilter(String connection);

    public static native long registerAlarm(String midlet, long time)
            throws ClassNotFoundException, ConnectionNotFoundException;
}
