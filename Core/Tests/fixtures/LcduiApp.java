package corefixture;

import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Choice;
import javax.microedition.lcdui.ChoiceGroup;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Form;
import javax.microedition.lcdui.Gauge;
import javax.microedition.lcdui.List;
import javax.microedition.lcdui.StringItem;
import javax.microedition.lcdui.TextField;
import javax.microedition.midlet.MIDlet;

public final class LcduiApp extends MIDlet implements CommandListener {
    private Display display;
    private Form form;
    private StringItem status;
    private ChoiceGroup modes;
    private Command menuCommand;

    protected void startApp() {
        form = new Form("Tiêu đề");
        status = new StringItem("Trạng thái", "Ready");
        TextField name = new TextField("Tên", "Việt", 12, TextField.ANY);
        Gauge progress = new Gauge("Tiến độ", true, 10, 3);
        modes = new ChoiceGroup("Chế độ", Choice.MULTIPLE,
                new String[] {"Easy", "Hard"}, null);
        modes.setSelectedIndex(0, true);

        form.append(status);
        form.append(name);
        form.append(progress);
        form.append(modes);
        form.append("Tail");

        form.addCommand(new Command("OK", Command.OK, 1));
        form.addCommand(new Command("Back", "Go back", Command.BACK, 2));
        menuCommand = new Command("Menu", Command.SCREEN, 3);
        form.addCommand(menuCommand);
        form.setCommandListener(this);

        display = Display.getDisplay(this);
        display.setCurrent(form);

        status.setText("Running");
        name.setString("phoneME");
        progress.setValue(7);
        form.setTitle("Native Form");
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void commandAction(Command command, Displayable displayable) {
        if (displayable instanceof List) {
            List list = (List) displayable;
            status.setText(command == List.SELECT_COMMAND
                    ? "List " + list.getSelectedIndex()
                    : "Wrong list command");
            display.setCurrent(form);
            return;
        }
        if (command == menuCommand) {
            List list = new List("Menu", Choice.IMPLICIT,
                    new String[] {"One", "Two"}, null);
            list.setCommandListener(this);
            display.setCurrent(list);
            return;
        }
        status.setText(modes.isSelected(1) ? "Hard selected" : "Accepted");
    }
}
