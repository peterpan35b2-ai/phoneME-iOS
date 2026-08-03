package javax.microedition.lcdui;

public abstract class Item {
    protected Item(String label) {
    }

    public String getLabel() { return null; }
    public void setLabel(String label) { }
    public int getLayout() { return 0; }
    public void setLayout(int layout) { }
    public void addCommand(Command command) { }
    public void removeCommand(Command command) { }
    public void setDefaultCommand(Command command) { }
    public void setItemCommandListener(ItemCommandListener listener) { }
    public int getPreferredWidth() { return 0; }
    public int getPreferredHeight() { return 0; }
    public void setPreferredSize(int width, int height) { }
    public int getMinimumWidth() { return 0; }
    public int getMinimumHeight() { return 0; }
    public void notifyStateChanged() { }
}
