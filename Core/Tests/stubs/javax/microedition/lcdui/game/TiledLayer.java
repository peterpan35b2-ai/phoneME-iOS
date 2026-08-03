package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public class TiledLayer extends Layer {
    public TiledLayer(int columns, int rows, Image image, int tileWidth, int tileHeight) {
        super(1, 1);
    }
    public int getColumns() { return 0; }
    public int getRows() { return 0; }
    public int getCellWidth() { return 0; }
    public int getCellHeight() { return 0; }
    public int getCell(int column, int row) { return 0; }
    public void setCell(int column, int row, int tileIndex) { }
    public void fillCells(int column, int row, int columns, int rows, int tileIndex) { }
    public int createAnimatedTile(int staticTileIndex) { return 0; }
    public int getAnimatedTile(int animatedTileIndex) { return 0; }
    public void setAnimatedTile(int animatedTileIndex, int staticTileIndex) { }
    public void setStaticTileSet(Image image, int tileWidth, int tileHeight) { }
    public void paint(Graphics graphics) { }
}
