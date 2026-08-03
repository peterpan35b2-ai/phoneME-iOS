package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasThrowOps extends MIDlet {
    private Thread worker;

    protected void startApp() {
        Display.getDisplay(this).setCurrent(new ThrowCanvas());
        worker = new Thread(new Runnable() {
            public void run() {
                while (true) {
                    Thread.yield();
                }
            }
        });
        worker.start();
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) {
        try {
            worker.join();
        } catch (InterruptedException ignored) {
        }
    }

    private static final class ThrowCanvas extends Canvas {
        protected void paint(Graphics graphics) { }

        protected void pointerPressed(int x, int y) {
            throw new RuntimeException("canvas callback failure");
        }

        protected void hideNotify() {
            throw new RuntimeException("hide callback failure");
        }
    }
}
