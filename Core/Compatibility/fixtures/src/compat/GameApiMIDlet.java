package compat;

import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.game.GameCanvas;
import javax.microedition.lcdui.game.LayerManager;
import javax.microedition.lcdui.game.Sprite;
import javax.microedition.lcdui.game.TiledLayer;
import javax.microedition.midlet.MIDlet;

public final class GameApiMIDlet extends MIDlet {
    private final Scene scene = new Scene();

    protected void startApp() {
        Display.getDisplay(this).setCurrent(scene);
        scene.repaint();
        System.out.println("COMPAT_MILESTONE:gameapi-start");
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
        System.out.println("COMPAT_MILESTONE:gameapi-destroy");
    }

    private static final class Scene extends GameCanvas {
        private final LayerManager layers = new LayerManager();

        Scene() {
            super(false);
            Image spriteImage = Image.createRGBImage(
                    new int[] {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff},
                    2, 2, true);
            Sprite sprite = new Sprite(spriteImage, 2, 2);
            sprite.setPosition(20, 20);
            sprite.defineCollisionRectangle(0, 0, 2, 2);
            Image tileImage = Image.createRGBImage(
                    new int[] {0xff222222, 0xff444444, 0xff666666, 0xff888888},
                    2, 2, true);
            TiledLayer tiles = new TiledLayer(2, 2, tileImage, 1, 1);
            tiles.fillCells(0, 0, 2, 2, 1);
            layers.append(tiles);
            layers.append(sprite);
            layers.setViewWindow(0, 0, 64, 64);
        }

        protected void paint(Graphics graphics) {
            graphics.setColor(0x001122);
            graphics.fillRect(0, 0, getWidth(), getHeight());
            layers.paint(graphics, 0, 0);
            System.out.println("COMPAT_MILESTONE:gameapi-paint");
        }
    }
}
