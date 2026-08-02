package javax.microedition.io;

import java.io.IOException;

public interface HttpConnection extends ContentConnection, OutputConnection {
    String HEAD = "HEAD";
    String GET = "GET";
    String POST = "POST";
    int HTTP_OK = 200;
    int HTTP_MOVED_PERM = 301;
    int HTTP_MOVED_TEMP = 302;
    int HTTP_NOT_FOUND = 404;
    int HTTP_INTERNAL_ERROR = 500;
    String getURL();
    String getProtocol();
    String getHost();
    String getFile();
    String getRef();
    String getQuery();
    int getPort();
    String getRequestMethod();
    void setRequestMethod(String method) throws IOException;
    String getRequestProperty(String key);
    void setRequestProperty(String key, String value) throws IOException;
    int getResponseCode() throws IOException;
    String getResponseMessage() throws IOException;
    long getExpiration() throws IOException;
    long getDate() throws IOException;
    long getLastModified() throws IOException;
    String getHeaderField(String name) throws IOException;
    String getHeaderField(int index) throws IOException;
    String getHeaderFieldKey(int index) throws IOException;
    int getHeaderFieldInt(String name, int fallback) throws IOException;
    long getHeaderFieldDate(String name, long fallback) throws IOException;
}
