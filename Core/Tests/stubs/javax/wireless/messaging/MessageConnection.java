package javax.wireless.messaging;

import java.io.IOException;
import javax.microedition.io.Connection;

public interface MessageConnection extends Connection {
    String TEXT_MESSAGE = "text";
    String BINARY_MESSAGE = "binary";
    String MULTIPART_MESSAGE = "multipart";

    Message newMessage(String type) throws IOException;
    Message newMessage(String type, String address) throws IOException;
    void send(Message message) throws IOException;
    Message receive() throws IOException;
    int numberOfSegments(Message message);
    void setMessageListener(MessageListener listener) throws IOException;
}
