package javax.microedition.lcdui;

public class ChoiceGroup extends Item implements Choice {
    public ChoiceGroup(String label, int choiceType) {
        super(label);
    }

    public ChoiceGroup(String label, int choiceType,
                       String[] strings, Image[] images) {
        super(label);
    }

    public int size() { return 0; }
    public String getString(int index) { return null; }
    public Image getImage(int index) { return null; }
    public int append(String text, Image image) { return 0; }
    public void insert(int index, String text, Image image) { }
    public void delete(int index) { }
    public void deleteAll() { }
    public void set(int index, String text, Image image) { }
    public boolean isSelected(int index) { return false; }
    public int getSelectedIndex() { return 0; }
    public int getSelectedFlags(boolean[] flags) { return 0; }
    public void setSelectedIndex(int index, boolean selected) { }
    public void setSelectedFlags(boolean[] flags) { }
    public void setFitPolicy(int policy) { }
    public int getFitPolicy() { return 0; }
    public void setFont(int index, Font font) { }
    public Font getFont(int index) { return null; }
}
