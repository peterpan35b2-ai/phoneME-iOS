import java.io.InputStream;
import java.io.OutputStream;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.Datagram;
import javax.microedition.io.DatagramConnection;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.HttpsConnection;
import javax.microedition.io.SecurityInfo;
import javax.microedition.io.SocketConnection;
import javax.microedition.pki.Certificate;

public final class NetworkOps {
    private NetworkOps() {}

    public static int socketRoundTrip() throws Exception {
        SocketConnection connection = (SocketConnection) Connector.open(
            "socket://example.test:4321", Connector.READ_WRITE, true);
        int endpoint = connection.getPort();
        OutputStream output = connection.openOutputStream();
        InputStream input = connection.openInputStream();
        output.write(0x5A);
        output.flush();
        int reply = input.read();
        input.close();
        output.close();
        connection.close();
        return endpoint + reply;
    }

    public static int streamSurvivesConnectionClose() throws Exception {
        Connection connection = Connector.open(
            "socket://example.test:4321", Connector.READ_WRITE, true);
        OutputStream output = ((SocketConnection) connection).openOutputStream();
        InputStream input = ((SocketConnection) connection).openInputStream();
        connection.close();
        output.write(0x33);
        output.close();
        int value = input.read();
        input.close();
        return value;
    }

    public static int datagramRoundTrip() throws Exception {
        DatagramConnection connection = (DatagramConnection) Connector.open(
            "datagram://example.test:9876", Connector.READ_WRITE, true);
        Datagram scratch = connection.newDatagram(8);
        scratch.writeBytes("ok\n");
        scratch.reset();
        if (!"ok".equals(scratch.readLine())) return -1;
        Datagram outgoing = connection.newDatagram(16);
        outgoing.setAddress("datagram://example.test:9876");
        outgoing.writeInt(0x01020304);
        connection.send(outgoing);
        Datagram incoming = connection.newDatagram(16);
        connection.receive(incoming);
        int value = incoming.readInt();
        connection.close();
        return value;
    }

    public static int httpRoundTrip() throws Exception {
        HttpConnection connection = (HttpConnection) Connector.open(
            "http://example.test/api?q=1", Connector.READ_WRITE, true);
        connection.setRequestMethod(HttpConnection.POST);
        connection.setRequestProperty("X-Test", "fixture");
        OutputStream output = connection.openOutputStream();
        output.write(0x41);
        output.close();
        int status = connection.getResponseCode();
        int body = connection.openInputStream().read();
        int header = connection.getHeaderFieldInt("X-Number", -1);
        int port = connection.getPort();
        int compatibilityScore = 0;
        if (connection.getHeaderField(0).startsWith("HTTP/1.1 200")) compatibilityScore++;
        if (connection.getHeaderFieldKey(0) == null) compatibilityScore++;
        if ("1".equals(connection.getHeaderField(1))) compatibilityScore++;
        if ("Content-Length".equals(connection.getHeaderFieldKey(1))) compatibilityScore++;
        if (connection.getDate() == 784111777000L) compatibilityScore++;
        if (connection.getHeaderFieldDate("Date", -1L) == 784111777000L) compatibilityScore++;
        connection.close();
        return status + body + header + port + compatibilityScore;
    }

    public static int httpsRoundTrip() throws Exception {
        HttpsConnection connection = (HttpsConnection) Connector.open(
            "https://secure.test/resource", Connector.READ, true);
        int status = connection.getResponseCode();
        int body = connection.openInputStream().read();
        int port = connection.getPort();
        SecurityInfo security = connection.getSecurityInfo();
        Certificate certificate = security.getServerCertificate();
        int securityScore = 0;
        if ("TLS".equals(security.getProtocolName())) securityScore++;
        if ("TLSv1.3".equals(security.getProtocolVersion())) securityScore++;
        if ("TLS_AES_128_GCM_SHA256".equals(security.getCipherSuite())) securityScore++;
        if ("CN=secure.test".equals(certificate.getSubject())) securityScore++;
        if ("CN=Fixture CA".equals(certificate.getIssuer())) securityScore++;
        if (certificate.getNotAfter() > certificate.getNotBefore()) securityScore++;
        connection.close();
        return status + body + port + securityScore;
    }
}
