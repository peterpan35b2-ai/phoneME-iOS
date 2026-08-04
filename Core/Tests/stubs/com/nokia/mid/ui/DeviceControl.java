package com.nokia.mid.ui;

public final class DeviceControl {
    private DeviceControl() {}
    public static native void setLights(int num, int level);
    public static native void flashLights(long duration);
    public static native void startVibra(int frequency, long duration);
    public static native void stopVibra();
}
