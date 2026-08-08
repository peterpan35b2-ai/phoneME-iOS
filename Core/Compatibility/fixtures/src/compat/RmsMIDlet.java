package compat;

import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.RecordStore;

public final class RmsMIDlet extends MIDlet {
    protected void startApp() {
        try {
            try {
                RecordStore.deleteRecordStore("compat17");
            } catch (Exception ignored) {
            }
            RecordStore store = RecordStore.openRecordStore("compat17", true);
            byte[] payload = new byte[] {1, 3, 5, 7};
            int id = store.addRecord(payload, 0, payload.length);
            byte[] loaded = store.getRecord(id);
            if (loaded.length != payload.length || loaded[2] != payload[2]) {
                throw new RuntimeException("RMS round trip mismatch");
            }
            store.closeRecordStore();
            store = RecordStore.openRecordStore("compat17", false);
            byte[] reopened = store.getRecord(id);
            if (reopened.length != payload.length || reopened[2] != payload[2]) {
                throw new RuntimeException("RMS reopen mismatch");
            }
            store.closeRecordStore();
            RecordStore.deleteRecordStore("compat17");
            Form result = new Form("RMS OK");
            result.append("roundtrip + reopen");
            Display.getDisplay(this).setCurrent(result);
        } catch (Exception error) {
            throw new RuntimeException(error.toString());
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
        System.out.println("COMPAT_MILESTONE:rms-destroy");
    }
}
