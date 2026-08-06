package corefixture;

import java.io.ByteArrayInputStream;

import com.nokia.mid.ui.DeviceControl;
import com.nokia.mid.ui.DirectGraphics;
import com.nokia.mid.ui.DirectUtils;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public final class GraphicsOps {
    private static final int TRANS_NONE = 0;
    private static final int TRANS_ROT90 = 5;
    private static int nokiaStage;

    private GraphicsOps() {
    }

    private static boolean testGetRgbRules(Image source) {
        int[] reversedRows = new int[4];
        source.getRGB(reversedRows, 2, -2, 0, 0, 2, 2);
        if (reversedRows[0] != 0xFF0000FF ||
                reversedRows[1] != 0xFFFFFFFF ||
                reversedRows[2] != 0xFFFF0000 ||
                reversedRows[3] != 0xFF00FF00) {
            return false;
        }

        boolean overlapRejected = false;
        try {
            source.getRGB(new int[4], 0, 1, 0, 0, 2, 2);
        } catch (IllegalArgumentException expected) {
            overlapRejected = true;
        }
        if (!overlapRejected) {
            return false;
        }

        int[] sentinel = new int[] {0x12345678};
        source.getRGB(sentinel, 0, 0, 0, 0, 0, 2);
        source.getRGB(sentinel, 0, 0, 1, 0, -1, 1);
        if (sentinel[0] != 0x12345678) {
            return false;
        }

        // phoneME checks negative dimensions before dereferencing rgbData.
        source.getRGB(null, 0, 0, 1, 0, -1, 1);

        boolean zeroWidthNullRejected = false;
        try {
            source.getRGB(null, 0, 0, 0, 0, 0, 1);
        } catch (NullPointerException expected) {
            zeroWidthNullRejected = true;
        }
        return zeroWidthNullRejected;
    }

    private static boolean testGraphicsRules(Image canvas,
                                             Graphics graphics) {
        boolean selfRegionRejected = false;
        try {
            graphics.drawRegion(canvas, 0, 0, 1, 1, TRANS_NONE,
                                0, 0, Graphics.LEFT | Graphics.TOP);
        } catch (IllegalArgumentException expected) {
            selfRegionRejected = true;
        }
        if (!selfRegionRejected) {
            return false;
        }

        boolean selfImageRejected = false;
        try {
            graphics.drawImage(canvas, 0, 0,
                               Graphics.LEFT | Graphics.TOP);
        } catch (IllegalArgumentException expected) {
            selfImageRejected = true;
        }
        if (!selfImageRejected) {
            return false;
        }

        Image source = Image.createRGBImage(
                new int[] {0xFFFF0000}, 1, 1, true);
        graphics.drawRegion(source, 1, 1, 0, 0, TRANS_NONE,
                            0, 0, Graphics.LEFT | Graphics.TOP);
        graphics.drawRGB(new int[] {0xFFFF0000, 0xFF00FF00},
                         0, 0, 0, 0, 2, 2, true);
        graphics.drawRGB(new int[] {
                             0xFFFF0000, 0xFF00FF00, 0xFF0000FF
                         },
                         2, 1, 0, 0, -1, 1, true);

        boolean zeroSizeBoundsChecked = false;
        try {
            graphics.drawRGB(new int[0], 0, 0,
                             0, 0, 0, 0, true);
        } catch (ArrayIndexOutOfBoundsException expected) {
            zeroSizeBoundsChecked = true;
        }
        if (!zeroSizeBoundsChecked) {
            return false;
        }

        try {
            graphics.drawRGB(new int[] {0xFFFFFFFF}, 0, 1,
                             0, 0, 2, 1, true);
            return false;
        } catch (ArrayIndexOutOfBoundsException expected) {
            return true;
        }
    }

    private static boolean testNokiaBytePixels() {
        nokiaStage = 1;
        Image grayImage = Image.createImage(2, 2);
        DirectGraphics gray = DirectUtils.getDirectGraphics(
                grayImage.getGraphics());
        byte[] grayPixels = new byte[] {0, 85, (byte)170, (byte)255};
        gray.drawPixels(grayPixels, new byte[] {(byte)0xB0},
                0, 2, 0, 0, 2, 2, 0, DirectGraphics.TYPE_BYTE_8_GRAY);
        int[] argb = new int[4];
        grayImage.getRGB(argb, 0, 2, 0, 0, 2, 2);
        if (argb[0] != 0xFF000000) { nokiaStage = 11; return false; }
        if (argb[1] != 0xFFFFFFFF) { nokiaStage = 12; return false; }
        int grayRed = (argb[2] >>> 16) & 0xFF;
        int grayGreen = (argb[2] >>> 8) & 0xFF;
        int grayValue = argb[2] & 0xFF;
        if (grayRed < 166 || grayRed > 176 ||
                grayGreen < 166 || grayGreen > 176 ||
                grayValue < 166 || grayValue > 176) {
            nokiaStage = 13;
            return false;
        }
        if (argb[3] != 0xFFFFFFFF) { nokiaStage = 14; return false; }
        nokiaStage = 2;
        byte[] grayOut = new byte[4];
        byte[] grayMask = new byte[1];
        gray.getPixels(grayOut, grayMask, 0, 2, 0, 0, 2, 2,
                DirectGraphics.TYPE_BYTE_8_GRAY);
        int grayOutValue = grayOut[2] & 0xFF;
        if ((grayOut[0] & 0xFF) != 0 || (grayOut[1] & 0xFF) != 255 ||
                grayOutValue < 166 || grayOutValue > 176 ||
                (grayOut[3] & 0xFF) != 255 ||
                (grayMask[0] & 0xF0) != 0xF0) {
            return false;
        }

        nokiaStage = 3;
        Image packedImage = Image.createImage(4, 1);
        DirectGraphics packed = DirectUtils.getDirectGraphics(
                packedImage.getGraphics());
        packed.drawPixels(new byte[] {(byte)0x1B}, null,
                0, 4, 0, 0, 4, 1, 0, DirectGraphics.TYPE_BYTE_2_GRAY);
        byte[] packedOut = new byte[1];
        packed.getPixels(packedOut, null, 0, 4, 0, 0, 4, 1,
                DirectGraphics.TYPE_BYTE_2_GRAY);
        if ((packedOut[0] & 0xFF) != 0x1B) return false;

        nokiaStage = 4;
        Image rgbImage = Image.createImage(1, 1);
        DirectGraphics rgb = DirectUtils.getDirectGraphics(
                rgbImage.getGraphics());
        rgb.drawPixels(new byte[] {(byte)0xE3}, null,
                0, 1, 0, 0, 1, 1, 0, DirectGraphics.TYPE_BYTE_332_RGB);
        byte[] rgbOut = new byte[1];
        rgb.getPixels(rgbOut, null, 0, 1, 0, 0, 1, 1,
                DirectGraphics.TYPE_BYTE_332_RGB);
        if ((rgbOut[0] & 0xFF) != 0xE3) return false;

        nokiaStage = 5;
        Image verticalImage = Image.createImage(2, 8);
        DirectGraphics vertical = DirectUtils.getDirectGraphics(
                verticalImage.getGraphics());
        vertical.drawPixels(new byte[] {0x55, (byte)0xAA}, null,
                0, 2, 0, 0, 2, 8, 0,
                DirectGraphics.TYPE_BYTE_1_GRAY_VERTICAL);
        byte[] verticalOut = new byte[2];
        vertical.getPixels(verticalOut, null, 0, 2, 0, 0, 2, 8,
                DirectGraphics.TYPE_BYTE_1_GRAY_VERTICAL);
        if ((verticalOut[0] & 0xFF) != 0x55 ||
                (verticalOut[1] & 0xFF) != 0xAA) return false;

        nokiaStage = 6;
        try {
            gray.drawPixels(new byte[1], null, 0, 1, 0, 0, 1, 1, 0, 3);
            return false;
        } catch (IllegalArgumentException expected) {
        }
        nokiaStage = 7;
        try {
            DeviceControl.flashLights(-1L);
            return false;
        } catch (IllegalArgumentException expected) {
        }
        try {
            DeviceControl.startVibra(101, 1L);
            return false;
        } catch (IllegalArgumentException expected) {
        }
        DeviceControl.flashLights(0L);
        DeviceControl.startVibra(0, 0L);
        DeviceControl.stopVibra();
        return true;
    }

    private static boolean testUnicodeText() {
        Image textCanvas = Image.createImage(192, 32);
        Graphics textGraphics = textCanvas.getGraphics();
        textGraphics.setColor(0x000000);
        Font font = Font.getDefaultFont();
        textGraphics.setFont(font);
        String text = "Tiếng Việt 日本語";
        if (font.stringWidth(text) <= 0) {
            return false;
        }
        textGraphics.drawString(text, 2, 2, Graphics.LEFT | Graphics.TOP);
        int[] textPixels = new int[192 * 32];
        textCanvas.getRGB(textPixels, 0, 192, 0, 0, 192, 32);
        int changedTextPixels = 0;
        for (int i = 0; i < textPixels.length; i++) {
            if (textPixels[i] != 0xFFFFFFFF) {
                changedTextPixels++;
            }
        }
        return changedTextPixels >= 20;
    }

    public static int run() {
        Image canvas = Image.createImage(8, 8);
        if (!canvas.isMutable() || canvas.getWidth() != 8 ||
                canvas.getHeight() != 8) {
            return 1;
        }

        Graphics graphics = canvas.getGraphics();
        graphics.setColor(0x112233);
        graphics.fillRect(0, 0, 8, 8);
        if (graphics.getColor() != 0x112233 ||
                graphics.getRedComponent() != 0x11 ||
                graphics.getGreenComponent() != 0x22 ||
                graphics.getBlueComponent() != 0x33) {
            return 2;
        }
        if (graphics.getDisplayColor(0x112233) != 0x102031 ||
                graphics.getDisplayColor(0xAA112233) != 0x102031) {
            return 18;
        }

        graphics.setClip(1, 1, 4, 4);
        graphics.translate(1, 1);
        if (graphics.getClipX() != 0 || graphics.getClipY() != 0 ||
                graphics.getClipWidth() != 4 ||
                graphics.getClipHeight() != 4 ||
                graphics.getTranslateX() != 1 ||
                graphics.getTranslateY() != 1) {
            return 3;
        }
        graphics.setColor(255, 0, 0);
        graphics.fillRect(0, 0, 4, 4);
        graphics.translate(-1, -1);
        graphics.setClip(0, 0, 8, 8);

        int[] canvasPixels = new int[64];
        canvas.getRGB(canvasPixels, 0, 8, 0, 0, 8, 8);
        if (canvasPixels[0] != 0xFF102031 ||
                canvasPixels[9] != 0xFFFF0000 ||
                canvasPixels[36] != 0xFFFF0000 ||
                canvasPixels[45] != 0xFF102031) {
            return 4;
        }

        Image alphaCanvas = Image.createImage(1, 1);
        Graphics alphaGraphics = alphaCanvas.getGraphics();
        alphaGraphics.drawRGB(new int[] {0x800000FF}, 0, 1,
                              0, 0, 1, 1, true);
        int[] alphaPixel = new int[1];
        alphaCanvas.getRGB(alphaPixel, 0, 1, 0, 0, 1, 1);
        if (alphaPixel[0] != 0xFF7B7DFF) {
            return 5;
        }

        int[] sourcePixels = new int[] {
            0xFFFF0000, 0xFF00FF00,
            0xFF0000FF, 0xFFFFFFFF
        };
        Image source = Image.createRGBImage(sourcePixels, 2, 2, true);
        if (source.isMutable()) {
            return 6;
        }
        if (!testGetRgbRules(source)) {
            return 14;
        }
        Image copy = Image.createImage(source);
        if (copy.isMutable() || copy.getWidth() != 2 || copy.getHeight() != 2) {
            return 7;
        }
        Image rotated = Image.createImage(source, 0, 0, 2, 2, TRANS_ROT90);
        int[] rotatedPixels = new int[4];
        rotated.getRGB(rotatedPixels, 0, 2, 0, 0, 2, 2);
        if (rotatedPixels[0] != 0xFF0000FF ||
                rotatedPixels[1] != 0xFFFF0000 ||
                rotatedPixels[2] != 0xFFFFFFFF ||
                rotatedPixels[3] != 0xFF00FF00) {
            return 8;
        }

        graphics.drawImage(Image.createRGBImage(
                new int[] {0xFF00FF00}, 1, 1, true),
                8, 8, Graphics.RIGHT | Graphics.BOTTOM);
        graphics.drawRegion(source, 0, 0, 2, 2, TRANS_NONE,
                            0, 6, Graphics.LEFT | Graphics.TOP);
        graphics.copyArea(0, 6, 2, 2, 2, 6,
                          Graphics.LEFT | Graphics.TOP);
        canvas.getRGB(canvasPixels, 0, 8, 0, 0, 8, 8);
        if (canvasPixels[63] != 0xFF00FF00 ||
                canvasPixels[48] != 0xFFFF0000 ||
                canvasPixels[50] != 0xFFFF0000) {
            return 9;
        }

        graphics.setStrokeStyle(Graphics.DOTTED);
        if (graphics.getStrokeStyle() != Graphics.DOTTED) {
            return 10;
        }
        graphics.setColor(0xFFFFFF);
        graphics.drawLine(0, 5, 7, 5);
        graphics.drawRect(0, 0, 7, 7);
        graphics.drawRoundRect(1, 1, 5, 5, 2, 2);
        graphics.fillRoundRect(2, 2, 3, 3, 2, 2);
        graphics.drawArc(0, 0, 6, 6, 0, 180);
        graphics.fillArc(1, 1, 4, 4, 180, 90);
        graphics.fillTriangle(0, 7, 3, 3, 6, 7);

        Font font = Font.getFont(Font.FACE_MONOSPACE,
                Font.STYLE_BOLD | Font.STYLE_UNDERLINED,
                Font.SIZE_SMALL);
        if (!font.isBold() || !font.isUnderlined() || font.isItalic() ||
                font.isPlain() || font.getFace() != Font.FACE_MONOSPACE ||
                font.getSize() != Font.SIZE_SMALL || font.getHeight() <= 0 ||
                font.getBaselinePosition() <= 0 || font.charWidth('W') <= 0 ||
                font.stringWidth("AB") < font.charWidth('A') ||
                font.substringWidth("ABCDE", 1, 2) <= 0) {
            return 11;
        }
        graphics.setFont(font);
        graphics.drawString("AB", 4, 4,
                            Graphics.HCENTER | Graphics.BASELINE);
        graphics.drawSubstring("ABCDE", 1, 2, 0, 0,
                               Graphics.LEFT | Graphics.TOP);
        graphics.drawChar('Z', 7, 0, Graphics.RIGHT | Graphics.TOP);
        graphics.drawChars(new char[] {'O', 'K'}, 0, 2,
                           0, 7, Graphics.LEFT | Graphics.BOTTOM);

        if (!testUnicodeText()) {
            return 12;
        }
        if (!testGraphicsRules(canvas, graphics)) {
            return 15;
        }
        if (!testNokiaBytePixels()) {
            return 180 + nokiaStage;
        }

        byte[] png = new byte[] {
            -119, 80, 78, 71, 13, 10, 26, 10,
            0, 0, 0, 13, 73, 72, 68, 82,
            0, 0, 0, 2, 0, 0, 0, 1,
            8, 6, 0, 0, 0, -12, 34, 127, -118,
            0, 0, 0, 14, 73, 68, 65, 84,
            120, -100, 99, -8, -49, -64, 0, 66,
            13, 0, 15, 122, 3, 126, 119, -23,
            127, -105, 0, 0, 0, 0, 73, 69,
            78, 68, -82, 66, 96, -126
        };
        Image decoded = Image.createImage(png, 0, png.length);
        int[] decodedPixels = new int[2];
        decoded.getRGB(decodedPixels, 0, 2, 0, 0, 2, 1);
        if (decoded.isMutable() || decoded.getWidth() != 2 ||
                decoded.getHeight() != 1 ||
                decodedPixels[0] != 0xFFFF0000 ||
                decodedPixels[1] != 0x800000FF) {
            return 13;
        }
        try {
            Image streamed = Image.createImage(new ByteArrayInputStream(png));
            int[] streamedPixels = new int[2];
            streamed.getRGB(streamedPixels, 0, 2, 0, 0, 2, 1);
            if (streamed.isMutable() || streamed.getWidth() != 2 ||
                    streamed.getHeight() != 1 ||
                    streamedPixels[0] != 0xFFFF0000 ||
                    streamedPixels[1] != 0x800000FF) {
                return 16;
            }
        } catch (java.io.IOException exception) {
            return 17;
        }
        return 0;
    }
}
