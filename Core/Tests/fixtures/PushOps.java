package corefixture;

import javax.microedition.io.PushRegistry;
import javax.microedition.midlet.MIDlet;

public final class PushOps extends MIDlet {
    private static final String CONNECTION = "socket://:41001";
    private static final String MIDLET = "corefixture.PushOps";

    protected void startApp() {
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public static int connectionRoundTrip() throws Exception {
        PushRegistry.registerConnection(CONNECTION, MIDLET, "*");
        int result = 0;
        String[] registered = PushRegistry.listConnections(false);
        if (registered != null && registered.length == 1
                && CONNECTION.equals(registered[0])) {
            result |= 1;
        }
        if (MIDLET.equals(PushRegistry.getMIDlet(CONNECTION))) {
            result |= 2;
        }
        if ("*".equals(PushRegistry.getFilter(CONNECTION))) {
            result |= 4;
        }
        if (PushRegistry.listConnections(true).length == 0) {
            result |= 8;
        }
        if (PushRegistry.unregisterConnection(CONNECTION)
                && !PushRegistry.unregisterConnection(CONNECTION)) {
            result |= 16;
        }
        return result;
    }

    public static long alarmReplacement() throws Exception {
        long firstPrevious = PushRegistry.registerAlarm(MIDLET, 1000L);
        long secondPrevious = PushRegistry.registerAlarm(MIDLET, 2000L);
        PushRegistry.registerAlarm(MIDLET, 0L);
        return firstPrevious + secondPrevious;
    }
}
