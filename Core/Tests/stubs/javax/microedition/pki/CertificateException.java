package javax.microedition.pki;

import java.io.IOException;

public class CertificateException extends IOException {
    public static final byte BAD_EXTENSIONS = Byte.parseByte("1");
    public static final byte CERTIFICATE_CHAIN_TOO_LONG = Byte.parseByte("2");
    public static final byte EXPIRED = Byte.parseByte("3");
    public static final byte UNAUTHORIZED_INTERMEDIATE_CA = Byte.parseByte("4");
    public static final byte MISSING_SIGNATURE = Byte.parseByte("5");
    public static final byte NOT_YET_VALID = Byte.parseByte("6");
    public static final byte SITENAME_MISMATCH = Byte.parseByte("7");
    public static final byte UNRECOGNIZED_ISSUER = Byte.parseByte("8");
    public static final byte UNSUPPORTED_SIGALG = Byte.parseByte("9");
    public static final byte INAPPROPRIATE_KEY_USAGE = Byte.parseByte("10");
    public static final byte BROKEN_CHAIN = Byte.parseByte("11");
    public static final byte ROOT_CA_EXPIRED = Byte.parseByte("12");
    public static final byte UNSUPPORTED_PUBLIC_KEY_TYPE = Byte.parseByte("13");
    public static final byte VERIFICATION_FAILED = Byte.parseByte("14");

    private final Certificate certificate;
    private final byte reason;

    public CertificateException(Certificate certificate, byte status) {
        this.certificate = certificate;
        this.reason = status;
    }

    public CertificateException(String message,
                                Certificate certificate,
                                byte status) {
        super(message);
        this.certificate = certificate;
        this.reason = status;
    }

    public Certificate getCertificate() {
        return certificate;
    }

    public byte getReason() {
        return reason;
    }
}
