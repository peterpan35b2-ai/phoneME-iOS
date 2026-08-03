package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasThrowOps extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new ThrowCanvas());
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private static final class ThrowCanvas extends Canvas {
        protected void paint(Graphics graphics) { }

        protected void pointerPressed(int x, int y) {
            throw new RuntimeException("canvas callback failure");
        }
    }
}
