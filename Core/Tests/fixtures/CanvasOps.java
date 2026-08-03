package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.midlet.MIDlet;

public final class CanvasOps extends MIDlet {
    private ProbeCanvas canvas;

    protected void startApp() {
        if (canvas == null) {
            canvas = new ProbeCanvas();
            Display.getDisplay(this).setCurrent(canvas);
            canvas.repaint(1, 2, 20, 20);
            canvas.repaint(10, 12, 30, 25);
            canvas.serviceRepaints();
            canvas.reportServiceRepaintsResult();
        }
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private static final class ProbeCanvas extends GameCanvas {
        private int paintCount;

        ProbeCanvas() {
            super(false);
            setTitle("constructed");
        }

        protected void paint(Graphics graphics) {
            ++paintCount;
            if (graphics != null) {
                graphics.setColor(0x123456);
                graphics.fillRect(0, 0, getWidth(), getHeight());
            }
            setTitle("paint:" + paintCount);
        }

        void reportServiceRepaintsResult() {
            setTitle("service:" + paintCount);
        }

        protected void showNotify() {
            setTitle("show");
            Graphics graphics = getGraphics();
            if (graphics != null) {
                graphics.setColor(0x334455);
                graphics.fillRect(0, 0, 4, 4);
                flushGraphics(0, 0, 4, 4);
            }
            repaint();
        }

        protected void hideNotify() {
            setTitle("hide");
        }

        protected void sizeChanged(int width, int height) {
            setTitle("size:" + width + ":" + height);
            repaint();
        }

        protected void keyPressed(int keyCode) {
            setTitle("down:" + keyCode + ":" +
                     getGameAction(keyCode) + ":" +
                     getKeyCode(Canvas.UP) + ":" +
                     getKeyStates() + ":" + getKeyName(keyCode));
        }

        protected void keyRepeated(int keyCode) {
            setTitle("repeat:" + keyCode + ":" + getKeyStates());
        }

        protected void keyReleased(int keyCode) {
            setTitle("up:" + keyCode + ":" + getKeyStates());
        }

        protected void pointerPressed(int x, int y) {
            setFullScreenMode(true);
            setTitle("pointerDown:" + x + ":" + y);
        }

        protected void pointerDragged(int x, int y) {
            setTitle("pointerDrag:" + x + ":" + y);
        }

        protected void pointerReleased(int x, int y) {
            setFullScreenMode(false);
            setTitle("pointerUp:" + x + ":" + y);
        }
    }
}
