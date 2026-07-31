import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.RecordStore;
import javax.microedition.rms.RecordStoreNotFoundException;

/** Clears only the cached NinjaSchool server list in the current suite. */
public final class NinjaSchoolCacheReset extends MIDlet {
    protected void startApp() {
        try {
            String[] stores = RecordStore.listRecordStores();
            if (stores != null) {
                for (int i = 0; i < stores.length; i++) {
                    System.out.println("RMS_STORE: " + stores[i]);
                }
            }

            try {
                RecordStore.deleteRecordStore("vjNJlink");
                System.out.println("NINJASCHOOL_SERVER_CACHE_CLEARED");
            } catch (RecordStoreNotFoundException e) {
                System.out.println("NINJASCHOOL_SERVER_CACHE_NOT_FOUND");
            }
        } catch (Throwable t) {
            System.out.println("NINJASCHOOL_SERVER_CACHE_RESET_FAILED: " + t);
            t.printStackTrace();
        } finally {
            notifyDestroyed();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
