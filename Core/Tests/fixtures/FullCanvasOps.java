package corefixture;

import com.nokia.mid.ui.FullCanvas;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.midlet.MIDlet;

public final class FullCanvasOps extends MIDlet {
    protected void startApp() {
        ProbeFullCanvas canvas = new ProbeFullCanvas();
        String result;
        if (FullCanvas.KEY_UP_ARROW != -1 ||
                FullCanvas.KEY_DOWN_ARROW != -2 ||
                FullCanvas.KEY_LEFT_ARROW != -3 ||
                FullCanvas.KEY_RIGHT_ARROW != -4 ||
                FullCanvas.KEY_SOFTKEY1 != -6 ||
                FullCanvas.KEY_SOFTKEY2 != -7 ||
                FullCanvas.KEY_SOFTKEY3 != -5 ||
                FullCanvas.KEY_SEND != -10 ||
                FullCanvas.KEY_END != -11) {
            result = "fullcanvas:constants";
        } else {
            boolean commandRejected = false;
            boolean listenerRejected = false;
            try {
                canvas.addCommand(new Command("blocked", Command.OK, 1));
            } catch (IllegalStateException expected) {
                commandRejected = true;
            }
            try {
                canvas.setCommandListener(null);
            } catch (IllegalStateException expected) {
                listenerRejected = true;
            }
            result = commandRejected && listenerRejected
                    ? "fullcanvas:ok"
                    : "fullcanvas:commands";
        }
        Display.getDisplay(this).setCurrent(canvas);
        canvas.setTitle(result);
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    private static final class ProbeFullCanvas extends FullCanvas {
        protected void paint(Graphics graphics) {
            graphics.setColor(0x224466);
            graphics.fillRect(0, 0, getWidth(), getHeight());
        }
    }
}
