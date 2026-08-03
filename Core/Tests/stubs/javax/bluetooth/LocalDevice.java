package javax.bluetooth;

public final class LocalDevice {
    private LocalDevice() {}
    public static LocalDevice getLocalDevice() throws BluetoothStateException { return null; }
    public static String getProperty(String property) { return null; }
    public static boolean isPowerOn() { return false; }
    public DiscoveryAgent getDiscoveryAgent() { return null; }
    public String getFriendlyName() { return null; }
    public String getBluetoothAddress() { return null; }
    public DeviceClass getDeviceClass() { return null; }
    public boolean setDiscoverable(int mode) throws BluetoothStateException { return false; }
    public int getDiscoverable() { return 0; }
}
