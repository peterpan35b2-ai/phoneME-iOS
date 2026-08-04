package corefixture;

import javax.microedition.midlet.MIDlet;

public final class FailingLifecycleApp extends MIDlet {
    public FailingLifecycleApp() {
    }

    protected void startApp() {
        throw new IllegalStateException("fixture start failure");
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
