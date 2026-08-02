package javax.microedition.rms;

public interface RecordEnumeration {
    int numRecords() throws RecordStoreNotOpenException;
    boolean hasNextElement();
    int nextRecordId() throws InvalidRecordIDException;
    byte[] nextRecord() throws InvalidRecordIDException, RecordStoreNotOpenException;
    boolean hasPreviousElement();
    int previousRecordId() throws InvalidRecordIDException;
    byte[] previousRecord() throws InvalidRecordIDException, RecordStoreNotOpenException;
    void reset();
    void rebuild();
    void keepUpdated(boolean keepUpdated);
    boolean isKeptUpdated();
    void destroy();
}
