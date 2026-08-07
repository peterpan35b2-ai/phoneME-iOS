package javax.microedition.lcdui;

import java.io.IOException;
import java.io.InputStream;

public final class Image {
    private Image() {
    }

    public static Image createImage(int width, int height) { return null; }
    public static Image createMutableARGB(int width, int height) { return null; }
    public static Image createImage(String name) throws IOException { return null; }
    public static Image createImage(InputStream stream) throws IOException { return null; }
    public static Image createImage(byte[] data, int offset, int length) {
        return null;
    }
    public static Image createImage(Image source) { return null; }
    public static Image createImage(Image source, int x, int y,
                                    int width, int height, int transform) {
        return null;
    }
    public static Image createRGBImage(int[] rgb, int width, int height,
                                       boolean processAlpha) {
        return null;
    }

    public Graphics getGraphics() { return null; }
    public int getWidth() { return 0; }
    public int getHeight() { return 0; }
    public boolean isMutable() { return false; }
    public void getRGB(int[] rgbData, int offset, int scanlength,
                       int x, int y, int width, int height) {
    }
    public void setRGB(int[] rgbData, int offset, int scanlength,
                       int x, int y, int width, int height) {
    }
}
