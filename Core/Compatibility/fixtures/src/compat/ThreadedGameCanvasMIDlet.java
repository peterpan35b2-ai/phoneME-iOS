package compat;

import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.midlet.MIDlet;

public final class ThreadedGameCanvasMIDlet extends MIDlet implements Runnable {
    private final LoopCanvas canvas = new LoopCanvas();
    private volatile boolean running;
    private Thread loop;

    protected void startApp() {
        Display.getDisplay(this).setCurrent(canvas);
        if (loop == null) {
            running = true;
            loop = new Thread(this);
            loop.start();
        }
        System.out.println("COMPAT_MILESTONE:threaded-gamecanvas-start");
    }

    public void run() {
        int frames = 0;
        while (running && frames < 3) {
            canvas.frame = frames + 1;
            canvas.repaint();
            canvas.serviceRepaints();
            frames++;
            Thread.yield();
        }
        System.out.println("COMPAT_MILESTONE:threaded-gamecanvas-loop");
    }

    protected void pauseApp() {
        running = false;
    }

    protected void destroyApp(boolean unconditional) {
        running = false;
        System.out.println("COMPAT_MILESTONE:threaded-gamecanvas-destroy");
    }

    private static final class LoopCanvas extends GameCanvas {
        int frame;

        LoopCanvas() {
            super(false);
        }

        protected void paint(Graphics graphics) {
            graphics.setColor(0x101010 + frame * 0x10101);
            graphics.fillRect(0, 0, getWidth(), getHeight());
        }
    }
}
