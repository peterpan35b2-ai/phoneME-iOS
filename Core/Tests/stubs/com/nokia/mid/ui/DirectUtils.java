package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;

public final class DirectUtils {
    private DirectUtils() {}
    public static native DirectGraphics getDirectGraphics(Graphics graphics);
}
