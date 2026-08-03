package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasBudgetOps extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new BusyCanvas());
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private static final class BusyCanvas extends Canvas {
        protected void paint(Graphics graphics) {
            graphics.setColor(0x224466);
            graphics.fillRect(0, 0, getWidth(), getHeight());
            int counter = 0;
            while (true) {
                counter = counter * 31 + 1;
                if (counter == Integer.MIN_VALUE) {
                    graphics.setColor(counter);
                }
            }
        }
    }
}
