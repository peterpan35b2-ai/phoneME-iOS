package corefixture;

import javax.microedition.midlet.MIDlet;

public final class SelfPauseApp extends MIDlet {
    private boolean started;

    public SelfPauseApp() {
    }

    protected void startApp() {
        if (!started) {
            started = true;
            notifyPaused();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
