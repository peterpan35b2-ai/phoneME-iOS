package javax.microedition.lcdui;

public abstract class Displayable {
    protected Displayable() {
    }

    public String getTitle() { return null; }
    public void setTitle(String title) { }
    public void addCommand(Command command) { }
    public void removeCommand(Command command) { }
    public void setCommandListener(CommandListener listener) { }
    public void setTicker(Ticker ticker) { }
    public Ticker getTicker() { return null; }
    public boolean isShown() { return false; }
}
