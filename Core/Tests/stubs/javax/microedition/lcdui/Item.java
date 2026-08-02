package javax.microedition.lcdui;

public abstract class Item {
    protected Item(String label) {
    }

    public String getLabel() { return null; }
    public void setLabel(String label) { }
    public int getLayout() { return 0; }
    public void setLayout(int layout) { }
    public void setItemCommandListener(ItemCommandListener listener) { }
}
