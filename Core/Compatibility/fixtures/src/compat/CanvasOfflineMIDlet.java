package compat;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasOfflineMIDlet extends MIDlet {
    private final TestCanvas canvas = new TestCanvas();

    protected void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
        canvas.repaint();
        System.out.println("COMPAT_MILESTONE:canvas-start");
    }

    protected void pauseApp() {
        System.out.println("COMPAT_MILESTONE:canvas-pause");
    }

    protected void destroyApp(boolean unconditional) {
        System.out.println("COMPAT_MILESTONE:canvas-destroy");
    }

    private static final class TestCanvas extends Canvas {
        protected void paint(Graphics graphics) {
            graphics.setColor(0x123456);
            graphics.fillRect(0, 0, getWidth(), getHeight());
            graphics.setColor(0xffffff);
            graphics.drawString("phoneME corpus", 8, 8, Graphics.LEFT | Graphics.TOP);
            System.out.println("COMPAT_MILESTONE:canvas-paint");
        }
    }
}
