import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.Datagram;
import javax.microedition.io.DatagramConnection;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.HttpsConnection;
import javax.microedition.io.SecurityInfo;
import javax.microedition.io.ServerSocketConnection;
import javax.microedition.io.SocketConnection;
import javax.microedition.pki.Certificate;
import javax.microedition.pki.CertificateException;

public final class NetworkOps {
    private static int interruptedReadResult;

    private NetworkOps() {}

    private static final class BlockingReader implements Runnable {
        public void run() {
            SocketConnection connection = null;
            InputStream input = null;
            try {
                connection = (SocketConnection) Connector.open(
                    "socket://interrupt.test:7200",
                    Connector.READ_WRITE, true);
                input = connection.openInputStream();
                input.read();
                interruptedReadResult = 1;
            } catch (InterruptedIOException expected) {
                interruptedReadResult = 2;
            } catch (Exception failure) {
                interruptedReadResult = 3;
            } finally {
                try {
                    if (input != null) input.close();
                } catch (Exception ignored) {}
                try {
                    if (connection != null) connection.close();
                } catch (Exception ignored) {}
            }
        }
    }

    public static int interruptedRead() throws Exception {
        interruptedReadResult = 0;
        Thread worker = new Thread(new BlockingReader());
        worker.start();
        Thread.sleep(50L);
        worker.interrupt();
        worker.join(2000L);
        return worker.isAlive() ? 4 : interruptedReadResult;
    }

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

    public static int nestedDataOutputBulkWrite() throws Exception {
        SocketConnection connection = (SocketConnection) Connector.open(
            "socket://example.test:4321", Connector.READ_WRITE, true);
        DataOutputStream output = new DataOutputStream(
            new BufferedOutputStream(connection.openOutputStream()));
        DataInputStream input = new DataInputStream(connection.openInputStream());
        output.writeInt(0x12345678);
        output.flush();
        int value = input.readInt();
        input.close();
        output.close();
        connection.close();
        return value;
    }

    public static int socketExactReadSemantics() throws Exception {
        SocketConnection connection = (SocketConnection) Connector.open(
            "socket://example.test:4321", Connector.READ_WRITE, true);
        OutputStream output = connection.openOutputStream();
        InputStream input = connection.openInputStream();
        byte[] payload = new byte[64];
        for (int index = 0; index < payload.length; index++) {
            payload[index] = (byte) index;
        }
        output.write(payload);
        output.flush();

        byte[] first = new byte[3];
        byte[] second = new byte[5];
        byte[] remainder = new byte[56];
        int firstCount = input.read(first);
        int secondCount = input.read(second);
        int remainderCount = input.read(remainder);
        int checksum = 0;
        for (int index = 0; index < first.length; index++) {
            checksum += first[index] & 0xFF;
        }
        for (int index = 0; index < second.length; index++) {
            checksum += second[index] & 0xFF;
        }
        for (int index = 0; index < remainder.length; index++) {
            checksum += remainder[index] & 0xFF;
        }

        input.close();
        output.close();
        connection.close();
        if (firstCount != 3 || secondCount != 5 || remainderCount != 56) {
            return -1;
        }
        return checksum;
    }

    public static int socketLargePayload() throws Exception {
        SocketConnection connection = (SocketConnection) Connector.open(
            "socket://example.test:4321", Connector.READ_WRITE, true);
        DataOutputStream output = new DataOutputStream(
            connection.openOutputStream());
        DataInputStream input = new DataInputStream(
            connection.openInputStream());
        byte[] payload = new byte[64 * 1024];
        for (int index = 0; index < payload.length; index++) {
            payload[index] = (byte) ((index * 37 + 11) & 0xFF);
        }
        output.write(payload);
        output.flush();

        byte[] received = new byte[payload.length];
        input.readFully(received);
        int result = 1;
        for (int index = 0; index < payload.length; index++) {
            if (received[index] != payload[index]) {
                result = 0;
                break;
            }
        }

        input.close();
        output.close();
        connection.close();
        return result;
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

    public static int serverSocketOpenClose() throws Exception {
        ServerSocketConnection connection = (ServerSocketConnection)
            Connector.open("socket://:12345", Connector.READ_WRITE, true);
        int port = connection.getLocalPort();
        connection.close();
        return port;
    }

    public static int datagramReceiverOpenClose() throws Exception {
        DatagramConnection connection = (DatagramConnection) Connector.open(
            "datagram://:9876", Connector.READ_WRITE, true);
        connection.close();
        return 1;
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

    public static int runtimeConstantSurface() {
        int score = HttpConnection.HEAD.length()
            + HttpConnection.GET.length()
            + HttpConnection.POST.length();
        score += Connector.READ + Connector.WRITE + Connector.READ_WRITE;
        score += SocketConnection.DELAY + SocketConnection.LINGER
            + SocketConnection.KEEPALIVE + SocketConnection.RCVBUF
            + SocketConnection.SNDBUF;
        score += CertificateException.BAD_EXTENSIONS
            + CertificateException.CERTIFICATE_CHAIN_TOO_LONG
            + CertificateException.EXPIRED
            + CertificateException.UNAUTHORIZED_INTERMEDIATE_CA
            + CertificateException.MISSING_SIGNATURE
            + CertificateException.NOT_YET_VALID
            + CertificateException.SITENAME_MISMATCH
            + CertificateException.UNRECOGNIZED_ISSUER
            + CertificateException.UNSUPPORTED_SIGALG
            + CertificateException.INAPPROPRIATE_KEY_USAGE
            + CertificateException.BROKEN_CHAIN
            + CertificateException.ROOT_CA_EXPIRED
            + CertificateException.UNSUPPORTED_PUBLIC_KEY_TYPE
            + CertificateException.VERIFICATION_FAILED;
        score += HttpConnection.HTTP_OK + HttpConnection.HTTP_CREATED
            + HttpConnection.HTTP_ACCEPTED
            + HttpConnection.HTTP_NOT_AUTHORITATIVE
            + HttpConnection.HTTP_NO_CONTENT + HttpConnection.HTTP_RESET
            + HttpConnection.HTTP_PARTIAL + HttpConnection.HTTP_MULT_CHOICE
            + HttpConnection.HTTP_MOVED_PERM
            + HttpConnection.HTTP_MOVED_TEMP
            + HttpConnection.HTTP_SEE_OTHER
            + HttpConnection.HTTP_NOT_MODIFIED
            + HttpConnection.HTTP_USE_PROXY
            + HttpConnection.HTTP_TEMP_REDIRECT
            + HttpConnection.HTTP_BAD_REQUEST
            + HttpConnection.HTTP_UNAUTHORIZED
            + HttpConnection.HTTP_PAYMENT_REQUIRED
            + HttpConnection.HTTP_FORBIDDEN + HttpConnection.HTTP_NOT_FOUND
            + HttpConnection.HTTP_BAD_METHOD
            + HttpConnection.HTTP_NOT_ACCEPTABLE
            + HttpConnection.HTTP_PROXY_AUTH
            + HttpConnection.HTTP_CLIENT_TIMEOUT
            + HttpConnection.HTTP_CONFLICT + HttpConnection.HTTP_GONE
            + HttpConnection.HTTP_LENGTH_REQUIRED
            + HttpConnection.HTTP_PRECON_FAILED
            + HttpConnection.HTTP_ENTITY_TOO_LARGE
            + HttpConnection.HTTP_REQ_TOO_LONG
            + HttpConnection.HTTP_UNSUPPORTED_TYPE
            + HttpConnection.HTTP_UNSUPPORTED_RANGE
            + HttpConnection.HTTP_EXPECT_FAILED
            + HttpConnection.HTTP_INTERNAL_ERROR
            + HttpConnection.HTTP_NOT_IMPLEMENTED
            + HttpConnection.HTTP_BAD_GATEWAY
            + HttpConnection.HTTP_UNAVAILABLE
            + HttpConnection.HTTP_GATEWAY_TIMEOUT
            + HttpConnection.HTTP_VERSION;
        return score;
    }

    public static int certificateExceptionRoundTrip() throws Exception {
        CertificateException siteName = new CertificateException(
            (Certificate) null, CertificateException.SITENAME_MISMATCH);
        CertificateException verification = new CertificateException(
            "verify", null, CertificateException.VERIFICATION_FAILED);
        int score = 0;
        if (siteName.getCertificate() == null) score += siteName.getReason();
        if ("Certificate does not contain the correct site name".equals(
                siteName.getMessage())) {
            score += 100;
        }
        if (siteName.getCause() == null) score += 10;
        if (verification.getCertificate() == null) {
            score += verification.getReason();
        }
        if ("verify".equals(verification.getMessage())) score += 100;
        if (verification.getCause() == null) score += 10;
        return score;
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
