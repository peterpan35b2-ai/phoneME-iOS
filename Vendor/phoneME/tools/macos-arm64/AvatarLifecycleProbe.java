/**
 * Runs the Avatar LAPRO MIDlet while sampling its public UI state.
 */
public final class AvatarLifecycleProbe extends vn.lapro.GameMidlet
        implements Runnable {
    private Thread monitor;

    public AvatarLifecycleProbe() {
        super();
        System.out.println("AVATAR_CONSTRUCTOR_RETURNED");
        printState("AFTER_CONSTRUCTOR");
    }

    protected void startApp() {
        printState("BEFORE_STARTAPP");
        super.startApp();
        printState("AFTER_STARTAPP");
        monitor = new Thread(this);
        monitor.start();
    }

    public void run() {
        for (int i = 0; i < 80; i++) {
            try {
                Thread.sleep(100);
                printState("TICK_" + i);
            } catch (Throwable t) {
                System.out.println("AVATAR_MONITOR_FAILED: " + t);
                t.printStackTrace();
                return;
            }
        }
    }

    private static String className(Object value) {
        return value == null ? "null" : value.getClass().getName();
    }

    private static void printState(String stage) {
        try {
            System.out.println("AVATAR_STATE " + stage
                    + " canvas=" + className(sf.Q)
                    + " screen=" + className(sf.sv)
                    + " overlay=" + className(sf.f)
                    + " loginSingleton=" + className(GC.Y)
                    + " D=" + sf.D
                    + " P=" + sf.P
                    + " pointer=" + sf.I
                    + " waitMode=" + sf.u
                    + " size=" + sf.s3 + "x" + sf.V);
        } catch (Throwable t) {
            System.out.println("AVATAR_STATE_FAILED " + stage + ": " + t);
            t.printStackTrace();
        }
    }
}
