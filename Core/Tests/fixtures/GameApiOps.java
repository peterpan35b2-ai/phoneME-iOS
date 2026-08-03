package corefixture;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.game.Layer;
import javax.microedition.lcdui.game.LayerManager;
import javax.microedition.lcdui.game.Sprite;
import javax.microedition.lcdui.game.TiledLayer;

public final class GameApiOps {
    private static final class ColorLayer extends Layer {
        private final int color;

        ColorLayer(int color) {
            super(1, 1);
            this.color = color;
        }

        public void paint(Graphics graphics) {
            graphics.setColor(color);
            graphics.fillRect(getX(), getY(), 1, 1);
        }
    }

    private static int pixel(Image image, int x, int y) {
        int[] value = new int[1];
        image.getRGB(value, 0, 1, x, y, 1, 1);
        return value[0];
    }

    public static int run() {
        int red = 0xFFFF0000;
        int green = 0xFF00FF00;
        int blue = 0xFF0000FF;
        int transparent = 0x00000000;
        Image sheet = Image.createRGBImage(new int[] {
            red, red, green, transparent,
            red, red, transparent, green
        }, 4, 2, true);

        Sprite sprite = new Sprite(sheet, 2, 2);
        if (sprite.getRawFrameCount() != 2 || sprite.getFrameSequenceLength() != 2) return 1;
        sprite.setFrameSequence(new int[] {1, 0, 1});
        if (sprite.getFrameSequenceLength() != 3 || sprite.getFrame() != 0) return 2;
        sprite.nextFrame();
        if (sprite.getFrame() != 1) return 3;
        sprite.prevFrame();
        if (sprite.getFrame() != 0) return 4;

        sprite.defineReferencePixel(1, 0);
        sprite.setRefPixelPosition(10, 20);
        if (sprite.getRefPixelX() != 10 || sprite.getRefPixelY() != 20) return 5;
        sprite.setTransform(Sprite.TRANS_ROT90);
        if (sprite.getRefPixelX() != 10 || sprite.getRefPixelY() != 20) return 6;
        if (sprite.getWidth() != 2 || sprite.getHeight() != 2) return 7;

        Sprite solid = new Sprite(Image.createRGBImage(new int[] {
            red, red, red, red
        }, 2, 2, true));
        solid.setPosition(sprite.getX(), sprite.getY());
        if (!sprite.collidesWith(solid, false)) return 8;
        sprite.setFrame(0);
        if (!sprite.collidesWith(solid, true)) return 9;
        solid.setVisible(false);
        if (sprite.collidesWith(solid, false)) return 10;
        solid.setVisible(true);
        solid.move(20, 0);
        if (sprite.collidesWith(solid, false)) return 11;

        Sprite sequenceResize = new Sprite(sheet, 2, 2);
        sequenceResize.setFrameSequence(new int[] {1, 0, 1});
        sequenceResize.setFrame(2);
        Image largerSheet = Image.createRGBImage(new int[] {
            red, red, green, green, blue, blue,
            red, red, green, green, blue, blue
        }, 6, 2, true);
        sequenceResize.setImage(largerSheet, 2, 2);
        if (sequenceResize.getRawFrameCount() != 3 ||
            sequenceResize.getFrameSequenceLength() != 3 ||
            sequenceResize.getFrame() != 2) return 12;
        sequenceResize.setImage(Image.createRGBImage(new int[] {
            red, red, red, red
        }, 2, 2, true), 2, 2);
        if (sequenceResize.getRawFrameCount() != 1 ||
            sequenceResize.getFrameSequenceLength() != 1 ||
            sequenceResize.getFrame() != 0) return 13;

        Sprite dimensionResize = new Sprite(Image.createRGBImage(new int[] {
            red, red, red, red
        }, 2, 2, true));
        dimensionResize.defineReferencePixel(1, 0);
        dimensionResize.setRefPixelPosition(30, 40);
        dimensionResize.setTransform(Sprite.TRANS_ROT90);
        dimensionResize.defineCollisionRectangle(-2, -2, 8, 8);
        dimensionResize.setImage(Image.createRGBImage(new int[] {
            red, red, red, red, red, red
        }, 3, 2, true), 3, 2);
        if (dimensionResize.getRefPixelX() != 30 ||
            dimensionResize.getRefPixelY() != 40 ||
            dimensionResize.getWidth() != 2 ||
            dimensionResize.getHeight() != 3) return 14;

        Image mutable = Image.createImage(1, 1);
        Graphics mutableGraphics = mutable.getGraphics();
        mutableGraphics.setColor(0xFF0000);
        mutableGraphics.fillRect(0, 0, 1, 1);
        Sprite original = new Sprite(mutable);
        Sprite copied = new Sprite(original);
        mutableGraphics.setColor(0x00FF00);
        mutableGraphics.fillRect(0, 0, 1, 1);
        Image copyTarget = Image.createImage(1, 1);
        copied.paint(copyTarget.getGraphics());
        if (pixel(copyTarget, 0, 0) != red) return 15;
        Image originalTarget = Image.createImage(1, 1);
        original.paint(originalTarget.getGraphics());
        if (pixel(originalTarget, 0, 0) != green) return 16;

        Image tiles = Image.createRGBImage(new int[] {red, green}, 2, 1, true);
        TiledLayer tiled = new TiledLayer(2, 2, tiles, 1, 1);
        if (tiled.getColumns() != 2 || tiled.getRows() != 2 ||
            tiled.getCellWidth() != 1 || tiled.getCellHeight() != 1) return 17;
        int animated = tiled.createAnimatedTile(2);
        if (animated != -1 || tiled.getAnimatedTile(animated) != 2) return 18;
        tiled.setCell(0, 0, animated);
        tiled.fillCells(1, 0, 1, 2, 1);
        if (tiled.getCell(0, 0) != animated || tiled.getCell(1, 1) != 1) return 19;
        tiled.setAnimatedTile(animated, 1);
        if (tiled.getAnimatedTile(animated) != 1) return 20;
        try {
            tiled.fillCells(0, 0, -1, 1, 0);
            return 21;
        } catch (IllegalArgumentException expected) {
        }

        tiled.setStaticTileSet(Image.createRGBImage(new int[] {
            red, green, blue
        }, 3, 1, true), 1, 1);
        if (tiled.getCell(0, 0) != animated ||
            tiled.getAnimatedTile(animated) != 1) return 22;
        tiled.setStaticTileSet(Image.createRGBImage(new int[] {red}, 1, 1, true), 1, 1);
        if (tiled.getCell(0, 0) != 0 || tiled.getCell(1, 1) != 0) return 23;
        try {
            tiled.getAnimatedTile(animated);
            return 24;
        } catch (IndexOutOfBoundsException expected) {
        }

        tiled.setCell(0, 0, 1);
        Sprite tileProbe = new Sprite(Image.createRGBImage(new int[] {red}, 1, 1, true));
        tileProbe.setPosition(0, 0);
        if (!tileProbe.collidesWith(tiled, false)) return 25;
        if (!tileProbe.collidesWith(tiled, true)) return 26;
        tiled.setVisible(false);
        if (tileProbe.collidesWith(tiled, false)) return 27;
        tiled.setVisible(true);
        tiled.setCell(0, 0, 0);
        if (tileProbe.collidesWith(tiled, false)) return 28;

        Image target = Image.createImage(4, 4);
        Graphics graphics = target.getGraphics();
        graphics.setColor(0x000000);
        graphics.fillRect(0, 0, 4, 4);

        Sprite redSprite = new Sprite(Image.createRGBImage(new int[] {red}, 1, 1, true));
        Sprite greenSprite = new Sprite(Image.createRGBImage(new int[] {green}, 1, 1, true));
        redSprite.setPosition(1, 1);
        greenSprite.setPosition(1, 1);
        LayerManager manager = new LayerManager();
        manager.append(redSprite);
        manager.append(greenSprite);
        if (manager.getSize() != 2 || manager.getLayerAt(0) != redSprite) return 29;
        manager.paint(graphics, 0, 0);
        if (pixel(target, 1, 1) != red) return 30;

        manager.insert(redSprite, 1);
        if (manager.getLayerAt(0) != greenSprite ||
            manager.getLayerAt(1) != redSprite) return 31;
        try {
            manager.insert(redSprite, 2);
            return 32;
        } catch (IndexOutOfBoundsException expected) {
        }
        graphics.setColor(0x000000);
        graphics.fillRect(0, 0, 4, 4);
        manager.paint(graphics, 0, 0);
        if (pixel(target, 1, 1) != green) return 33;

        manager.insert(greenSprite, 0);
        manager.remove(greenSprite);
        if (manager.getSize() != 1 || manager.getLayerAt(0) != redSprite) return 34;
        manager.setViewWindow(0, 0, 4, 4);

        graphics.translate(1, 1);
        graphics.setClip(0, 0, 2, 2);
        int beforeX = graphics.getTranslateX();
        int beforeY = graphics.getTranslateY();
        int beforeClipX = graphics.getClipX();
        int beforeClipY = graphics.getClipY();
        int beforeClipW = graphics.getClipWidth();
        int beforeClipH = graphics.getClipHeight();
        manager.paint(graphics, 0, 0);
        if (graphics.getTranslateX() != beforeX || graphics.getTranslateY() != beforeY) return 35;
        if (graphics.getClipX() != beforeClipX || graphics.getClipY() != beforeClipY ||
            graphics.getClipWidth() != beforeClipW || graphics.getClipHeight() != beforeClipH) return 36;

        Image colorTarget = Image.createImage(2, 2);
        Graphics colorGraphics = colorTarget.getGraphics();
        colorGraphics.setColor(0x0000FF);
        LayerManager colorManager = new LayerManager();
        colorManager.append(new ColorLayer(0xFF0000));
        colorManager.paint(colorGraphics, 0, 0);
        if (colorGraphics.getColor() != 0xFF0000 ||
            pixel(colorTarget, 0, 0) != red) return 37;

        return 0;
    }
}
