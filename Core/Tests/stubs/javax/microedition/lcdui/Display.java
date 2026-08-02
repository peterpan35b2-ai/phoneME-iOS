package javax.microedition.lcdui;

import javax.microedition.midlet.MIDlet;

public final class Display {
    private Display() {
    }

    public static Display getDisplay(MIDlet midlet) { return null; }
    public Displayable getCurrent() { return null; }
    public void setCurrent(Displayable displayable) { }
}
