import java.io.InputStream;
import java.io.InputStreamReader;

import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
import javax.microedition.midlet.MIDlet;

import main.GameMidlet;

/** Diagnoses NinjaSchool server-list loading inside the installed suite. */
public final class NinjaSchoolServerProbe extends MIDlet {
    protected void startApp() {
        try {
            printArrays("STATIC_INIT");

            byte[] cached = w.a("NJlink");
            System.out.println("NJLINK_CACHE="
                    + (cached == null ? "null" : String.valueOf(cached.length)));
            System.out.println("IND_SERVER=" + w.c("indServer"));

            String[] stores = javax.microedition.rms.RecordStore.listRecordStores();
            System.out.println("RMS_STORES="
                    + (stores == null ? -1 : stores.length));
            if (stores != null) {
                for (int i = 0; i < stores.length; i++) {
                    System.out.println("RMS_STORE=" + stores[i]);
                }
            }

            GameMidlet.d();
            printArrays("AFTER_D");

            GameMidlet.c();
            printArrays("AFTER_C");

            testRawHttpAndSplit();
            System.out.println("NINJASCHOOL_SERVER_PROBE_OK");
        } catch (Throwable t) {
            System.out.println("NINJASCHOOL_SERVER_PROBE_FAILED: " + t);
            t.printStackTrace();
        } finally {
            notifyDestroyed();
        }
    }

    private static void printArrays(String stage) {
        System.out.println(stage
                + " l=" + length(GameMidlet.l)
                + " m=" + length(GameMidlet.m)
                + " n=" + length(GameMidlet.n)
                + " o=" + length(GameMidlet.o)
                + " p=" + length(GameMidlet.p)
                + " q=" + length(GameMidlet.q));

        if (GameMidlet.l != null && GameMidlet.l.length > 0) {
            System.out.println(stage + " first=" + GameMidlet.l[0]
                    + ":" + GameMidlet.m[0]
                    + ":" + GameMidlet.n[0]);
        }
    }

    private static int length(Object[] value) {
        return value == null ? -1 : value.length;
    }

    private static int length(short[] value) {
        return value == null ? -1 : value.length;
    }

    private static int length(byte[] value) {
        return value == null ? -1 : value.length;
    }

    private static void testRawHttpAndSplit() throws Exception {
        HttpConnection http = null;
        InputStream input = null;
        try {
            http = (HttpConnection) Connector.open(
                    "http://teamobi.com/srvips/NJVI.txt");
            int status = http.getResponseCode();
            input = http.openInputStream();
            InputStreamReader reader = new InputStreamReader(input, "utf-8");
            StringBuffer body = new StringBuffer();
            int ch;
            while ((ch = reader.read()) != -1) {
                body.append((char) ch);
            }

            String text = body.toString();
            String[] servers = am.a(text.trim(), ",", 0);
            System.out.println("RAW_HTTP status=" + status
                    + " chars=" + text.length()
                    + " servers=" + servers.length);
            if (servers.length > 0) {
                String[] fields = am.a(servers[0].trim(), ":", 0);
                System.out.println("RAW_FIRST fields=" + fields.length
                        + " value=" + servers[0]);
            }
        } finally {
            if (input != null) {
                input.close();
            }
            if (http != null) {
                http.close();
            }
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
