package javax.microedition.rms;

public final class RecordStore {
    private RecordStore() {
    }

    public static RecordStore openRecordStore(String name, boolean create)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public static RecordStore openRecordStore(String name, boolean create,
                                               int authMode, boolean writable)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public static void deleteRecordStore(String name)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public static String[] listRecordStores() {
        throw new UnsupportedOperationException();
    }

    public void closeRecordStore() throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public String getName() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int getVersion() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int getNumRecords() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int getSize() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int getSizeAvailable() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public long getLastModified() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int getNextRecordID() throws RecordStoreNotOpenException {
        throw new UnsupportedOperationException();
    }

    public int addRecord(byte[] data, int offset, int length)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public void setRecord(int id, byte[] data, int offset, int length)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public void deleteRecord(int id) throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public byte[] getRecord(int id) throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public int getRecord(int id, byte[] buffer, int offset)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public int getRecordSize(int id) throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public RecordEnumeration enumerateRecords(RecordFilter filter,
                                               RecordComparator comparator,
                                               boolean keepUpdated)
            throws RecordStoreException {
        throw new UnsupportedOperationException();
    }

    public void addRecordListener(RecordListener listener) {
        throw new UnsupportedOperationException();
    }

    public void removeRecordListener(RecordListener listener) {
        throw new UnsupportedOperationException();
    }
}
