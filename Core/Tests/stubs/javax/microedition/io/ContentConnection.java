package javax.microedition.io;

import java.io.IOException;

public interface ContentConnection extends InputConnection {
    String getType();
    String getEncoding();
    long getLength();
}
