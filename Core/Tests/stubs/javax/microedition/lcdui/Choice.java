package javax.microedition.lcdui;

public interface Choice {
    int EXCLUSIVE = 1;
    int MULTIPLE = 2;
    int IMPLICIT = 3;
    int POPUP = 4;
    int TEXT_WRAP_DEFAULT = 0;
    int TEXT_WRAP_ON = 1;
    int TEXT_WRAP_OFF = 2;

    int size();
    String getString(int index);
    Image getImage(int index);
    int append(String text, Image image);
    void insert(int index, String text, Image image);
    void delete(int index);
    void deleteAll();
    void set(int index, String text, Image image);
    boolean isSelected(int index);
    int getSelectedIndex();
    int getSelectedFlags(boolean[] flags);
    void setSelectedIndex(int index, boolean selected);
    void setSelectedFlags(boolean[] flags);
    void setFitPolicy(int policy);
    int getFitPolicy();
    void setFont(int index, Font font);
    Font getFont(int index);
}
