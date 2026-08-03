package javax.microedition.lcdui;

public class AlertType {
    public static final AlertType INFO = null;
    public static final AlertType WARNING = null;
    public static final AlertType ERROR = null;
    public static final AlertType ALARM = null;
    public static final AlertType CONFIRMATION = null;

    protected AlertType() {}
    public boolean playSound(Display display) { return false; }
}
