import java.io.IOException;
import java.io.InterruptedIOException;
import javax.microedition.io.Connector;
import javax.wireless.messaging.BinaryMessage;
import javax.wireless.messaging.MessageConnection;
import javax.wireless.messaging.TextMessage;

public final class WirelessMessagingOps {
    private WirelessMessagingOps() {}

    public static int classSurface() throws Exception {
        return Class.forName("javax.wireless.messaging.TextMessage") != null &&
               Class.forName("javax.wireless.messaging.MessageConnection") != null
            ? 1 : 0;
    }

    public static int textRoundTrip() throws Exception {
        MessageConnection connection = (MessageConnection) Connector.open(
            "sms://+84901234567", Connector.WRITE, true);
        if (!"text".equals(MessageConnection.TEXT_MESSAGE)) return -1;
        TextMessage message = (TextMessage) connection.newMessage(
            MessageConnection.TEXT_MESSAGE);
        message.setPayloadText("hello");
        message.setAddress("sms://+84907654321");
        int segments = connection.numberOfSegments(message);
        int result = message.getTimestamp() == null &&
                     "hello".equals(message.getPayloadText()) &&
                     "sms://+84907654321".equals(message.getAddress())
            ? segments : -2;
        connection.close();
        return result;
    }

    public static int binaryRoundTrip() throws Exception {
        MessageConnection connection = (MessageConnection) Connector.open(
            "sms://+84901234567", Connector.WRITE, true);
        BinaryMessage message = (BinaryMessage) connection.newMessage(
            MessageConnection.BINARY_MESSAGE, "sms://+84907654321");
        byte[] payload = new byte[] {1, 2, 3, 4};
        message.setPayloadData(payload);
        int result = message.getPayloadData()[2] == 3
            ? connection.numberOfSegments(message) : -1;
        connection.close();
        return result;
    }

    public static int sendRequiresHost() throws Exception {
        MessageConnection connection = (MessageConnection) Connector.open(
            "sms://+84901234567", Connector.WRITE, true);
        TextMessage message = (TextMessage) connection.newMessage(
            MessageConnection.TEXT_MESSAGE);
        message.setPayloadText("hello");
        try {
            connection.send(message);
            return 0;
        } catch (IOException expected) {
            return 1;
        } finally {
            connection.close();
        }
    }

    public static int receiveRequiresHost() throws Exception {
        MessageConnection connection = (MessageConnection) Connector.open(
            "sms://:5000", Connector.READ, true);
        try {
            connection.receive();
            return 0;
        } catch (InterruptedIOException expected) {
            return 1;
        } finally {
            connection.close();
        }
    }
}
