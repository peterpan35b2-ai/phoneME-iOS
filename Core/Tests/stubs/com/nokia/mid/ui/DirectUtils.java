package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;

public final class DirectUtils {
    private DirectUtils() {}
    public static native DirectGraphics getDirectGraphics(Graphics graphics);
    public static native javax.microedition.lcdui.Image createImage(int width, int height, int argb);
    public static native javax.microedition.lcdui.Image createImage(byte[] imageData, int imageOffset, int imageLength);
}
