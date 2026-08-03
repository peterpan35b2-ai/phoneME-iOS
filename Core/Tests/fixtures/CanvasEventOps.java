package corefixture;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class CanvasEventOps extends MIDlet {
    private EventCanvas canvas;

    protected void startApp() {
        if (canvas == null) {
            canvas = new EventCanvas();
            canvas.repaint();
        }
    }

    protected void pauseApp() { }
    protected void destroyApp(boolean unconditional) { }

    private final class EventCanvas extends Canvas {
        private int paintCount;

        EventCanvas() {
            Display.getDisplay(CanvasEventOps.this).setCurrent(this);
        }

        protected void paint(Graphics graphics) {
            paintCount++;
            setTitle("eventPaint:" + paintCount);
            if (paintCount == 1) {
                repaint(4, 5, 6, 7);
                serviceRepaints();
            }
        }

        protected void showNotify() {
            if (canvas != this) {
                throw new NullPointerException("showNotify ran before constructor assignment");
            }
            setTitle("eventShow");
        }

        protected void hideNotify() {
            setTitle("eventHide");
        }

        protected void sizeChanged(int width, int height) {
            setTitle("eventSize:" + width + ":" + height);
        }

        protected void keyPressed(int keyCode) {
            setTitle("eventDown:" + keyCode);
        }

        protected void keyRepeated(int keyCode) {
            setTitle("eventRepeat:" + keyCode);
        }

        protected void keyReleased(int keyCode) {
            setTitle("eventUp:" + keyCode);
        }

        protected void pointerPressed(int x, int y) {
            setTitle("eventPointerDown:" + x + ":" + y);
        }

        protected void pointerDragged(int x, int y) {
            setTitle("eventPointerDrag:" + x + ":" + y);
        }

        protected void pointerReleased(int x, int y) {
            setTitle("eventPointerUp:" + x + ":" + y);
        }
    }
}
