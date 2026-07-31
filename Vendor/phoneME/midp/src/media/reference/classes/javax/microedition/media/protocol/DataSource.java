package javax.microedition.media.protocol;

import java.io.IOException;
import javax.microedition.media.Controllable;
import javax.microedition.media.Control;

/** Base class for application-defined JSR-135 media sources. */
public abstract class DataSource implements Controllable {
    private final String locator;

    public DataSource(String locator) {
        this.locator = locator;
    }

    public String getLocator() {
        return locator;
    }

    public abstract String getContentType();
    public abstract void connect() throws IOException;
    public abstract void disconnect();
    public abstract void start() throws IOException;
    public abstract void stop() throws IOException;
    public abstract SourceStream[] getStreams();
    public abstract Control[] getControls();
    public abstract Control getControl(String controlType);
}
