package javax.microedition.lcdui;

import java.util.Date;
import java.util.TimeZone;

public class DateField extends Item {
    public static final int DATE = 1;
    public static final int TIME = 2;
    public static final int DATE_TIME = 3;

    public DateField(String label, int mode) {
        super(label);
    }

    public DateField(String label, int mode, TimeZone zone) {
        super(label);
    }

    public Date getDate() { return null; }
    public void setDate(Date date) {}
    public int getInputMode() { return DATE_TIME; }
    public void setInputMode(int mode) {}
}
