package com.nokia.mid.ui;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;

/** Nokia full-screen Canvas compatibility class. */
public abstract class FullCanvas extends Canvas {
    public static final int KEY_UP_ARROW = -1;
    public static final int KEY_DOWN_ARROW = -2;
    public static final int KEY_LEFT_ARROW = -3;
    public static final int KEY_RIGHT_ARROW = -4;
    public static final int KEY_SOFTKEY1 = -6;
    public static final int KEY_SOFTKEY2 = -7;
    public static final int KEY_SOFTKEY3 = -5;
    public static final int KEY_SEND = -10;
    public static final int KEY_END = -11;

    protected FullCanvas() {
        setFullScreenMode(true);
    }

    public void addCommand(Command command) {
        throw new IllegalStateException("Commands are not supported by FullCanvas");
    }

    public void setCommandListener(CommandListener listener) {
        throw new IllegalStateException("CommandListener is not supported by FullCanvas");
    }
}
