package javax.microedition.lcdui;

public class Spacer extends Item {
    public Spacer(int minimumWidth, int minimumHeight) {
        super(null);
    }

    public void setMinimumSize(int minimumWidth, int minimumHeight) {}
    public int getMinimumWidth() { return 0; }
    public int getMinimumHeight() { return 0; }
}
