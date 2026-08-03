package corefixture;

import java.util.Date;
import java.util.Timer;
import java.util.TimerTask;

public final class TimerOps {
    private static final Object LOCK = new Object();
    private static int count;
    private static int failures;
    private static Thread caller;

    private static final class OneShot extends TimerTask {
        public void run() {
            synchronized (LOCK) {
                if (scheduledExecutionTime() <= 0L) failures |= 1;
                if (Thread.currentThread() == null ||
                        Thread.currentThread() == caller) failures |= 2;
                count++;
                LOCK.notifyAll();
            }
        }
    }

    private static final class Repeating extends TimerTask {
        public void run() {
            synchronized (LOCK) {
                count++;
                if (count >= 3) {
                    if (!cancel()) failures |= 4;
                    LOCK.notifyAll();
                }
            }
        }
    }

    private static boolean waitForCount(int expected, long timeout) {
        long deadline = System.currentTimeMillis() + timeout;
        synchronized (LOCK) {
            while (count < expected && System.currentTimeMillis() < deadline) {
                try {
                    LOCK.wait(50L);
                } catch (InterruptedException unexpected) {
                    return false;
                }
            }
            return count >= expected;
        }
    }

    public static int run() {
        caller = Thread.currentThread();
        count = 0;
        failures = 0;

        Timer timer = new Timer(true);
        OneShot oneShot = new OneShot();
        timer.schedule(oneShot, 20L);
        if (!waitForCount(1, 1500L)) return 10;
        if (failures != 0) return 11 + failures;
        if (oneShot.cancel()) return 20;

        TimerTask cancelled = new OneShot();
        if (cancelled.cancel()) return 21;
        try {
            timer.schedule(cancelled, 1L);
            return 22;
        } catch (IllegalStateException expected) {
        }
        try {
            timer.schedule(new OneShot(), -1L);
            return 23;
        } catch (IllegalArgumentException expected) {
        }
        try {
            timer.schedule(new OneShot(), 0L, 0L);
            return 24;
        } catch (IllegalArgumentException expected) {
        }

        count = 0;
        Repeating repeating = new Repeating();
        timer.scheduleAtFixedRate(repeating, 0L, 20L);
        if (!waitForCount(3, 1500L)) return 25;
        int stoppedAt = count;
        try {
            Thread.sleep(100L);
        } catch (InterruptedException unexpected) {
            return 26;
        }
        if (count != stoppedAt || failures != 0) return 27;

        count = 0;
        timer.schedule(new OneShot(), new Date(System.currentTimeMillis() - 10L));
        if (!waitForCount(1, 1500L)) return 28;

        TimerTask duplicate = new OneShot();
        timer.schedule(duplicate, 500L);
        try {
            timer.schedule(duplicate, 500L);
            return 29;
        } catch (IllegalStateException expected) {
        }
        duplicate.cancel();

        timer.cancel();
        try {
            timer.schedule(new OneShot(), 1L);
            return 30;
        } catch (IllegalStateException expected) {
        }
        return failures;
    }
}
