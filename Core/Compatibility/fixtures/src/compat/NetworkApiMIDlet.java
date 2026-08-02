package compat;

import java.io.InputStream;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.DatagramConnection;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.SocketConnection;
import javax.microedition.midlet.MIDlet;

public final class NetworkApiMIDlet extends MIDlet {
    protected void startApp() {
        String url = System.getProperty("compat.http.url");
        if (url == null) {
            url = "http://127.0.0.1:18080/compat";
        }
        try {
            HttpConnection http = (HttpConnection) Connector.open(url);
            http.setRequestMethod(HttpConnection.GET);
            int response = http.getResponseCode();
            InputStream input = http.openInputStream();
            while (input.read() >= 0) {
            }
            input.close();
            http.close();
            System.out.println("COMPAT_NETWORK:http-get");
            if (response >= 200 && response < 400) {
                System.out.println("COMPAT_MILESTONE:http-response");
            }
        } catch (Exception error) {
            throw new RuntimeException(error.toString());
        }
    }

    private void referenceSocketAndDatagramApis() throws Exception {
        Connection socket = Connector.open("socket://127.0.0.1:18081");
        ((SocketConnection) socket).setSocketOption(SocketConnection.DELAY, 0);
        socket.close();
        DatagramConnection datagrams =
                (DatagramConnection) Connector.open("datagram://:18082");
        datagrams.close();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
        System.out.println("COMPAT_MILESTONE:network-destroy");
    }
}
