import main.GameMidlet;

/** Runs the real NinjaSchool MIDlet while sampling its server-array state. */
public final class NinjaSchoolLifecycleProbe extends GameMidlet
        implements Runnable {
    private Thread monitor;

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
                System.out.println("LIFECYCLE_MONITOR_FAILED: " + t);
                t.printStackTrace();
                return;
            }
        }
    }

    private static void printState(String stage) {
        try {
            int selected = w.c("indServer");
            int length = GameMidlet.l == null ? -1 : GameMidlet.l.length;
            String value = "none";
            if (GameMidlet.l != null && selected >= 0
                    && selected < GameMidlet.l.length) {
                value = GameMidlet.l[selected];
            }
            System.out.println("SERVER_STATE " + stage
                    + " selected=" + selected
                    + " l=" + length
                    + " m=" + (GameMidlet.m == null ? -1 : GameMidlet.m.length)
                    + " value=" + value);
        } catch (Throwable t) {
            System.out.println("SERVER_STATE_FAILED " + stage + ": " + t);
            t.printStackTrace();
        }
    }
}
