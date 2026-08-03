package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasRaceOps extends MIDlet implements Runnable {
    private RaceCanvas canvas;
    private Thread worker;
    private volatile boolean running;

    protected void startApp() {
        if (canvas != null) {
            running = true;
            return;
        }
        canvas = new RaceCanvas();
        Display.getDisplay(this).setCurrent(canvas);
        running = true;
        worker = new Thread(this);
        worker.start();
    }

    protected void pauseApp() { }

    protected void destroyApp(boolean unconditional) {
        running = false;
    }

    public void run() {
        int iteration = 0;
        while (running) {
            canvas.repaint(
                iteration & 31,
                (iteration >>> 1) & 31,
                24,
                24
            );
            if ((iteration & 3) == 0) {
                Thread.yield();
            }
            ++iteration;
        }
    }

    private static final class RaceCanvas extends Canvas {
        private int paintCount;

        protected void paint(Graphics graphics) {
            ++paintCount;
            if (graphics != null) {
                graphics.setColor(paintCount);
                graphics.fillRect(0, 0, getWidth(), getHeight());
            }
        }
    }
}
