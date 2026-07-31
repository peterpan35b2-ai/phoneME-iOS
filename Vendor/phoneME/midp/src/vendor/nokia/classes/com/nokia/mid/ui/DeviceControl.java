package com.nokia.mid.ui;

/** Nokia device feedback controls mapped to the iOS host. */
public final class DeviceControl {
    private DeviceControl() {
    }

    public static void setLights(int num, int level) {
        if (num < 0 || level < 0 || level > 100) {
            throw new IllegalArgumentException("Invalid light parameters");
        }
        nSetLights(level);
    }

    public static void flashLights(long duration) {
        if (duration < 0) {
            throw new IllegalArgumentException("Negative duration");
        }
        if (duration == 0) {
            return;
        }
        nFlashLights(duration);
    }

    public static void startVibra(int frequency, long duration) {
        if (frequency < 0 || frequency > 100 || duration < 0) {
            throw new IllegalArgumentException("Invalid vibration parameters");
        }
        if (frequency == 0 || duration == 0) {
            stopVibra();
            return;
        }
        nStartVibra(frequency, duration);
    }

    public static void stopVibra() {
        nStopVibra();
    }

    private static native void nSetLights(int level);
    private static native void nFlashLights(long duration);
    private static native void nStartVibra(int frequency, long duration);
    private static native void nStopVibra();
}
