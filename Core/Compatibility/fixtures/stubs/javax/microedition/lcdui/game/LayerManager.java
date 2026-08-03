package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;

public class LayerManager {
    public LayerManager() { }
    public void append(Layer layer) { }
    public void insert(Layer layer, int index) { }
    public void remove(Layer layer) { }
    public Layer getLayerAt(int index) { return null; }
    public int getSize() { return 0; }
    public void setViewWindow(int x, int y, int width, int height) { }
    public void paint(Graphics graphics, int x, int y) { }
}
