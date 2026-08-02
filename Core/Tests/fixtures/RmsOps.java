package corefixture;

import javax.microedition.rms.InvalidRecordIDException;
import javax.microedition.rms.RecordComparator;
import javax.microedition.rms.RecordEnumeration;
import javax.microedition.rms.RecordFilter;
import javax.microedition.rms.RecordListener;
import javax.microedition.rms.RecordStore;
import javax.microedition.rms.RecordStoreException;
import javax.microedition.rms.RecordStoreNotFoundException;
import javax.microedition.rms.RecordStoreNotOpenException;

public final class RmsOps {
    private static final String STORE = "core-rms";
    private static int listenerAdded;
    private static int listenerChanged;
    private static int listenerDeleted;
    private static RecordStore listenerStore;
    private static final Object THREAD_LOCK = new Object();
    private static int threadErrors;
    private static int threadAdds;

    private static final class CountingListener implements RecordListener {
        public void recordAdded(RecordStore store, int recordId) {
            if (store == listenerStore && recordId > 0) listenerAdded++;
        }

        public void recordChanged(RecordStore store, int recordId) {
            if (store == listenerStore && recordId > 0) listenerChanged++;
        }

        public void recordDeleted(RecordStore store, int recordId) {
            if (store == listenerStore && recordId > 0) listenerDeleted++;
        }
    }

    private static final class AcceptAllFilter implements RecordFilter {
        public boolean matches(byte[] candidate) {
            return true;
        }
    }

    private static final class AtLeastTwoFilter implements RecordFilter {
        public boolean matches(byte[] candidate) {
            return candidate.length > 0 && candidate[0] >= 2;
        }
    }

    private static final class DescendingComparator implements RecordComparator {
        public int compare(byte[] left, byte[] right) {
            if (left[0] == right[0]) return EQUIVALENT;
            return left[0] > right[0] ? PRECEDES : FOLLOWS;
        }
    }

    private static final class AscendingComparator implements RecordComparator {
        public int compare(byte[] left, byte[] right) {
            if (left[0] == right[0]) return EQUIVALENT;
            return left[0] < right[0] ? PRECEDES : FOLLOWS;
        }
    }

    private static final class ThrowingFilter implements RecordFilter {
        public boolean matches(byte[] candidate) {
            throw new IllegalStateException();
        }
    }

    private static final class ThrowingComparator implements RecordComparator {
        public int compare(byte[] left, byte[] right) {
            throw new IllegalStateException();
        }
    }

    private static final class HandleCountingListener implements RecordListener {
        private final RecordStore expected;
        int added;
        int changed;
        int deleted;

        HandleCountingListener(RecordStore expected) {
            this.expected = expected;
        }

        public void recordAdded(RecordStore store, int recordId) {
            if (store == expected && recordId > 0) added++;
        }

        public void recordChanged(RecordStore store, int recordId) {
            if (store == expected && recordId > 0) changed++;
        }

        public void recordDeleted(RecordStore store, int recordId) {
            if (store == expected && recordId > 0) deleted++;
        }
    }

    private static final class MutatingListener implements RecordListener {
        private final RecordStore store;
        private final RecordListener replacement;
        int added;

        MutatingListener(RecordStore store, RecordListener replacement) {
            this.store = store;
            this.replacement = replacement;
        }

        public void recordAdded(RecordStore callbackStore, int recordId) {
            added++;
            if (added == 1) {
                store.removeRecordListener(this);
                store.addRecordListener(replacement);
            }
        }

        public void recordChanged(RecordStore callbackStore, int recordId) {
        }

        public void recordDeleted(RecordStore callbackStore, int recordId) {
        }
    }

    private static final class ThreadListener implements RecordListener {
        private final RecordStore expected;

        ThreadListener(RecordStore expected) {
            this.expected = expected;
        }

        public void recordAdded(RecordStore store, int recordId) {
            if (store == expected && recordId > 0) {
                synchronized (THREAD_LOCK) {
                    threadAdds++;
                }
            }
        }

        public void recordChanged(RecordStore store, int recordId) {
        }

        public void recordDeleted(RecordStore store, int recordId) {
        }
    }

    private static final class ConcurrentWriter implements Runnable {
        private final String storeName;
        private final int marker;

        ConcurrentWriter(String storeName, int marker) {
            this.storeName = storeName;
            this.marker = marker;
        }

        public void run() {
            RecordStore store = null;
            try {
                store = RecordStore.openRecordStore(storeName, false);
                for (int index = 0; index < 20; index++) {
                    store.addRecord(new byte[] {
                        (byte) marker, (byte) index
                    }, 0, 2);
                    if ((index & 3) == 0) Thread.yield();
                }
                store.closeRecordStore();
            } catch (Exception failure) {
                synchronized (THREAD_LOCK) {
                    threadErrors++;
                }
                if (store != null) {
                    try {
                        store.closeRecordStore();
                    } catch (Exception ignored) {
                    }
                }
            }
        }
    }

    private RmsOps() {
    }

    public static int createAndMutate() throws Exception {
        try {
            RecordStore.deleteRecordStore(STORE);
        } catch (RecordStoreNotFoundException expected) {
        }

        RecordStore store = RecordStore.openRecordStore(STORE, true);
        int result = 0;
        int first = store.addRecord(new byte[] {1, 2, 3}, 0, 3);
        int second = store.addRecord(new byte[] {4, 5}, 0, 2);
        if (first == 1 && second == 2 && store.getNextRecordID() == 3) {
            result |= 1;
        }
        if (store.getNumRecords() == 2 && store.getSize() == 5
                && store.getSizeAvailable() > 0) {
            result |= 2;
        }

        byte[] replacement = {9, 8, 7, 6};
        store.setRecord(first, replacement, 1, 2);
        byte[] direct = store.getRecord(first);
        if (direct.length == 2 && direct[0] == 8 && direct[1] == 7
                && store.getRecordSize(first) == 2) {
            result |= 4;
        }
        byte[] destination = {0, 0, 0, 0};
        int copied = store.getRecord(first, destination, 1);
        if (copied == 2 && destination[1] == 8 && destination[2] == 7) {
            result |= 8;
        }

        RecordEnumeration records = store.enumerateRecords(null, null, false);
        if (records.numRecords() == 2 && records.hasNextElement()
                && records.nextRecordId() == first) {
            result |= 16;
        }
        byte[] secondRecord = records.nextRecord();
        if (secondRecord.length == 2 && secondRecord[0] == 4
                && records.hasPreviousElement()) {
            result |= 32;
        }
        byte[] previous = records.previousRecord();
        if (previous.length == 2 && previous[0] == 4) {
            result |= 64;
        }
        records.reset();
        records.keepUpdated(true);
        if (records.isKeptUpdated() && records.nextRecordId() == first) {
            result |= 128;
        }
        records.destroy();
        store.closeRecordStore();
        return result;
    }

    public static int reopenAndDelete() throws Exception {
        RecordStore store = RecordStore.openRecordStore(STORE, false);
        int result = 0;
        if (store.getName().equals(STORE) && store.getVersion() == 3
                && store.getNumRecords() == 2 && store.getNextRecordID() == 3
                && store.getLastModified() > 0) {
            result |= 1;
        }
        byte[] first = store.getRecord(1);
        byte[] second = store.getRecord(2);
        if (first.length == 2 && first[0] == 8 && first[1] == 7
                && second.length == 2 && second[0] == 4 && second[1] == 5) {
            result |= 2;
        }
        store.deleteRecord(2);
        if (store.getNumRecords() == 1 && store.getVersion() == 4) {
            result |= 4;
        }
        store.closeRecordStore();
        String[] names = RecordStore.listRecordStores();
        if (names != null && names.length == 1 && names[0].equals(STORE)) {
            result |= 8;
        }
        RecordStore.deleteRecordStore(STORE);
        if (RecordStore.listRecordStores() == null) {
            result |= 16;
        }
        return result;
    }

    public static int exceptions() throws Exception {
        int result = 0;
        try {
            RecordStore.openRecordStore("missing", false);
        } catch (RecordStoreNotFoundException expected) {
            result |= 1;
        }

        RecordStore store = RecordStore.openRecordStore("errors", true);
        try {
            store.getRecord(99);
        } catch (InvalidRecordIDException expected) {
            result |= 2;
        }
        RecordEnumeration filtered = store.enumerateRecords(
            new AcceptAllFilter(), null, false);
        if (filtered.numRecords() == 0) {
            result |= 4;
        }
        filtered.destroy();
        store.closeRecordStore();
        try {
            store.getNumRecords();
        } catch (RecordStoreNotOpenException expected) {
            result |= 8;
        }
        RecordStore.deleteRecordStore("errors");
        return result;
    }

    public static int advancedSemantics() throws Exception {
        final String name = "rms-advanced";
        try {
            RecordStore.deleteRecordStore(name);
        } catch (RecordStoreNotFoundException expected) {
        }

        listenerAdded = 0;
        listenerChanged = 0;
        listenerDeleted = 0;
        RecordStore first = RecordStore.openRecordStore(name, true);
        RecordStore second = RecordStore.openRecordStore(name, false);
        listenerStore = first;
        RecordListener listener = new CountingListener();
        first.addRecordListener(listener);

        int result = 0;
        int id3 = second.addRecord(new byte[] {3}, 0, 1);
        int id1 = second.addRecord(new byte[] {1}, 0, 1);
        int id2 = second.addRecord(new byte[] {2}, 0, 1);
        if (id3 == 1 && id1 == 2 && id2 == 3 && listenerAdded == 3) {
            result |= 1;
        }

        RecordFilter atLeastTwo = new AtLeastTwoFilter();
        RecordComparator descending = new DescendingComparator();
        RecordEnumeration live = first.enumerateRecords(
            atLeastTwo, descending, true);
        if (live.numRecords() == 2
                && live.nextRecord()[0] == 3
                && live.nextRecord()[0] == 2) {
            result |= 2;
        }

        int id4 = second.addRecord(new byte[] {4}, 0, 1);
        live.reset();
        if (live.numRecords() == 3 && live.nextRecordId() == id4) {
            result |= 4;
        }

        live.keepUpdated(false);
        int id5 = second.addRecord(new byte[] {5}, 0, 1);
        if (live.numRecords() == 3) {
            live.rebuild();
            if (live.numRecords() == 4 && live.nextRecordId() == id5) {
                result |= 8;
            }
        }

        second.setRecord(id1, new byte[] {6}, 0, 1);
        second.deleteRecord(id2);
        if (listenerAdded == 5 && listenerChanged == 1
                && listenerDeleted == 1) {
            result |= 16;
        }
        try {
            RecordStore.deleteRecordStore(name);
        } catch (RecordStoreException expected) {
            result |= 32;
        }

        first.removeRecordListener(listener);
        int transientId = second.addRecord(new byte[] {7}, 0, 1);
        second.deleteRecord(transientId);
        if (listenerAdded == 5 && listenerDeleted == 1) {
            result |= 64;
        }

        first.closeRecordStore();
        if (second.getNumRecords() == 4) {
            result |= 128;
        }
        try {
            live.nextRecord();
        } catch (RecordStoreNotOpenException expected) {
            result |= 256;
        } catch (InvalidRecordIDException unexpected) {
        }
        live.destroy();
        second.closeRecordStore();

        RecordStore reopened = RecordStore.openRecordStore(name, false);
        if (reopened.getNextRecordID() == transientId + 1
                && reopened.getVersion() == 9) {
            result |= 512;
        }
        reopened.closeRecordStore();
        RecordStore.deleteRecordStore(name);
        listenerStore = null;
        return result;
    }

    public static int callbackAndCursorSemantics() throws Exception {
        final String name = "rms-callback-cursor";
        try {
            RecordStore.deleteRecordStore(name);
        } catch (RecordStoreNotFoundException expected) {
        }

        int result = 0;
        RecordStore first = RecordStore.openRecordStore(name, true);
        RecordStore second = RecordStore.openRecordStore(name, false);
        HandleCountingListener firstListener =
            new HandleCountingListener(first);
        HandleCountingListener secondListener =
            new HandleCountingListener(second);
        first.addRecordListener(firstListener);
        second.addRecordListener(secondListener);

        int id1 = second.addRecord(new byte[] {1}, 0, 1);
        if (firstListener.added == 1 && secondListener.added == 1) {
            result |= 1;
        }

        HandleCountingListener replacement =
            new HandleCountingListener(first);
        MutatingListener mutating = new MutatingListener(first, replacement);
        first.addRecordListener(mutating);
        int id2 = second.addRecord(new byte[] {2}, 0, 1);
        if (mutating.added == 1 && replacement.added == 0) {
            result |= 2;
        }
        int id3 = second.addRecord(new byte[] {3}, 0, 1);
        if (mutating.added == 1 && replacement.added == 1) {
            result |= 4;
        }

        first.removeRecordListener(firstListener);
        second.removeRecordListener(secondListener);
        first.removeRecordListener(replacement);

        RecordEnumeration live = first.enumerateRecords(
            null, new AscendingComparator(), true);
        if (live.nextRecord()[0] == 1
                && live.nextRecord()[0] == 2
                && live.nextRecord()[0] == 3) {
            int id0 = second.addRecord(new byte[] {0}, 0, 1);
            if (!live.hasNextElement() && id0 > id3) {
                result |= 8;
            }
            int id4 = second.addRecord(new byte[] {4}, 0, 1);
            if (live.hasNextElement() && live.nextRecordId() == id4) {
                result |= 16;
            }
        }

        live.reset();
        if (live.nextRecord()[0] == 0) {
            second.deleteRecord(id1);
            if (live.nextRecordId() == id2) {
                result |= 32;
            }
        }

        try {
            first.enumerateRecords(new ThrowingFilter(), null, false);
        } catch (IllegalStateException expected) {
            result |= 64;
        }
        try {
            first.enumerateRecords(null, new ThrowingComparator(), false);
        } catch (IllegalStateException expected) {
            result |= 128;
        }

        live.destroy();
        try {
            live.numRecords();
        } catch (IllegalStateException expected) {
            result |= 256;
        }

        first.closeRecordStore();
        second.closeRecordStore();
        RecordStore.deleteRecordStore(name);
        return result;
    }

    public static int concurrentThreadSemantics() throws Exception {
        final String name = "rms-java-threads";
        try {
            RecordStore.deleteRecordStore(name);
        } catch (RecordStoreNotFoundException expected) {
        }

        threadErrors = 0;
        threadAdds = 0;
        RecordStore observer = RecordStore.openRecordStore(name, true);
        ThreadListener listener = new ThreadListener(observer);
        observer.addRecordListener(listener);
        Thread first = new Thread(new ConcurrentWriter(name, 1));
        Thread second = new Thread(new ConcurrentWriter(name, 2));
        first.start();
        second.start();
        first.join();
        second.join();

        int result = 0;
        if (threadErrors == 0 && !first.isAlive() && !second.isAlive()) {
            result |= 1;
        }
        if (threadAdds == 40 && observer.getNumRecords() == 40) {
            result |= 2;
        }
        if (observer.getNextRecordID() == 41) {
            result |= 4;
        }
        observer.removeRecordListener(listener);
        observer.closeRecordStore();

        RecordStore reopened = RecordStore.openRecordStore(name, false);
        if (reopened.getNumRecords() == 40
                && reopened.getNextRecordID() == 41) {
            result |= 8;
        }
        reopened.closeRecordStore();
        RecordStore.deleteRecordStore(name);
        return result;
    }
}
