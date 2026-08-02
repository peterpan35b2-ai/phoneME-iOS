package corefixture;

public final class MonitorOps {
    private int value;

    public MonitorOps() {
    }

    public static int reentrantMonitor() {
        Object lock = new Object();
        synchronized (lock) {
            synchronized (lock) {
                return 57;
            }
        }
    }

    public synchronized int add(int amount) {
        value += amount;
        return value;
    }

    public synchronized void fail() {
        throw new IllegalStateException();
    }

    public static synchronized int staticMonitor() {
        return 58;
    }

    public static int synchronizedMethods() {
        MonitorOps monitor = new MonitorOps();
        return monitor.add(1) + monitor.add(2) + staticMonitor();
    }
}
