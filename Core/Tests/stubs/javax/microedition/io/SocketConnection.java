package javax.microedition.io;

import java.io.IOException;

public interface SocketConnection extends StreamConnection {
    byte DELAY = Byte.parseByte("0");
    byte LINGER = Byte.parseByte("1");
    byte KEEPALIVE = Byte.parseByte("2");
    byte RCVBUF = Byte.parseByte("3");
    byte SNDBUF = Byte.parseByte("4");
    String getAddress() throws IOException;
    String getLocalAddress() throws IOException;
    int getPort() throws IOException;
    int getLocalPort() throws IOException;
    void setSocketOption(byte option, int value) throws IOException;
    int getSocketOption(byte option) throws IOException;
}
