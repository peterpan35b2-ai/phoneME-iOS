package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasCopyAreaOps extends MIDlet {
    private CopyCanvas canvas;

    protected void startApp() {
        if (canvas == null) {
            canvas = new CopyCanvas();
            Display.getDisplay(this).setCurrent(canvas);
        }
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private static final class CopyCanvas extends Canvas {
        protected void paint(Graphics graphics) {
            boolean blocked = false;
            try {
                graphics.copyArea(0, 0, 1, 1,
                                  1, 1, Graphics.LEFT | Graphics.TOP);
            } catch (IllegalStateException expected) {
                blocked = true;
            }
            graphics.setColor(blocked ? 0x00CC66 : 0xCC0000);
            graphics.fillRect(0, 0, getWidth(), getHeight());
            setTitle(blocked ? "copyBlocked" : "copyAllowed");
        }
    }
}
