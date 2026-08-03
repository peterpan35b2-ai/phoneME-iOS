package corefixture;

import java.util.Enumeration;
import javax.bluetooth.BluetoothStateException;
import javax.bluetooth.DataElement;
import javax.bluetooth.DeviceClass;
import javax.bluetooth.DiscoveryAgent;
import javax.bluetooth.DiscoveryListener;
import javax.bluetooth.LocalDevice;
import javax.bluetooth.RemoteDevice;
import javax.bluetooth.ServiceRecord;
import javax.bluetooth.UUID;

public final class BluetoothOps {
    private static final class ProbeListener implements DiscoveryListener {
        int inquiryCode = -1;
        int transaction = -1;
        int serviceCode = -1;

        public void deviceDiscovered(RemoteDevice device, DeviceClass deviceClass) {}
        public void servicesDiscovered(int transID, ServiceRecord[] records) {}

        public synchronized void inquiryCompleted(int discType) {
            inquiryCode = discType;
            notifyAll();
        }

        public synchronized void serviceSearchCompleted(int transID, int responseCode) {
            transaction = transID;
            serviceCode = responseCode;
            notifyAll();
        }

        synchronized boolean awaitInquiry(int expected, long timeout)
                throws InterruptedException {
            long deadline = System.currentTimeMillis() + timeout;
            while (inquiryCode != expected) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) return false;
                wait(remaining);
            }
            return true;
        }

        synchronized void resetService() {
            transaction = -1;
            serviceCode = -1;
        }

        synchronized boolean awaitService(int expectedTransaction,
                                          int expectedCode,
                                          long timeout)
                throws InterruptedException {
            long deadline = System.currentTimeMillis() + timeout;
            while (transaction != expectedTransaction || serviceCode != expectedCode) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) return false;
                wait(remaining);
            }
            return true;
        }
    }

    private static final class ProbeRemoteDevice extends RemoteDevice {
        ProbeRemoteDevice(String address) {
            super(address);
        }
    }

    public static int run() throws Exception {
        if (DiscoveryAgent.GIAC != 0x9E8B33 ||
            DiscoveryAgent.LIAC != 0x9E8B00 ||
            DiscoveryListener.SERVICE_SEARCH_NO_RECORDS != 4) return 1;
        if (DataElement.DATSEQ != 0x30 || DataElement.BOOL != 0x28) return 2;
        if (ServiceRecord.AUTHENTICATE_ENCRYPT != 2) return 3;

        UUID serial = new UUID(0x1101L);
        UUID serialText = new UUID("1101", true);
        if (!serial.equals(serialText)) return 4;
        if (serial.hashCode() != serialText.hashCode()) return 5;
        if (!"0000110100001000800000805f9b34fb".equals(serial.toString())) return 6;
        try {
            new UUID("xyz", false);
            return 7;
        } catch (IllegalArgumentException expected) {
        }

        LocalDevice local = LocalDevice.getLocalDevice();
        if (local == null || local != LocalDevice.getLocalDevice()) return 8;
        if (!LocalDevice.isPowerOn()) return 9;
        if (!"1.1".equals(LocalDevice.getProperty("bluetooth.api.version"))) return 10;
        if (!local.setDiscoverable(DiscoveryAgent.GIAC)) return 11;
        if (local.getDiscoverable() != DiscoveryAgent.GIAC) return 12;
        try {
            local.setDiscoverable(1234);
            return 13;
        } catch (IllegalArgumentException expected) {
        }

        DataElement signed = new DataElement(DataElement.INT_2, 32000L);
        if (signed.getDataType() != DataElement.INT_2 || signed.getLong() != 32000L) return 14;
        try {
            new DataElement(DataElement.INT_1, 128L);
            return 15;
        } catch (IllegalArgumentException expected) {
        }

        DataElement bool = new DataElement(true);
        if (bool.getDataType() != DataElement.BOOL || !bool.getBoolean()) return 16;
        try {
            bool.getLong();
            return 17;
        } catch (ClassCastException expected) {
        }

        DataElement sequence = new DataElement(DataElement.DATSEQ);
        DataElement first = new DataElement(DataElement.STRING, "first");
        DataElement second = new DataElement(DataElement.STRING, "second");
        sequence.addElement(second);
        sequence.insertElementAt(first, 0);
        if (sequence.getSize() != 2) return 18;
        Enumeration values = (Enumeration) sequence.getValue();
        if (!values.hasMoreElements() || values.nextElement() != first) return 19;
        if (!values.hasMoreElements() || values.nextElement() != second) return 20;
        if (values.hasMoreElements()) return 21;
        if (!sequence.removeElement(first) || sequence.getSize() != 1) return 22;
        if (sequence.removeElement(first)) return 23;

        DataElement bytes = new DataElement(DataElement.U_INT_8, new byte[8]);
        if (((byte[]) bytes.getValue()).length != 8) return 24;
        try {
            new DataElement(DataElement.U_INT_8, new byte[7]);
            return 25;
        } catch (IllegalArgumentException expected) {
        }

        DiscoveryAgent agent = local.getDiscoveryAgent();
        ProbeListener listener = new ProbeListener();
        if (!agent.startInquiry(DiscoveryAgent.GIAC, listener)) return 26;
        if (!listener.awaitInquiry(DiscoveryListener.INQUIRY_COMPLETED, 2000L)) return 27;
        try {
            agent.startInquiry(1, listener);
            return 28;
        } catch (IllegalArgumentException expected) {
        }

        UUID[] services = new UUID[] {serial};
        ProbeRemoteDevice remote = new ProbeRemoteDevice("aabbccddeeff");
        if (!"AABBCCDDEEFF".equals(remote.getBluetoothAddress())) return 33;
        try {
            new ProbeRemoteDevice("not-an-address");
            return 34;
        } catch (IllegalArgumentException expected) {
        }
        listener.resetService();
        int firstTransaction = agent.searchServices(null, services, remote, listener);
        if (firstTransaction <= 0 ||
            !listener.awaitService(firstTransaction,
                                   DiscoveryListener.SERVICE_SEARCH_NO_RECORDS,
                                   2000L)) return 29;
        listener.resetService();
        int secondTransaction = agent.searchServices(null, services, remote, listener);
        if (secondTransaction <= firstTransaction ||
            !listener.awaitService(secondTransaction,
                                   DiscoveryListener.SERVICE_SEARCH_NO_RECORDS,
                                   2000L)) return 30;
        try {
            agent.searchServices(null, new UUID[0], remote, listener);
            return 31;
        } catch (IllegalArgumentException expected) {
        }
        try {
            agent.retrieveDevices(99);
            return 32;
        } catch (IllegalArgumentException expected) {
        }

        return 0;
    }
}
