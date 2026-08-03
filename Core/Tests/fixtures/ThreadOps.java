package corefixture;

public final class ThreadOps {
    private static final Object LOCK = new Object();
    private static final Object WAIT_LOCK = new Object();
    private static int counter;
    private static int signal;
    private static int completed;
    private static int interrupted;
    private static int rooted;
    private static int busyStarted;
    private static Thread busyThread;
    private static Thread additionalBusyThread;

    private static final class CounterTask implements Runnable {
        private final int iterations;

        CounterTask(int iterations) {
            this.iterations = iterations;
        }

        public void run() {
            for (int index = 0; index < iterations; index++) {
                synchronized (LOCK) {
                    counter++;
                }
                if ((index & 7) == 0) {
                    Thread.yield();
                }
            }
        }
    }

    private static final class WaitTask implements Runnable {
        public void run() {
            synchronized (WAIT_LOCK) {
                signal++;
                try {
                    WAIT_LOCK.wait(1000L);
                    completed++;
                } catch (InterruptedException expected) {
                    interrupted++;
                }
            }
        }
    }

    private static final class SleepTask implements Runnable {
        public void run() {
            try {
                Thread.sleep(200L);
                completed++;
            } catch (InterruptedException expected) {
                interrupted++;
            }
        }
    }

    private static final class TimeoutWaitTask implements Runnable {
        public void run() {
            synchronized (WAIT_LOCK) {
                try {
                    WAIT_LOCK.wait(20L);
                    completed++;
                } catch (InterruptedException expected) {
                    interrupted++;
                }
            }
        }
    }

    private static final class JoinTask implements Runnable {
        private final Thread target;

        JoinTask(Thread target) {
            this.target = target;
        }

        public void run() {
            signal++;
            try {
                target.join();
                completed++;
            } catch (InterruptedException expected) {
                interrupted++;
            }
        }
    }

    private static final class RootTask implements Runnable {
        public void run() {
            Object marker = new Object();
            synchronized (WAIT_LOCK) {
                signal++;
                try {
                    WAIT_LOCK.wait(1000L);
                } catch (InterruptedException ignored) {
                    return;
                }
            }
            rooted = marker.hashCode() == 0 ? 2 : 1;
        }
    }

    private static final class ThrowTask implements Runnable {
        public void run() {
            synchronized (LOCK) {
                throw new RuntimeException();
            }
        }
    }

    private static final class BusyTask implements Runnable {
        public void run() {
            busyStarted = 1;
            while (true) {
                counter++;
            }
        }
    }

    private static synchronized void reentrantLevelTwo() {
        counter++;
    }

    private static synchronized void reentrantLevelOne() {
        counter++;
        reentrantLevelTwo();
    }

    private static boolean waitForSignal(int expected, long timeoutMillis) {
        long deadline = System.currentTimeMillis() + timeoutMillis;
        while (signal < expected && System.currentTimeMillis() < deadline) {
            Thread.yield();
        }
        return signal >= expected;
    }

    public static int startBusyThread() {
        busyStarted = 0;
        busyThread = new Thread(new BusyTask());
        busyThread.start();
        long deadline = System.currentTimeMillis() + 1000L;
        while (busyStarted == 0 && System.currentTimeMillis() < deadline) {
            Thread.yield();
        }
        return busyStarted;
    }

    public static int busyThreadIsAlive() {
        return busyThread != null && busyThread.isAlive() ? 1 : 0;
    }

    public static int startAdditionalBusyThread() {
        busyStarted = 0;
        additionalBusyThread = new Thread(new BusyTask());
        additionalBusyThread.start();
        long deadline = System.currentTimeMillis() + 1000L;
        while (busyStarted == 0 && System.currentTimeMillis() < deadline) {
            Thread.yield();
        }
        return busyStarted;
    }

    public static int busyMainThreadFor(int durationMillis) {
        long deadline = System.currentTimeMillis() + durationMillis;
        int spins = 0;
        while (System.currentTimeMillis() < deadline) {
            spins++;
        }
        return spins != 0 ? 1 : 0;
    }

    public static int timedWaitForCanvasPump() {
        synchronized (WAIT_LOCK) {
            try {
                WAIT_LOCK.wait(2L);
                return 1;
            } catch (InterruptedException unexpected) {
                return 0;
            }
        }
    }

    public static int run() {
        if (Thread.currentThread() == null) {
            return 1;
        }
        if (Thread.activeCount() < 1) {
            return 40;
        }

        counter = 0;
        Thread first = new Thread(new CounterTask(80));
        Thread second = new Thread(new CounterTask(80));
        first.start();
        second.start();
        try {
            first.join();
            second.join();
        } catch (InterruptedException unexpected) {
            return 2;
        }
        if (counter != 160 || first.isAlive() || second.isAlive()) {
            return 3;
        }

        reentrantLevelOne();
        if (counter != 162) {
            return 4;
        }

        signal = 0;
        completed = 0;
        interrupted = 0;
        Thread waiter = new Thread(new WaitTask());
        waiter.start();
        if (!waitForSignal(1, 1000L)) {
            return 5;
        }
        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notify();
        }
        try {
            waiter.join(1000L);
        } catch (InterruptedException unexpected) {
            return 6;
        }
        if (completed != 1 || waiter.isAlive()) {
            return 7;
        }

        signal = 0;
        completed = 0;
        Thread waiterOne = new Thread(new WaitTask());
        Thread waiterTwo = new Thread(new WaitTask());
        waiterOne.start();
        waiterTwo.start();
        if (!waitForSignal(2, 1000L)) {
            return 8;
        }
        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notifyAll();
        }
        try {
            waiterOne.join();
            waiterTwo.join();
        } catch (InterruptedException unexpected) {
            return 9;
        }
        if (completed != 2) {
            return 10;
        }

        signal = 0;
        completed = 0;
        interrupted = 0;
        Thread interruptedWaiter = new Thread(new WaitTask());
        interruptedWaiter.start();
        if (!waitForSignal(1, 1000L)) {
            return 24;
        }
        interruptedWaiter.interrupt();
        try {
            interruptedWaiter.join(1000L);
        } catch (InterruptedException unexpected) {
            return 25;
        }
        if (interrupted != 1 || interruptedWaiter.isAlive()) {
            return 26;
        }

        completed = 0;
        interrupted = 0;
        Thread timeoutWaiter = new Thread(new TimeoutWaitTask());
        timeoutWaiter.start();
        try {
            timeoutWaiter.join(1000L);
        } catch (InterruptedException unexpected) {
            return 27;
        }
        if (completed != 1 || interrupted != 0 || timeoutWaiter.isAlive()) {
            return 28;
        }

        signal = 0;
        completed = 0;
        interrupted = 0;
        Thread joinTarget = new Thread(new SleepTask());
        Thread joining = new Thread(new JoinTask(joinTarget));
        joinTarget.start();
        joining.start();
        if (!waitForSignal(1, 1000L)) {
            return 29;
        }
        joining.interrupt();
        try {
            joining.join(1000L);
        } catch (InterruptedException unexpected) {
            return 30;
        }
        if (interrupted != 1 || joining.isAlive()) {
            return 31;
        }
        joinTarget.interrupt();
        try {
            joinTarget.join();
        } catch (InterruptedException unexpected) {
            return 32;
        }

        completed = 0;
        interrupted = 0;
        Thread sleeper = new Thread(new SleepTask());
        Thread progress = new Thread(new Runnable() {
            public void run() {
                completed += 10;
            }
        });
        sleeper.start();
        progress.start();
        try {
            progress.join(1000L);
        } catch (InterruptedException unexpected) {
            return 11;
        }
        if (progress.isAlive() || completed != 10) {
            return 12;
        }
        sleeper.interrupt();
        try {
            sleeper.join(1000L);
        } catch (InterruptedException unexpected) {
            return 13;
        }
        if (interrupted != 1 || sleeper.isAlive()) {
            return 14;
        }

        completed = 0;
        Thread timeoutSleeper = new Thread(new SleepTask());
        timeoutSleeper.start();
        try {
            timeoutSleeper.join(10L);
        } catch (InterruptedException unexpected) {
            return 15;
        }
        if (!timeoutSleeper.isAlive()) {
            return 16;
        }
        timeoutSleeper.interrupt();
        try {
            timeoutSleeper.join();
        } catch (InterruptedException unexpected) {
            return 17;
        }

        signal = 0;
        rooted = 0;
        Thread rootThread = new Thread(new RootTask());
        rootThread.start();
        if (!waitForSignal(1, 1000L)) {
            return 18;
        }
        System.gc();
        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notifyAll();
        }
        try {
            rootThread.join();
        } catch (InterruptedException unexpected) {
            return 19;
        }
        if (rooted != 1) {
            return 20;
        }

        Thread throwing = new Thread(new ThrowTask());
        throwing.start();
        try {
            throwing.join();
        } catch (InterruptedException unexpected) {
            return 21;
        }
        synchronized (LOCK) {
            counter++;
        }

        Thread priority = new Thread();
        priority.setPriority(7);
        if (priority.getPriority() != 7) {
            return 22;
        }
        boolean rejected = false;
        try {
            priority.setPriority(11);
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }
        if (!rejected) {
            return 23;
        }

        int beforeNamed;
        synchronized (LOCK) {
            beforeNamed = counter;
        }
        Thread named = new Thread(new CounterTask(1), "named-worker");
        named.start();
        try {
            named.join();
        } catch (InterruptedException unexpected) {
            return 24;
        }
        synchronized (LOCK) {
            if (counter != beforeNamed + 1) return 25;
        }

        return 0;
    }
}
