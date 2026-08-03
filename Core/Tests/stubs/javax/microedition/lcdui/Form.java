package javax.microedition.lcdui;

public class Form extends Screen {
    public Form(String title) {
    }

    public Form(String title, Item[] items) {
    }

    public int append(Item item) { return 0; }
    public int append(String text) { return 0; }
    public int append(Image image) { return 0; }
    public void setItemStateListener(ItemStateListener listener) { }
    public void insert(int index, Item item) { }
    public void set(int index, Item item) { }
    public void delete(int index) { }
    public void deleteAll() { }
    public Item get(int index) { return null; }
    public int size() { return 0; }
}
