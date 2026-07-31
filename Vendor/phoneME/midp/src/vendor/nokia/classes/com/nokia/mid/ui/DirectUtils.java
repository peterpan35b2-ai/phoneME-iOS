package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

/** Factory helpers for the Nokia DirectGraphics API. */
public final class DirectUtils {
    private DirectUtils() {
    }

    public static DirectGraphics getDirectGraphics(Graphics graphics) {
        if (graphics == null) {
            throw new NullPointerException("graphics is null");
        }
        return new DirectGraphicsImpl(graphics);
    }

    public static Image createImage(int width, int height, int argbColor) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("Invalid image dimensions");
        }
        Image image = Image.createImage(width, height);
        int[] row = new int[width];
        for (int i = 0; i < row.length; i++) {
            row[i] = argbColor;
        }
        Graphics graphics = image.getGraphics();
        for (int y = 0; y < height; y++) {
            graphics.drawRGB(row, 0, width, 0, y, width, 1, true);
        }
        return image;
    }

    public static Image createImage(byte[] imageData, int imageOffset,
            int imageLength) {
        Image source = Image.createImage(imageData, imageOffset, imageLength);
        Image result = Image.createImage(source.getWidth(), source.getHeight());
        result.getGraphics().drawImage(source, 0, 0, Graphics.TOP | Graphics.LEFT);
        return result;
    }
}
