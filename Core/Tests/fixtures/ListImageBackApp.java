package corefixture;

import javax.microedition.lcdui.Alert;
import javax.microedition.lcdui.AlertType;
import javax.microedition.lcdui.Choice;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.List;
import javax.microedition.midlet.MIDlet;

public final class ListImageBackApp extends MIDlet
        implements CommandListener {
    private Display display;
    private List list;

    protected void startApp() {
        Image icon = Image.createImage(2, 2);
        Graphics graphics = icon.getGraphics();
        graphics.setColor(0x123456);
        graphics.fillRect(0, 0, 2, 2);

        list = new List(
                "Icons",
                Choice.IMPLICIT,
                new String[] {"Image item"},
                new Image[] {icon});
        list.setCommandListener(this);
        display = Display.getDisplay(this);
        display.setCurrent(list);
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void commandAction(Command command, Displayable displayable) {
        if (displayable != list || command != List.SELECT_COMMAND) {
            return;
        }
        Alert loading = new Alert(
                "Opening", "Returning", null, AlertType.INFO);
        loading.setTimeout(50);
        display.setCurrent(loading, list);
    }
}
