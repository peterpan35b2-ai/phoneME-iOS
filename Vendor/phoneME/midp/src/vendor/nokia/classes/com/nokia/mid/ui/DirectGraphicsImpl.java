package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

final class DirectGraphicsImpl implements DirectGraphics {
    private final Graphics graphics;
    private int alpha = 255;
    private int lastRGB;

    DirectGraphicsImpl(Graphics graphics) {
        this.graphics = graphics;
        lastRGB = graphics.getColor();
    }

    public void setARGBColor(int argbColor) {
        alpha = (argbColor >>> 24) & 0xff;
        lastRGB = argbColor & 0x00ffffff;
        graphics.setColor(lastRGB);
    }

    public int getAlphaComponent() {
        synchronizeColor();
        return alpha;
    }

    public int getNativePixelFormat() {
        return TYPE_USHORT_565_RGB;
    }

    public void drawImage(Image image, int x, int y, int anchor,
            int manipulation) {
        if (image == null) {
            throw new NullPointerException("image is null");
        }
        int width = image.getWidth();
        int height = image.getHeight();
        int[] source = new int[width * height];
        image.getRGB(source, 0, width, 0, 0, width, height);
        drawARGB(source, width, height, x, y, anchor, manipulation);
    }

    public void drawPixels(int[] pixels, boolean transparency,
            int offset, int scanlength, int x, int y, int width, int height,
            int manipulation, int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        if (format != TYPE_INT_888_RGB && format != TYPE_INT_8888_ARGB) {
            throw new IllegalArgumentException("Unsupported int pixel format");
        }
        checkDimensions(width, height);
        int[] argb = new int[width * height];
        for (int row = 0; row < height; row++) {
            int sourceIndex = offset + row * scanlength;
            int targetIndex = row * width;
            for (int column = 0; column < width; column++) {
                int value = pixels[sourceIndex + column];
                if (format == TYPE_INT_888_RGB || !transparency) {
                    value |= 0xff000000;
                }
                argb[targetIndex + column] = value;
            }
        }
        drawARGB(argb, width, height, x, y,
                Graphics.TOP | Graphics.LEFT, manipulation);
    }

    public void drawPixels(short[] pixels, boolean transparency,
            int offset, int scanlength, int x, int y, int width, int height,
            int manipulation, int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        checkShortFormat(format);
        checkDimensions(width, height);
        int[] argb = new int[width * height];
        for (int row = 0; row < height; row++) {
            int sourceIndex = offset + row * scanlength;
            int targetIndex = row * width;
            for (int column = 0; column < width; column++) {
                argb[targetIndex + column] = shortToARGB(
                        pixels[sourceIndex + column] & 0xffff,
                        transparency, format);
            }
        }
        drawARGB(argb, width, height, x, y,
                Graphics.TOP | Graphics.LEFT, manipulation);
    }

    public void drawPixels(byte[] pixels, byte[] transparencyMask,
            int offset, int scanlength, int x, int y, int width, int height,
            int manipulation, int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        checkByteFormat(format);
        checkDimensions(width, height);
        int[] argb = new int[width * height];
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                int pixelPosition = offset + row * scanlength + column;
                int value = bytePixelToARGB(pixels, pixelPosition,
                        scanlength, format);
                if (transparencyMask != null &&
                        !getPackedBit(transparencyMask, pixelPosition)) {
                    value &= 0x00ffffff;
                }
                argb[row * width + column] = value;
            }
        }
        drawARGB(argb, width, height, x, y,
                Graphics.TOP | Graphics.LEFT, manipulation);
    }

    public void getPixels(int[] pixels, int offset, int scanlength,
            int x, int y, int width, int height, int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        if (format != TYPE_INT_888_RGB && format != TYPE_INT_8888_ARGB) {
            throw new IllegalArgumentException("Unsupported int pixel format");
        }
        int[] argb = readARGB(x, y, width, height);
        for (int row = 0; row < height; row++) {
            int targetIndex = offset + row * scanlength;
            int sourceIndex = row * width;
            for (int column = 0; column < width; column++) {
                int value = argb[sourceIndex + column];
                pixels[targetIndex + column] = format == TYPE_INT_888_RGB
                        ? value & 0x00ffffff : value;
            }
        }
    }

    public void getPixels(short[] pixels, int offset, int scanlength,
            int x, int y, int width, int height, int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        checkShortFormat(format);
        int[] argb = readARGB(x, y, width, height);
        for (int row = 0; row < height; row++) {
            int targetIndex = offset + row * scanlength;
            int sourceIndex = row * width;
            for (int column = 0; column < width; column++) {
                pixels[targetIndex + column] = (short)argbToShort(
                        argb[sourceIndex + column], format);
            }
        }
    }

    public void getPixels(byte[] pixels, byte[] transparencyMask,
            int offset, int scanlength, int x, int y, int width, int height,
            int format) {
        if (pixels == null) {
            throw new NullPointerException("pixels is null");
        }
        checkByteFormat(format);
        int[] argb = readARGB(x, y, width, height);
        for (int row = 0; row < height; row++) {
            for (int column = 0; column < width; column++) {
                int position = offset + row * scanlength + column;
                int value = argb[row * width + column];
                setBytePixel(pixels, position, scanlength, format, value);
                if (transparencyMask != null) {
                    setPackedBit(transparencyMask, position,
                            ((value >>> 24) & 0xff) != 0);
                }
            }
        }
    }

    public void drawTriangle(int x1, int y1, int x2, int y2,
            int x3, int y3, int argbColor) {
        if (((argbColor >>> 24) & 0xff) == 0) {
            return;
        }
        int oldColor = graphics.getColor();
        graphics.setColor(argbColor & 0x00ffffff);
        graphics.drawLine(x1, y1, x2, y2);
        graphics.drawLine(x2, y2, x3, y3);
        graphics.drawLine(x3, y3, x1, y1);
        graphics.setColor(oldColor);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2,
            int x3, int y3, int argbColor) {
        if (((argbColor >>> 24) & 0xff) == 0) {
            return;
        }
        int oldColor = graphics.getColor();
        graphics.setColor(argbColor & 0x00ffffff);
        graphics.fillTriangle(x1, y1, x2, y2, x3, y3);
        graphics.setColor(oldColor);
    }

    public void drawPolygon(int[] xPoints, int xOffset,
            int[] yPoints, int yOffset, int nPoints, int argbColor) {
        checkPolygon(xPoints, xOffset, yPoints, yOffset, nPoints);
        if (((argbColor >>> 24) & 0xff) == 0) {
            return;
        }
        int oldColor = graphics.getColor();
        graphics.setColor(argbColor & 0x00ffffff);
        for (int i = 0; i < nPoints; i++) {
            int next = i + 1 == nPoints ? 0 : i + 1;
            graphics.drawLine(xPoints[xOffset + i], yPoints[yOffset + i],
                    xPoints[xOffset + next], yPoints[yOffset + next]);
        }
        graphics.setColor(oldColor);
    }

    public void fillPolygon(int[] xPoints, int xOffset,
            int[] yPoints, int yOffset, int nPoints, int argbColor) {
        checkPolygon(xPoints, xOffset, yPoints, yOffset, nPoints);
        if (((argbColor >>> 24) & 0xff) == 0) {
            return;
        }
        if (nPoints == 3) {
            fillTriangle(xPoints[xOffset], yPoints[yOffset],
                    xPoints[xOffset + 1], yPoints[yOffset + 1],
                    xPoints[xOffset + 2], yPoints[yOffset + 2], argbColor);
            return;
        }

        int minimumY = yPoints[yOffset];
        int maximumY = minimumY;
        for (int i = 1; i < nPoints; i++) {
            int value = yPoints[yOffset + i];
            if (value < minimumY) minimumY = value;
            if (value > maximumY) maximumY = value;
        }

        int[] intersections = new int[nPoints];
        int oldColor = graphics.getColor();
        graphics.setColor(argbColor & 0x00ffffff);
        for (int y = minimumY; y <= maximumY; y++) {
            int count = 0;
            for (int i = 0; i < nPoints; i++) {
                int next = i + 1 == nPoints ? 0 : i + 1;
                int x1 = xPoints[xOffset + i];
                int y1 = yPoints[yOffset + i];
                int x2 = xPoints[xOffset + next];
                int y2 = yPoints[yOffset + next];
                if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                    intersections[count++] = x1 +
                            (int)(((long)(y - y1) * (long)(x2 - x1)) /
                            (long)(y2 - y1));
                }
            }
            insertionSort(intersections, count);
            for (int i = 0; i + 1 < count; i += 2) {
                graphics.drawLine(intersections[i], y, intersections[i + 1], y);
            }
        }
        graphics.setColor(oldColor);
    }

    private void drawARGB(int[] source, int width, int height,
            int x, int y, int anchor, int manipulation) {
        TransformedImage transformed = transform(source, width, height,
                manipulation);
        int drawX = anchorX(x, transformed.width, anchor);
        int drawY = anchorY(y, transformed.height, anchor);
        graphics.drawRGB(transformed.pixels, 0, transformed.width,
                drawX, drawY, transformed.width, transformed.height, true);
    }

    private int[] readARGB(int x, int y, int width, int height) {
        checkDimensions(width, height);
        int[] result = new int[width * height];
        nGetPixels(graphics, result, 0, width, x, y, width, height);
        return result;
    }

    private void synchronizeColor() {
        int current = graphics.getColor();
        if (current != lastRGB) {
            lastRGB = current;
            alpha = 255;
        }
    }

    private static int anchorX(int x, int width, int anchor) {
        int horizontal = anchor & (Graphics.LEFT | Graphics.RIGHT |
                Graphics.HCENTER);
        if (horizontal == 0 || horizontal == Graphics.LEFT) return x;
        if (horizontal == Graphics.RIGHT) return x - width;
        if (horizontal == Graphics.HCENTER) return x - width / 2;
        throw new IllegalArgumentException("Invalid horizontal anchor");
    }

    private static int anchorY(int y, int height, int anchor) {
        int vertical = anchor & (Graphics.TOP | Graphics.BOTTOM |
                Graphics.VCENTER | Graphics.BASELINE);
        if (vertical == 0 || vertical == Graphics.TOP) return y;
        if (vertical == Graphics.BOTTOM) return y - height;
        if (vertical == Graphics.VCENTER) return y - height / 2;
        throw new IllegalArgumentException("Invalid vertical anchor");
    }

    private static TransformedImage transform(int[] source, int width,
            int height, int manipulation) {
        int rotation = manipulation & ~(FLIP_HORIZONTAL | FLIP_VERTICAL);
        if (rotation != 0 && rotation != ROTATE_90 &&
                rotation != ROTATE_180 && rotation != ROTATE_270) {
            throw new IllegalArgumentException("Unsupported manipulation");
        }
        int outputWidth = rotation == ROTATE_90 || rotation == ROTATE_270
                ? height : width;
        int outputHeight = rotation == ROTATE_90 || rotation == ROTATE_270
                ? width : height;
        int[] result = new int[outputWidth * outputHeight];
        for (int sourceY = 0; sourceY < height; sourceY++) {
            for (int sourceX = 0; sourceX < width; sourceX++) {
                int targetX;
                int targetY;
                if (rotation == ROTATE_90) {
                    targetX = sourceY;
                    targetY = width - 1 - sourceX;
                } else if (rotation == ROTATE_180) {
                    targetX = width - 1 - sourceX;
                    targetY = height - 1 - sourceY;
                } else if (rotation == ROTATE_270) {
                    targetX = height - 1 - sourceY;
                    targetY = sourceX;
                } else {
                    targetX = sourceX;
                    targetY = sourceY;
                }
                if ((manipulation & FLIP_VERTICAL) != 0) {
                    targetY = outputHeight - 1 - targetY;
                }
                if ((manipulation & FLIP_HORIZONTAL) != 0) {
                    targetX = outputWidth - 1 - targetX;
                }
                result[targetY * outputWidth + targetX] =
                        source[sourceY * width + sourceX];
            }
        }
        return new TransformedImage(result, outputWidth, outputHeight);
    }

    private static int shortToARGB(int value, boolean transparency, int format) {
        int alphaValue = 255;
        int red;
        int green;
        int blue;
        if (format == TYPE_USHORT_4444_ARGB) {
            alphaValue = transparency ? expand4((value >>> 12) & 0xf) : 255;
            red = expand4((value >>> 8) & 0xf);
            green = expand4((value >>> 4) & 0xf);
            blue = expand4(value & 0xf);
        } else if (format == TYPE_USHORT_444_RGB) {
            red = expand4((value >>> 8) & 0xf);
            green = expand4((value >>> 4) & 0xf);
            blue = expand4(value & 0xf);
        } else if (format == TYPE_USHORT_1555_ARGB) {
            alphaValue = transparency && (value & 0x8000) == 0 ? 0 : 255;
            red = expand5((value >>> 10) & 0x1f);
            green = expand5((value >>> 5) & 0x1f);
            blue = expand5(value & 0x1f);
        } else if (format == TYPE_USHORT_555_RGB) {
            red = expand5((value >>> 10) & 0x1f);
            green = expand5((value >>> 5) & 0x1f);
            blue = expand5(value & 0x1f);
        } else {
            red = expand5((value >>> 11) & 0x1f);
            green = expand6((value >>> 5) & 0x3f);
            blue = expand5(value & 0x1f);
        }
        return (alphaValue << 24) | (red << 16) | (green << 8) | blue;
    }

    private static int argbToShort(int value, int format) {
        int alphaValue = (value >>> 24) & 0xff;
        int red = (value >>> 16) & 0xff;
        int green = (value >>> 8) & 0xff;
        int blue = value & 0xff;
        if (format == TYPE_USHORT_4444_ARGB) {
            return ((alphaValue >>> 4) << 12) | ((red >>> 4) << 8) |
                    ((green >>> 4) << 4) | (blue >>> 4);
        }
        if (format == TYPE_USHORT_444_RGB) {
            return ((red >>> 4) << 8) | ((green >>> 4) << 4) | (blue >>> 4);
        }
        if (format == TYPE_USHORT_1555_ARGB) {
            return (alphaValue == 0 ? 0 : 0x8000) | ((red >>> 3) << 10) |
                    ((green >>> 3) << 5) | (blue >>> 3);
        }
        if (format == TYPE_USHORT_555_RGB) {
            return ((red >>> 3) << 10) | ((green >>> 3) << 5) |
                    (blue >>> 3);
        }
        return ((red >>> 3) << 11) | ((green >>> 2) << 5) | (blue >>> 3);
    }

    private static int bytePixelToARGB(byte[] pixels, int position,
            int scanlength, int format) {
        int value;
        if (format == TYPE_BYTE_8_GRAY) {
            value = pixels[position] & 0xff;
            return 0xff000000 | value << 16 | value << 8 | value;
        }
        if (format == TYPE_BYTE_332_RGB) {
            value = pixels[position] & 0xff;
            int red = ((value >>> 5) & 7) * 255 / 7;
            int green = ((value >>> 2) & 7) * 255 / 7;
            int blue = (value & 3) * 255 / 3;
            return 0xff000000 | red << 16 | green << 8 | blue;
        }
        int grayLevel;
        if (format == TYPE_BYTE_1_GRAY_VERTICAL) {
            int row = position / scanlength;
            int column = position % scanlength;
            int byteIndex = (row / 8) * scanlength + column;
            int bit = row & 7;
            grayLevel = ((pixels[byteIndex] >>> bit) & 1) * 255;
        } else {
            int bits = format;
            int bitPosition = position * bits;
            int byteIndex = bitPosition >>> 3;
            int shift = 8 - bits - (bitPosition & 7);
            int mask = (1 << bits) - 1;
            grayLevel = ((pixels[byteIndex] >>> shift) & mask) * 255 / mask;
        }
        return 0xff000000 | grayLevel << 16 | grayLevel << 8 | grayLevel;
    }

    private static void setBytePixel(byte[] pixels, int position,
            int scanlength, int format, int argb) {
        int red = (argb >>> 16) & 0xff;
        int green = (argb >>> 8) & 0xff;
        int blue = argb & 0xff;
        if (format == TYPE_BYTE_8_GRAY) {
            pixels[position] = (byte)gray(red, green, blue);
            return;
        }
        if (format == TYPE_BYTE_332_RGB) {
            pixels[position] = (byte)(((red * 7 / 255) << 5) |
                    ((green * 7 / 255) << 2) | (blue * 3 / 255));
            return;
        }
        int grayValue = gray(red, green, blue);
        if (format == TYPE_BYTE_1_GRAY_VERTICAL) {
            int row = position / scanlength;
            int column = position % scanlength;
            int byteIndex = (row / 8) * scanlength + column;
            int bit = row & 7;
            if (grayValue >= 128) pixels[byteIndex] |= (byte)(1 << bit);
            else pixels[byteIndex] &= (byte)~(1 << bit);
            return;
        }
        int bits = format;
        int bitPosition = position * bits;
        int byteIndex = bitPosition >>> 3;
        int shift = 8 - bits - (bitPosition & 7);
        int mask = (1 << bits) - 1;
        int packed = grayValue * mask / 255;
        pixels[byteIndex] = (byte)((pixels[byteIndex] & ~(mask << shift)) |
                (packed << shift));
    }

    private static boolean getPackedBit(byte[] values, int position) {
        return ((values[position >>> 3] >>> (7 - (position & 7))) & 1) != 0;
    }

    private static void setPackedBit(byte[] values, int position,
            boolean enabled) {
        int index = position >>> 3;
        int mask = 1 << (7 - (position & 7));
        if (enabled) values[index] |= (byte)mask;
        else values[index] &= (byte)~mask;
    }

    private static int gray(int red, int green, int blue) {
        return (red * 30 + green * 59 + blue * 11) / 100;
    }

    private static int expand4(int value) {
        return value * 17;
    }

    private static int expand5(int value) {
        return (value << 3) | (value >>> 2);
    }

    private static int expand6(int value) {
        return (value << 2) | (value >>> 4);
    }

    private static void checkDimensions(int width, int height) {
        if (width < 0 || height < 0) {
            throw new IllegalArgumentException("Negative dimensions");
        }
    }

    private static void checkShortFormat(int format) {
        if (format != TYPE_USHORT_4444_ARGB &&
                format != TYPE_USHORT_444_RGB &&
                format != TYPE_USHORT_555_RGB &&
                format != TYPE_USHORT_1555_ARGB &&
                format != TYPE_USHORT_565_RGB) {
            throw new IllegalArgumentException("Unsupported short pixel format");
        }
    }

    private static void checkByteFormat(int format) {
        if (format != TYPE_BYTE_1_GRAY &&
                format != TYPE_BYTE_1_GRAY_VERTICAL &&
                format != TYPE_BYTE_2_GRAY &&
                format != TYPE_BYTE_4_GRAY &&
                format != TYPE_BYTE_8_GRAY &&
                format != TYPE_BYTE_332_RGB) {
            throw new IllegalArgumentException("Unsupported byte pixel format");
        }
    }

    private static void checkPolygon(int[] xPoints, int xOffset,
            int[] yPoints, int yOffset, int nPoints) {
        if (xPoints == null || yPoints == null) {
            throw new NullPointerException("polygon points are null");
        }
        if (nPoints < 3 || xOffset < 0 || yOffset < 0 ||
                xOffset + nPoints > xPoints.length ||
                yOffset + nPoints > yPoints.length) {
            throw new IllegalArgumentException("Invalid polygon");
        }
    }

    private static void insertionSort(int[] values, int count) {
        for (int i = 1; i < count; i++) {
            int value = values[i];
            int index = i - 1;
            while (index >= 0 && values[index] > value) {
                values[index + 1] = values[index];
                index--;
            }
            values[index + 1] = value;
        }
    }

    private static native void nGetPixels(Graphics graphics, int[] pixels,
            int offset, int scanlength, int x, int y, int width, int height);

    private static final class TransformedImage {
        final int[] pixels;
        final int width;
        final int height;

        TransformedImage(int[] pixels, int width, int height) {
            this.pixels = pixels;
            this.width = width;
            this.height = height;
        }
    }
}
