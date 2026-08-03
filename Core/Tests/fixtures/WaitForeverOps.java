package corefixture;

public final class WaitForeverOps {
    private static final Object LOCK = new Object();
    private static volatile boolean waiting;

    private WaitForeverOps() { }

    public static int startWaiter() {
        Thread worker = new Thread(new Runnable() {
            public void run() {
                synchronized (LOCK) {
                    waiting = true;
                    try {
                        LOCK.wait();
                    } catch (InterruptedException ignored) {
                    }
                }
            }
        });
        worker.start();
        while (!waiting) {
            Thread.yield();
        }
        return 1;
    }
}
