package javax.microedition.lcdui;

public class TextBox extends Screen {
    public TextBox(String title, String text, int maxSize, int constraints) {}

    public String getString() { return null; }
    public void setString(String text) {}
    public int getChars(char[] data) { return 0; }
    public void setChars(char[] data, int offset, int length) {}
    public void insert(String source, int position) {}
    public void insert(char[] data, int offset, int length, int position) {}
    public void delete(int offset, int length) {}
    public int getMaxSize() { return 0; }
    public int setMaxSize(int maxSize) { return maxSize; }
    public int size() { return 0; }
    public int getCaretPosition() { return 0; }
    public void setConstraints(int constraints) {}
    public int getConstraints() { return 0; }
    public void setInitialInputMode(String characterSubset) {}
}
