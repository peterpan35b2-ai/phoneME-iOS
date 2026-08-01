/*
 * iOS-native HTTPS implementation for the phoneME MIDP runtime.
 *
 * TLS, certificate validation, redirects and modern cipher negotiation are
 * delegated to NSURLSession. The Java side preserves the MIDP
 * HttpsConnection request/response contract and buffers one request/response
 * per connection, which matches the way the vast majority of MIDlets use GCF.
 */
package com.sun.midp.io.j2me.https;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.InterruptedIOException;
import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Vector;

import javax.microedition.io.Connection;
import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
import javax.microedition.io.HttpsConnection;
import javax.microedition.io.SecurityInfo;
import javax.microedition.pki.Certificate;

import com.sun.j2me.security.AccessController;
import com.sun.j2me.security.InterruptedSecurityException;
import com.sun.midp.io.ConnectionBaseAdapter;
import com.sun.midp.io.HttpUrl;
import com.sun.midp.util.DateParser;

/** URLSession-backed implementation of the MIDP HTTPS protocol. */
public final class Protocol extends ConnectionBaseAdapter
        implements HttpsConnection {

    private static final String HTTPS_PERMISSION_NAME =
        "javax.microedition.io.Connector.https";

    private static final int DEFAULT_PORT = 443;
    private static final int DEFAULT_TIMEOUT_MS = 60000;

    private static final int STRING_RESPONSE_MESSAGE = 1;
    private static final int STRING_RESPONSE_HEADERS = 2;
    private static final int STRING_FINAL_URL = 3;
    private static final int STRING_TLS_PROTOCOL = 4;
    private static final int STRING_TLS_VERSION = 5;
    private static final int STRING_CIPHER_SUITE = 6;
    private static final int STRING_CERT_SUBJECT = 7;
    private static final int STRING_CERT_ISSUER = 8;
    private static final int STRING_CERT_SERIAL = 9;
    private static final int STRING_ERROR = 10;

    private static final int LONG_CERT_NOT_BEFORE = 1;
    private static final int LONG_CERT_NOT_AFTER = 2;

    private HttpUrl url;
    private String originalUrl;
    private String method = HttpConnection.GET;
    private int timeoutMs = DEFAULT_TIMEOUT_MS;

    private final Hashtable requestHeaders = new Hashtable();
    private final Vector responseHeaderNames = new Vector();
    private final Vector responseHeaderValues = new Vector();
    private final ByteArrayOutputStream requestBody =
        new ByteArrayOutputStream();

    private byte[] responseBody;
    private int responseOffset;
    private int responseCode = -1;
    private String responseMessage;
    private String finalUrl;
    private int nativeHandle;
    private boolean connected;
    private boolean outputOpened;

    /** Creates a fresh HTTPS connection. */
    public Protocol() {
    }

    /** Opens a GCF HTTPS URL. The supplied name excludes the scheme. */
    public Connection openPrim(String name, int mode, boolean timeouts)
            throws IOException {
        if (name == null || name.length() < 3 ||
                name.charAt(0) != '/' || name.charAt(1) != '/') {
            throw new IllegalArgumentException(
                "HTTPS URL must start with //");
        }

        checkPermission(name);
        initStreamConnection(mode);

        if (mode == Connector.READ) {
            maxOStreams = 0;
        } else if (mode == Connector.WRITE) {
            maxIStreams = 0;
        }

        url = new HttpUrl("https", name);
        if (url.host == null || url.host.length() == 0) {
            throw new IllegalArgumentException("missing host in URL");
        }
        if (url.port == -1) {
            url.port = DEFAULT_PORT;
        }

        originalUrl = url.toString();
        if (!originalUrl.startsWith("https:")) {
            originalUrl = "https:" + name;
        }
        return this;
    }

    private void checkPermission(String name) throws InterruptedIOException {
        try {
            AccessController.checkPermission(
                HTTPS_PERMISSION_NAME, "https:" + name);
        } catch (InterruptedSecurityException ise) {
            throw new InterruptedIOException(
                "Interrupted while requesting HTTPS permission");
        }
    }

    public InputStream openInputStream() throws IOException {
        InputStream stream = super.openInputStream();
        ensureResponse();
        return stream;
    }

    public OutputStream openOutputStream() throws IOException {
        ensureOpen();
        OutputStream stream = super.openOutputStream();
        outputOpened = true;
        return stream;
    }

    protected int readBytes(byte[] data, int offset, int length)
            throws IOException {
        ensureResponse();
        if (responseOffset >= responseBody.length) {
            return -1;
        }

        int count = responseBody.length - responseOffset;
        if (count > length) {
            count = length;
        }
        System.arraycopy(responseBody, responseOffset, data, offset, count);
        responseOffset += count;
        return count;
    }

    protected int writeBytes(byte[] data, int offset, int length)
            throws IOException {
        ensureOpen();
        if (connected) {
            throw new IOException("HTTPS request already sent");
        }
        requestBody.write(data, offset, length);
        return length;
    }

    protected void flush() throws IOException {
        ensureOpen();
        /* URLSession sends a complete request body at once. Keep flush as a
         * local buffer boundary so MIDlets may flush and continue writing;
         * the request is submitted on output close or response access. */
    }

    protected void closeOutputStream() throws IOException {
        IOException pending = null;
        try {
            ensureResponse();
        } catch (IOException ioe) {
            pending = ioe;
        }
        super.closeOutputStream();
        if (pending != null) {
            throw pending;
        }
    }

    protected void disconnect() throws IOException {
        if (nativeHandle != 0) {
            nClose(nativeHandle);
            nativeHandle = 0;
        }
        responseBody = null;
        responseOffset = 0;
    }

    private synchronized void ensureResponse() throws IOException {
        ensureOpen();
        if (connected) {
            return;
        }

        byte[] body = requestBody.toByteArray();
        nativeHandle = nExecute(
            originalUrl,
            method,
            encodeRequestHeaders(),
            body,
            timeoutMs);
        if (nativeHandle <= 0) {
            nativeHandle = 0;
            throw new IOException("Unable to start native HTTPS request");
        }

        String error = nGetString(nativeHandle, STRING_ERROR);
        if (error != null && error.length() != 0) {
            nClose(nativeHandle);
            nativeHandle = 0;
            throw new IOException(error);
        }

        responseCode = nGetStatusCode(nativeHandle);
        responseMessage = nGetString(
            nativeHandle, STRING_RESPONSE_MESSAGE);
        finalUrl = nGetString(nativeHandle, STRING_FINAL_URL);
        responseBody = nGetBody(nativeHandle);
        if (responseBody == null) {
            responseBody = new byte[0];
        }
        responseOffset = 0;
        decodeResponseHeaders(nGetString(
            nativeHandle, STRING_RESPONSE_HEADERS));
        connected = true;
    }

    private String encodeRequestHeaders() {
        StringBuffer result = new StringBuffer();
        Enumeration keys = requestHeaders.keys();
        while (keys.hasMoreElements()) {
            String key = (String)keys.nextElement();
            result.append(key);
            result.append(": ");
            result.append((String)requestHeaders.get(key));
            result.append("\r\n");
        }
        return result.toString();
    }

    private void decodeResponseHeaders(String rawHeaders) {
        responseHeaderNames.removeAllElements();
        responseHeaderValues.removeAllElements();
        if (rawHeaders == null || rawHeaders.length() == 0) {
            return;
        }

        int start = 0;
        int length = rawHeaders.length();
        while (start < length) {
            int end = rawHeaders.indexOf('\n', start);
            if (end == -1) {
                end = length;
            }
            int lineEnd = end;
            if (lineEnd > start && rawHeaders.charAt(lineEnd - 1) == '\r') {
                lineEnd--;
            }
            if (lineEnd > start) {
                String line = rawHeaders.substring(start, lineEnd);
                int colon = line.indexOf(':');
                if (colon > 0) {
                    String key = line.substring(0, colon).trim();
                    String value = line.substring(colon + 1).trim();
                    responseHeaderNames.addElement(key);
                    responseHeaderValues.addElement(value);
                }
            }
            start = end + 1;
        }
    }

    public String getURL() {
        return originalUrl;
    }

    public String getProtocol() {
        return "https";
    }

    public String getHost() {
        return url == null ? null : url.host;
    }

    public String getFile() {
        return url == null ? null : url.path;
    }

    public String getRef() {
        return url == null ? null : url.fragment;
    }

    public String getQuery() {
        return url == null ? null : url.query;
    }

    public int getPort() {
        return url == null || url.port == -1 ? DEFAULT_PORT : url.port;
    }

    public String getRequestMethod() {
        return method;
    }

    public void setRequestMethod(String newMethod) throws IOException {
        ensureOpen();
        if (connected) {
            throw new IOException("HTTPS connection already open");
        }
        if (outputOpened) {
            return;
        }
        if (!HttpConnection.GET.equals(newMethod) &&
                !HttpConnection.POST.equals(newMethod) &&
                !HttpConnection.HEAD.equals(newMethod)) {
            throw new IOException("unsupported method: " + newMethod);
        }
        method = newMethod;
    }

    public String getRequestProperty(String key) {
        if (key == null) {
            return null;
        }
        Enumeration keys = requestHeaders.keys();
        while (keys.hasMoreElements()) {
            String storedKey = (String)keys.nextElement();
            if (storedKey.equalsIgnoreCase(key)) {
                return (String)requestHeaders.get(storedKey);
            }
        }
        return null;
    }

    public void setRequestProperty(String key, String value)
            throws IOException {
        ensureOpen();
        if (connected) {
            throw new IOException("HTTPS connection already open");
        }
        if (outputOpened) {
            return;
        }
        if (key == null || value == null) {
            throw new NullPointerException();
        }
        if (key.length() == 0 || key.indexOf(':') != -1 ||
                containsLineBreak(key) || containsIllegalLineBreak(value)) {
            throw new IllegalArgumentException("illegal HTTP header");
        }

        String previousKey = findRequestHeaderKey(key);
        if (previousKey != null) {
            requestHeaders.remove(previousKey);
        }
        requestHeaders.put(key, value);
    }

    private String findRequestHeaderKey(String key) {
        Enumeration keys = requestHeaders.keys();
        while (keys.hasMoreElements()) {
            String storedKey = (String)keys.nextElement();
            if (storedKey.equalsIgnoreCase(key)) {
                return storedKey;
            }
        }
        return null;
    }

    private static boolean containsLineBreak(String value) {
        return value.indexOf('\r') != -1 || value.indexOf('\n') != -1;
    }

    private static boolean containsIllegalLineBreak(String value) {
        int index = 0;
        while (true) {
            int cr = value.indexOf("\r\n", index);
            if (cr == -1) {
                return value.indexOf('\r', index) != -1 ||
                    value.indexOf('\n', index) != -1;
            }
            int continuation = cr + 2;
            if (continuation >= value.length() ||
                    (value.charAt(continuation) != ' ' &&
                     value.charAt(continuation) != '\t')) {
                return true;
            }
            index = continuation + 1;
        }
    }

    public int getResponseCode() throws IOException {
        ensureResponse();
        return responseCode;
    }

    public String getResponseMessage() throws IOException {
        ensureResponse();
        return responseMessage;
    }

    public long getExpiration() throws IOException {
        return getHeaderFieldDate("expires", 0);
    }

    public long getDate() throws IOException {
        return getHeaderFieldDate("date", 0);
    }

    public long getLastModified() throws IOException {
        return getHeaderFieldDate("last-modified", 0);
    }

    public String getHeaderField(String name) throws IOException {
        ensureResponse();
        if (name == null) {
            return getHeaderField(0);
        }
        for (int i = 0; i < responseHeaderNames.size(); i++) {
            String key = (String)responseHeaderNames.elementAt(i);
            if (key.equalsIgnoreCase(name)) {
                return (String)responseHeaderValues.elementAt(i);
            }
        }
        return null;
    }

    public int getHeaderFieldInt(String name, int defaultValue)
            throws IOException {
        String value = getHeaderField(name);
        if (value == null) {
            return defaultValue;
        }
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException nfe) {
            return defaultValue;
        }
    }

    public long getHeaderFieldDate(String name, long defaultValue)
            throws IOException {
        String value = getHeaderField(name);
        if (value == null) {
            return defaultValue;
        }
        try {
            return DateParser.parse(value);
        } catch (IllegalArgumentException iae) {
            return defaultValue;
        }
    }

    public String getHeaderField(int index) throws IOException {
        ensureResponse();
        if (index == 0) {
            StringBuffer status = new StringBuffer("HTTP/1.1 ");
            status.append(responseCode);
            if (responseMessage != null && responseMessage.length() != 0) {
                status.append(' ');
                status.append(responseMessage);
            }
            return status.toString();
        }
        int headerIndex = index - 1;
        if (headerIndex < 0 || headerIndex >= responseHeaderValues.size()) {
            return null;
        }
        return (String)responseHeaderValues.elementAt(headerIndex);
    }

    public String getHeaderFieldKey(int index) throws IOException {
        ensureResponse();
        if (index == 0) {
            return null;
        }
        int headerIndex = index - 1;
        if (headerIndex < 0 || headerIndex >= responseHeaderNames.size()) {
            return null;
        }
        return (String)responseHeaderNames.elementAt(headerIndex);
    }

    public String getType() {
        try {
            return getHeaderField("content-type");
        } catch (IOException ioe) {
            return null;
        }
    }

    public String getEncoding() {
        try {
            return getHeaderField("content-encoding");
        } catch (IOException ioe) {
            return null;
        }
    }

    public long getLength() {
        try {
            String value = getHeaderField("content-length");
            if (value != null) {
                try {
                    return Long.parseLong(value.trim());
                } catch (NumberFormatException ignored) {
                }
            }
            ensureResponse();
            return responseBody.length;
        } catch (IOException ioe) {
            return -1;
        }
    }

    public SecurityInfo getSecurityInfo() throws IOException {
        ensureResponse();
        return new IOSSecurityInfo(
            safeString(nGetString(nativeHandle, STRING_TLS_PROTOCOL), "TLS"),
            safeString(nGetString(nativeHandle, STRING_TLS_VERSION), "1.2"),
            safeString(nGetString(nativeHandle, STRING_CIPHER_SUITE),
                "IOS_SYSTEM_CIPHER"),
            new IOSCertificate(
                safeString(nGetString(nativeHandle, STRING_CERT_SUBJECT),
                    "CN=" + getHost()),
                safeString(nGetString(nativeHandle, STRING_CERT_ISSUER),
                    "iOS Trust Store"),
                nGetString(nativeHandle, STRING_CERT_SERIAL),
                nGetLong(nativeHandle, LONG_CERT_NOT_BEFORE),
                nGetLong(nativeHandle, LONG_CERT_NOT_AFTER)));
    }

    private static String safeString(String value, String fallback) {
        return value == null || value.length() == 0 ? fallback : value;
    }

    /** Final URL after redirects, retained for native diagnostics. */
    String getFinalURL() throws IOException {
        ensureResponse();
        return finalUrl == null ? originalUrl : finalUrl;
    }

    private static final class IOSSecurityInfo implements SecurityInfo {
        private final String protocolName;
        private final String protocolVersion;
        private final String cipherSuite;
        private final Certificate certificate;

        IOSSecurityInfo(String protocolName, String protocolVersion,
                String cipherSuite, Certificate certificate) {
            this.protocolName = protocolName;
            this.protocolVersion = protocolVersion;
            this.cipherSuite = cipherSuite;
            this.certificate = certificate;
        }

        public Certificate getServerCertificate() {
            return certificate;
        }

        public String getProtocolVersion() {
            return protocolVersion;
        }

        public String getProtocolName() {
            return protocolName;
        }

        public String getCipherSuite() {
            return cipherSuite;
        }
    }

    private static final class IOSCertificate implements Certificate {
        private final String subject;
        private final String issuer;
        private final String serial;
        private final long notBefore;
        private final long notAfter;

        IOSCertificate(String subject, String issuer, String serial,
                long notBefore, long notAfter) {
            this.subject = subject;
            this.issuer = issuer;
            this.serial = serial;
            this.notBefore = notBefore < 0 ? 0 : notBefore;
            this.notAfter = notAfter <= 0 ? Long.MAX_VALUE : notAfter;
        }

        public String getSubject() {
            return subject;
        }

        public String getIssuer() {
            return issuer;
        }

        public String getType() {
            return "X.509";
        }

        public String getVersion() {
            return "3";
        }

        public String getSigAlgName() {
            return "iOS System Trust";
        }

        public long getNotBefore() {
            return notBefore;
        }

        public long getNotAfter() {
            return notAfter;
        }

        public String getSerialNumber() {
            return serial;
        }
    }

    private static native int nExecute(String url, String method,
        String headers, byte[] body, int timeoutMs);
    private static native int nGetStatusCode(int handle);
    private static native String nGetString(int handle, int field);
    private static native byte[] nGetBody(int handle);
    private static native long nGetLong(int handle, int field);
    private static native void nClose(int handle);
}
