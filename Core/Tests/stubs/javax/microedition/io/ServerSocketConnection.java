package javax.microedition.io;

import java.io.IOException;

public interface ServerSocketConnection extends Connection {
    StreamConnection acceptAndOpen() throws IOException;
    String getLocalAddress() throws IOException;
    int getLocalPort() throws IOException;
}
