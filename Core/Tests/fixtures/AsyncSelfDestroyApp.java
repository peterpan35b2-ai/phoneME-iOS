package corefixture;

import javax.microedition.midlet.MIDlet;

public final class AsyncSelfDestroyApp extends MIDlet implements Runnable {
    private Thread worker;

    protected void startApp() {
        if (worker == null) {
            worker = new Thread(this, "self-destroy");
            worker.start();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void run() {
        try {
            Thread.sleep(25L);
        } catch (InterruptedException ignored) {
        }
        notifyDestroyed();
    }
}
