package corefixture;

import java.util.Date;
import java.util.TimeZone;
import javax.microedition.lcdui.Alert;
import javax.microedition.lcdui.AlertType;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.CommandListener;
import javax.microedition.lcdui.Choice;
import javax.microedition.lcdui.ChoiceGroup;
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.DateField;
import javax.microedition.lcdui.Displayable;
import javax.microedition.lcdui.Form;
import javax.microedition.lcdui.Gauge;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.ImageItem;
import javax.microedition.lcdui.Item;
import javax.microedition.lcdui.ItemCommandListener;
import javax.microedition.lcdui.ItemStateListener;
import javax.microedition.lcdui.List;
import javax.microedition.lcdui.Spacer;
import javax.microedition.lcdui.StringItem;
import javax.microedition.lcdui.TextBox;
import javax.microedition.lcdui.TextField;
import javax.microedition.midlet.MIDlet;

public final class LcduiApp extends MIDlet
        implements CommandListener, ItemCommandListener,
        ItemStateListener, Runnable {
    private static final class MenuList extends List {
        MenuList(String title, int type, String[] strings) {
            super(title, type, strings, null);
        }
    }

    private Display display;
    private Form form;
    private StringItem status;
    private ChoiceGroup modes;
    private DateField dateField;
    private Command menuCommand;
    private Command listItemCommand;
    private Command textCommand;
    private Command alertCommand;
    private int serialRuns;

    protected void startApp() {
        form = new Form("Tiêu đề");
        status = new StringItem("Trạng thái", "Ready");
        TextField name = new TextField("Tên", "Việt", 12, TextField.ANY);
        Gauge progress = new Gauge("Tiến độ", true, 10, 3);
        modes = new ChoiceGroup("Chế độ", Choice.MULTIPLE,
                new String[] {"Easy", "Hard"}, null);
        modes.setSelectedIndex(0, true);
        dateField = new DateField("Ngày", DateField.DATE_TIME,
                TimeZone.getTimeZone("GMT+07:00"));
        dateField.setDate(new Date(1700000000000L));
        Spacer spacer = new Spacer(12, 18);
        Image previewImage = Image.createImage(2, 3);
        Graphics previewGraphics = previewImage.getGraphics();
        previewGraphics.setColor(0x123456);
        previewGraphics.fillRect(0, 0, 2, 3);
        ImageItem preview = new ImageItem(
                "Preview", previewImage, 0, "preview");
        StringItem action = new StringItem("Action", "Tap",
                StringItem.BUTTON);
        Command itemCommand = new Command("Go", Command.ITEM, 1);
        action.setDefaultCommand(itemCommand);
        action.setItemCommandListener(this);

        form.append(status);
        form.append(name);
        form.append(progress);
        form.append(modes);
        form.append(dateField);
        form.append(spacer);
        form.append(preview);
        form.append(action);
        form.append("Tail");

        form.addCommand(new Command("OK", Command.OK, 1));
        form.addCommand(new Command("Back", "Go back", Command.BACK, 2));
        menuCommand = new Command("Menu", Command.SCREEN, 3);
        textCommand = new Command("Text", Command.SCREEN, 4);
        alertCommand = new Command("Alert", Command.SCREEN, 5);
        form.addCommand(menuCommand);
        form.addCommand(textCommand);
        form.addCommand(alertCommand);
        form.setCommandListener(this);
        form.setItemStateListener(this);

        display = Display.getDisplay(this);
        if (!display.isColor() || display.numColors() != 16777216
                || display.numAlphaLevels() != 256
                || display.getBestImageWidth(Display.ALERT) != 64
                || display.getBestImageHeight(Display.LIST_ELEMENT) != 32
                || display.vibrate(0) || display.flashBacklight(0)) {
            throw new IllegalStateException();
        }
        try {
            display.getBestImageWidth(99);
            throw new IllegalStateException();
        } catch (IllegalArgumentException expected) {
        }
        display.setCurrent(form);
        display.callSerially(this);

        status.setText("Running");
        name.setString("phoneME");
        progress.setValue(7);
        form.setTitle("Native Form");
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public void run() {
        serialRuns++;
        if (serialRuns < 64) {
            display.callSerially(this);
        } else {
            status.setText("Serial");
        }
    }

    public void commandAction(Command command, Item item) {
        status.setText("Item " + command.getLabel());
    }

    public void itemStateChanged(Item item) {
        status.setText("State " + item.getLabel());
    }

    public void commandAction(Command command, Displayable displayable) {
        if (displayable instanceof List) {
            List list = (List) displayable;
            if (command == listItemCommand) {
                status.setText("List item " + list.getSelectedIndex());
            } else {
                status.setText(command == List.SELECT_COMMAND
                        ? "List " + list.getSelectedIndex()
                        : "Wrong list command");
            }
            display.callSerially(new Runnable() {
                public void run() {
                    display.setCurrent(form);
                }
            });
            return;
        }
        if (displayable instanceof TextBox) {
            TextBox textBox = (TextBox) displayable;
            status.setText("Text " + textBox.getString() + " @"
                    + (dateField.getDate().getTime() / 1000L));
            display.setCurrent(form);
            return;
        }
        if (displayable instanceof Alert) {
            status.setText(command == Alert.DISMISS_COMMAND
                    ? "Alert dismissed" : "Wrong alert command");
            display.setCurrent(form);
            return;
        }
        if (command == menuCommand) {
            List list = new MenuList("Menu", Choice.IMPLICIT,
                    new String[] {"One", "Two"});
            listItemCommand = new Command("Use", Command.ITEM, 1);
            list.addCommand(listItemCommand);
            list.setCommandListener(this);
            display.setCurrent(list);
            return;
        }
        if (command == textCommand) {
            TextBox textBox = new TextBox("Nhập", "abc", 20,
                    TextField.ANY);
            textBox.addCommand(new Command("Done", Command.OK, 1));
            textBox.setCommandListener(this);
            display.setCurrent(textBox);
            return;
        }
        if (command == alertCommand) {
            Alert alert = new Alert("Cảnh báo", "Nội dung Việt", null,
                    AlertType.WARNING);
            alert.setTimeout(Alert.FOREVER);
            alert.setCommandListener(this);
            display.setCurrent(alert, form);
            return;
        }
        status.setText(modes.isSelected(1) ? "Hard selected" : "Accepted");
    }
}
