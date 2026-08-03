package javax.bluetooth;

import java.io.IOException;

public class RemoteDevice {
    protected RemoteDevice(String address) {}
    public String getBluetoothAddress() { return null; }
    public String getFriendlyName(boolean alwaysAsk) throws IOException { return null; }
}
