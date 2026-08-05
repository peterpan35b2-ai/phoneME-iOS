package corefixture;

public final class ThreadOps {
    private static final Object LOCK = new Object();
    private static final Object WAIT_LOCK = new Object();
    private static int counter;
    private static int signal;
    private static int completed;
    private static int interrupted;
    private static int rooted;
    private static int classInitReaderStarted;
    private static int classInitRead;
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

    private static final class OverrideThread extends Thread {
        OverrideThread(Runnable target) {
            super(target);
        }

        public void run() {
            rooted = 91;
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

    private interface ConstructorCallback {
        void run();
    }

    private static final class BlockingNestedArgument
            implements ConstructorCallback {
        BlockingNestedArgument(long token) {
            synchronized (WAIT_LOCK) {
                signal++;
                try {
                    WAIT_LOCK.wait(1000L);
                } catch (InterruptedException unexpected) {
                    rooted = -1;
                }
            }
        }

        public void run() {
        }
    }

    private static final class BlockingClassInitArgument
            implements ConstructorCallback {
        static {
            synchronized (WAIT_LOCK) {
                signal++;
                try {
                    WAIT_LOCK.wait(1000L);
                } catch (InterruptedException unexpected) {
                    rooted = -1;
                }
            }
        }

        BlockingClassInitArgument(long token) {
        }

        public void run() {
        }
    }

    private static final class BlockingSharedInitializer {
        static int value;

        static {
            synchronized (WAIT_LOCK) {
                signal++;
                try {
                    WAIT_LOCK.wait(1000L);
                } catch (InterruptedException unexpected) {
                    rooted = -2;
                }
            }
            value = 73;
        }
    }

    private static final class ClassInitOwnerTask implements Runnable {
        public void run() {
            completed = BlockingSharedInitializer.value;
        }
    }

    private static final class ClassInitReaderTask implements Runnable {
        public void run() {
            classInitReaderStarted = 1;
            classInitRead = BlockingSharedInitializer.value;
        }
    }

    private static final class PendingConstructor {
        private short state;
        private String label;
        private ConstructorCallback callback;

        PendingConstructor(String label, ConstructorCallback callback,
                           long token) {
            state = -1;
            this.label = label;
            this.callback = callback;
            if (token == 0L) {
                state = 0;
            }
        }
    }

    private static final class ConstructorRootTask implements Runnable {
        public void run() {
            PendingConstructor value = new PendingConstructor(
                "avatar", new BlockingNestedArgument(7L), 11L);
            rooted = value.state == -1
                    && value.label.equals("avatar")
                    && value.callback != null ? 1 : 2;
        }
    }

    private static final class ConstructorClassInitRootTask
            implements Runnable {
        public void run() {
            PendingConstructor value = new PendingConstructor(
                "avatar", new BlockingClassInitArgument(7L), 11L);
            rooted = value.state == -1
                    && value.label.equals("avatar")
                    && value.callback != null ? 1 : 2;
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

    public static int constructorRootRace() {
        signal = 0;
        rooted = 0;
        Thread constructorRootThread = new Thread(new ConstructorRootTask());
        constructorRootThread.start();
        if (!waitForSignal(1, 1000L)) {
            return 41;
        }
        System.gc();
        int gcSafepointWork = 0;
        for (int index = 0; index < 1024; ++index) {
            gcSafepointWork ^= index;
        }
        if (gcSafepointWork == -1) {
            return 44;
        }
        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notifyAll();
        }
        try {
            constructorRootThread.join();
        } catch (InterruptedException unexpected) {
            return 42;
        }
        return rooted == 1 ? 0 : 43;
    }

    public static int classInitializationWaitsForOwner() {
        signal = 0;
        completed = 0;
        rooted = 0;
        classInitReaderStarted = 0;
        classInitRead = 0;

        Thread owner = new Thread(new ClassInitOwnerTask());
        owner.start();
        if (!waitForSignal(1, 1000L)) {
            return 51;
        }

        Thread reader = new Thread(new ClassInitReaderTask());
        reader.start();
        long readerDeadline = System.currentTimeMillis() + 1000L;
        while (classInitReaderStarted == 0
                && System.currentTimeMillis() < readerDeadline) {
            Thread.yield();
        }
        if (classInitReaderStarted == 0) {
            return 52;
        }
        for (int index = 0; index < 256; ++index) {
            Thread.yield();
        }
        if (!reader.isAlive() || classInitRead != 0) {
            return 53;
        }

        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notifyAll();
        }
        try {
            owner.join(1000L);
            reader.join(1000L);
        } catch (InterruptedException unexpected) {
            return 54;
        }
        if (owner.isAlive() || reader.isAlive()) {
            return 55;
        }
        return completed == 73 && classInitRead == 73 && rooted == 0
                ? 0 : 56;
    }

    public static int constructorClassInitRootRace() {
        signal = 0;
        rooted = 0;
        Thread constructorRootThread = new Thread(
            new ConstructorClassInitRootTask());
        constructorRootThread.start();
        if (!waitForSignal(1, 1000L)) {
            return 45;
        }
        System.gc();
        int gcSafepointWork = 0;
        for (int index = 0; index < 1024; ++index) {
            gcSafepointWork ^= index;
        }
        if (gcSafepointWork == -1) {
            return 48;
        }
        synchronized (WAIT_LOCK) {
            WAIT_LOCK.notifyAll();
        }
        try {
            constructorRootThread.join();
        } catch (InterruptedException unexpected) {
            return 46;
        }
        return rooted == 1 ? 0 : 47;
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

        int constructorRootResult = constructorRootRace();
        if (constructorRootResult != 0) {
            return constructorRootResult;
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

        rooted = 0;
        int beforeOverride;
        synchronized (LOCK) {
            beforeOverride = counter;
        }
        Thread override = new OverrideThread(new CounterTask(10));
        override.start();
        try {
            override.join();
        } catch (InterruptedException unexpected) {
            return 26;
        }
        if (rooted != 91) return 27;
        synchronized (LOCK) {
            if (counter != beforeOverride) return 28;
        }

        return 0;
    }
}
