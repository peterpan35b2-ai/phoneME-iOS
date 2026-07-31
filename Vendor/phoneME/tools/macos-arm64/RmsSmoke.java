import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.RecordStore;

/** Verifies RecordStore create, write, list, reopen, read, and delete. */
public final class RmsSmoke extends MIDlet {
    private static final String NAME = "phoneME_RMS_smoke";

    protected void startApp() {
        RecordStore store = null;
        try {
            try {
                RecordStore.deleteRecordStore(NAME);
            } catch (Throwable ignored) {
            }

            store = RecordStore.openRecordStore(NAME, true);
            byte[] expected = { 7, 11, 23 };
            int id = store.addRecord(expected, 0, expected.length);
            System.out.println("RMS_ADD id=" + id
                    + " count=" + store.getNumRecords());
            store.closeRecordStore();
            store = null;

            String[] names = RecordStore.listRecordStores();
            System.out.println("RMS_LIST count="
                    + (names == null ? -1 : names.length));
            if (names != null) {
                for (int i = 0; i < names.length; i++) {
                    System.out.println("RMS_LIST name=" + names[i]);
                }
            }

            store = RecordStore.openRecordStore(NAME, false);
            byte[] actual = store.getRecord(id);
            System.out.println("RMS_READ len=" + actual.length
                    + " bytes=" + actual[0] + "," + actual[1]
                    + "," + actual[2]);
            store.closeRecordStore();
            store = null;

            RecordStore.deleteRecordStore(NAME);
            System.out.println("RMS_SMOKE_OK");
        } catch (Throwable t) {
            System.out.println("RMS_SMOKE_FAILED: " + t);
            t.printStackTrace();
        } finally {
            if (store != null) {
                try {
                    store.closeRecordStore();
                } catch (Throwable ignored) {
                }
            }
            notifyDestroyed();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
