package corefixture;

import javax.microedition.lcdui.Alert;
import javax.microedition.lcdui.AlertType;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;
import javax.microedition.lcdui.Gauge;
import javax.microedition.midlet.MIDlet;

public final class TimedAlertApp extends MIDlet {
    protected void startApp() {
        Display display = Display.getDisplay(this);
        Form loaded = new Form("Loaded");
        Alert loading = new Alert(
                "Loading", "Complete", null, AlertType.INFO);
        loading.setIndicator(new Gauge("Progress", false, 100, 100));
        loading.setTimeout(50);
        display.setCurrent(loading, loaded);
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
