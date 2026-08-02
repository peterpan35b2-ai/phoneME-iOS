package corefixture;

import javax.microedition.midlet.MIDlet;

public final class LifecycleApp extends MIDlet {
    public LifecycleApp() {
    }

    protected void startApp() {
        if (!"hello".equals(getAppProperty("Test-Property"))) {
            throw new IllegalStateException();
        }
        if (!"phoneME-iOS".equals(getAppProperty("Continuation-Property"))) {
            throw new IllegalStateException();
        }
        if (!"Việt".equals(getAppProperty("Vietnamese-Property"))) {
            throw new IllegalStateException();
        }
        if (getAppProperty("Missing-Property") != null) {
            throw new IllegalStateException();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
