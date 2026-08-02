package javax.microedition.io;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

public interface Datagram extends DataInput, DataOutput {
    String getAddress();
    void setAddress(String address) throws IOException;
    void setAddress(Datagram reference);
    byte[] getData();
    int getOffset();
    int getLength();
    void setData(byte[] buffer, int offset, int length);
    void setLength(int length);
    void reset();
}
