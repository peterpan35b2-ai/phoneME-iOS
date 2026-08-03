package javax.microedition.lcdui;

public class StringItem extends Item {
    public static final int PLAIN = 0;
    public static final int HYPERLINK = 1;
    public static final int BUTTON = 2;

    public StringItem(String label, String text) {
        super(label);
    }

    public StringItem(String label, String text, int appearanceMode) {
        super(label);
    }

    public String getText() { return null; }
    public void setText(String text) { }
    public int getAppearanceMode() { return 0; }
    public Font getFont() { return null; }
    public void setFont(Font font) { }
}
