package javax.microedition.media.protocol;

/** Describes the MIME content type exposed by a SourceStream. */
public class ContentDescriptor {
    private final String contentType;

    public ContentDescriptor(String contentType) {
        if (contentType == null) {
            throw new IllegalArgumentException("contentType is null");
        }
        this.contentType = contentType;
    }

    public String getContentType() {
        return contentType;
    }
}
