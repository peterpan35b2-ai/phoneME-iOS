package compat;

import java.io.InputStream;
import java.io.InputStreamReader;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.DatagramConnection;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.SocketConnection;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;
import javax.microedition.midlet.MIDlet;

public final class NetworkApiMIDlet extends MIDlet {
    protected void startApp() {
        Form status = new Form("Network Fixture");
        Display.getDisplay(this).setCurrent(status);
        String url = System.getProperty("compat.http.url");
        if (url == null) {
            url = "http://127.0.0.1:18080/compat";
        }
        try {
            HttpConnection http = (HttpConnection) Connector.open(url);
            http.setRequestMethod(HttpConnection.GET);
            int response = http.getResponseCode();
            InputStream input = http.openInputStream();
            InputStreamReader reader = new InputStreamReader(input, "utf-8");
            int characters = 0;
            while (reader.read() >= 0) {
                characters++;
            }
            reader.close();
            http.close();
            System.out.println("COMPAT_NETWORK:http-get");
            if (response >= 200 && response < 400 && characters > 0) {
                status.append("HTTP " + response + ": " + characters + " chars");
                System.out.println("COMPAT_MILESTONE:http-response");
            } else {
                status.append("HTTP failed: " + response + ", chars=" + characters);
            }
        } catch (Exception error) {
            status.append("HTTP exception: " + error.toString());
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
