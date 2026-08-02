package corefixture;

import javax.microedition.midlet.MIDlet;

public final class SelfDestroyApp extends MIDlet {
    public SelfDestroyApp() {
    }

    protected void startApp() {
        notifyDestroyed();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
