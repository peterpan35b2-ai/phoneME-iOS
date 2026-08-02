package javax.microedition.io;

import javax.microedition.pki.Certificate;

public interface SecurityInfo {
    Certificate getServerCertificate();
    String getProtocolName();
    String getProtocolVersion();
    String getCipherSuite();
}
