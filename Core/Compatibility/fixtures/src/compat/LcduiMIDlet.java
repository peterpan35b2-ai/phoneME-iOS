package compat;

import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Form;
import javax.microedition.lcdui.StringItem;
import javax.microedition.midlet.MIDlet;

public final class LcduiMIDlet extends MIDlet implements CommandListener {
    private final Command exit = new Command("Exit", Command.EXIT, 1);

    protected void startApp() {
        Form form = new Form("Compatibility");
        form.append(new StringItem("Status", "LCDUI ready"));
        form.addCommand(exit);
        form.setCommandListener(this);
        Display.getDisplay(this).setCurrent(form);
        System.out.println("COMPAT_MILESTONE:lcdui-form");
    }

    protected void pauseApp() {
        System.out.println("COMPAT_MILESTONE:lcdui-pause");
    }

    protected void destroyApp(boolean unconditional) {
        System.out.println("COMPAT_MILESTONE:lcdui-destroy");
    }

    public void commandAction(Command command, Displayable displayable) {
        if (command == exit) {
            notifyDestroyed();
        }
    }
}
