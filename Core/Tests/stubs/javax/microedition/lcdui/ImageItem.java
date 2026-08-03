package javax.microedition.lcdui;

public class ImageItem extends Item {
    public ImageItem(String label, Image image, int layout, String altText) {
        super(label);
    }

    public ImageItem(String label, Image image, int layout, String altText,
                     int appearanceMode) {
        super(label);
    }

    public Image getImage() { return null; }
    public void setImage(Image image) {}
    public String getAltText() { return null; }
    public void setAltText(String altText) {}
    public int getAppearanceMode() { return 0; }
}
