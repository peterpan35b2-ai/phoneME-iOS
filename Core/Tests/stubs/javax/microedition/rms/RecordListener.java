package javax.microedition.rms;

public interface RecordListener {
    void recordAdded(RecordStore store, int recordId);
    void recordChanged(RecordStore store, int recordId);
    void recordDeleted(RecordStore store, int recordId);
}
