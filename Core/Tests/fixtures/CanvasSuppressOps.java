package corefixture;

import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.midlet.MIDlet;

public final class CanvasSuppressOps extends MIDlet {
    private SuppressingCanvas canvas;

    protected void startApp() {
        if (canvas == null) {
            canvas = new SuppressingCanvas();
            Display.getDisplay(this).setCurrent(canvas);
            canvas.repaint();
        }
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private static final class SuppressingCanvas extends GameCanvas {
        SuppressingCanvas() {
            super(true);
        }

        protected void paint(Graphics graphics) { }

        protected void keyPressed(int keyCode) {
            setTitle("suppressDown:" + keyCode);
        }

        protected void keyRepeated(int keyCode) {
            setTitle("suppressRepeat:" + keyCode);
        }

        protected void keyReleased(int keyCode) {
            setTitle("suppressUp:" + keyCode);
        }
    }
}
