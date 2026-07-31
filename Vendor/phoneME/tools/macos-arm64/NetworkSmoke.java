import java.io.DataInputStream;
import java.io.IOException;

import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.SocketConnection;
import javax.microedition.midlet.MIDlet;

/** Native phoneME network smoke test for the macOS arm64 port. */
public final class NetworkSmoke extends MIDlet implements Runnable {
    private Thread worker;

    protected void startApp() {
        if (worker == null) {
            worker = new Thread(this);
            worker.start();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void run() {
        try {
            testHttp();
            testSocket();
            System.out.println("NETWORK_SMOKE_OK");
        } catch (Throwable t) {
            System.out.println("NETWORK_SMOKE_FAILED: " + t);
            t.printStackTrace();
        } finally {
            notifyDestroyed();
        }
    }

    private static void testSocket() throws IOException {
        SocketConnection socket = null;
        try {
            socket = (SocketConnection) Connector.open(
                    "socket://nj1.teamobi.com:14444",
                    Connector.READ_WRITE,
                    true);
            System.out.println("SOCKET_OK: " + socket.getAddress()
                    + ":" + socket.getPort());
        } finally {
            if (socket != null) {
                socket.close();
            }
        }
    }

    private static void testHttp() throws IOException {
        HttpConnection http = null;
        DataInputStream input = null;
        try {
            http = (HttpConnection) Connector.open(
                    "http://teamobi.com/srvips/NJVI.txt",
                    Connector.READ,
                    true);
            int responseCode = http.getResponseCode();
            input = http.openDataInputStream();
            int firstByte = input.read();
            System.out.println("HTTP_OK: status=" + responseCode
                    + " firstByte=" + firstByte);
        } finally {
            if (input != null) {
                input.close();
            }
            if (http != null) {
                http.close();
            }
        }
    }
}
