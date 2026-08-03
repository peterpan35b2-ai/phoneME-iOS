package javax.microedition.lcdui;

public class Alert extends Screen {
    public static final int FOREVER = -2;
    public static final Command DISMISS_COMMAND = null;

    public Alert(String title) {}
    public Alert(String title, String alertText, Image alertImage,
                 AlertType alertType) {}

    public String getString() { return null; }
    public void setString(String text) {}
    public Image getImage() { return null; }
    public void setImage(Image image) {}
    public AlertType getType() { return null; }
    public void setType(AlertType type) {}
    public int getTimeout() { return 0; }
    public void setTimeout(int timeout) {}
    public int getDefaultTimeout() { return 0; }
    public Gauge getIndicator() { return null; }
    public void setIndicator(Gauge indicator) { }
}
