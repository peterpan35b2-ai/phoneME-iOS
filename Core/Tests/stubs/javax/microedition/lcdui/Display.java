package javax.microedition.lcdui;

import javax.microedition.midlet.MIDlet;

public final class Display {
    public static final int LIST_ELEMENT = 1;
    public static final int CHOICE_GROUP_ELEMENT = 2;
    public static final int ALERT = 3;
    public static final int TAB = 4;
    public static final int COLOR_BACKGROUND = 0;
    public static final int COLOR_FOREGROUND = 1;
    public static final int COLOR_HIGHLIGHTED_BACKGROUND = 2;
    public static final int COLOR_HIGHLIGHTED_FOREGROUND = 3;
    public static final int COLOR_BORDER = 4;
    public static final int COLOR_HIGHLIGHTED_BORDER = 5;

    private Display() {
    }

    public static Display getDisplay(MIDlet midlet) { return null; }
    public Displayable getCurrent() { return null; }
    public void setCurrent(Displayable displayable) { }
    public void setCurrent(Alert alert, Displayable nextDisplayable) { }
    public void setCurrentItem(Item item) { }
    public int getColor(int colorSpecifier) { return 0; }
    public int getBorderStyle(boolean highlighted) { return 0; }
    public boolean isColor() { return true; }
    public int numColors() { return 0; }
    public int numAlphaLevels() { return 0; }
    public void callSerially(Runnable runnable) { }
    public boolean flashBacklight(int duration) { return false; }
    public boolean vibrate(int duration) { return false; }
    public int getBestImageWidth(int imageType) { return 0; }
    public int getBestImageHeight(int imageType) { return 0; }
}
