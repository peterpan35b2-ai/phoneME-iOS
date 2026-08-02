package javax.microedition.midlet;

public abstract class MIDlet {
    protected MIDlet() {}

    protected abstract void startApp();
    protected abstract void pauseApp();
    protected abstract void destroyApp(boolean unconditional);

    public final void notifyDestroyed() {}
    public final void notifyPaused() {}
    public final void resumeRequest() {}
    public final String getAppProperty(String key) { return null; }
    public final int checkPermission(String permission) { return -1; }
    public final boolean platformRequest(String url) { return false; }
}
