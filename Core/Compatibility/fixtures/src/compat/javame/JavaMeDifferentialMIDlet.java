package compat.javame;

import java.util.Date;
import java.util.TimeZone;
import javax.microedition.lcdui.Choice;
import javax.microedition.lcdui.ChoiceGroup;
import javax.microedition.lcdui.Command;
import javax.microedition.lcdui.DateField;
import javax.microedition.lcdui.Font;
import javax.microedition.lcdui.Form;
import javax.microedition.lcdui.Gauge;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.Item;
import javax.microedition.lcdui.List;
import javax.microedition.lcdui.Spacer;
import javax.microedition.lcdui.StringItem;
import javax.microedition.lcdui.TextField;
import javax.microedition.lcdui.Ticker;
import javax.microedition.lcdui.game.Sprite;
import javax.microedition.lcdui.game.TiledLayer;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.RecordEnumeration;
import javax.microedition.rms.RecordStore;

public final class JavaMeDifferentialMIDlet extends MIDlet {
    private interface IntCall {
        int run() throws Throwable;
    }

    private interface LongCall {
        long run() throws Throwable;
    }

    private static int checksum(int result, int value) {
        return result * 31 + value;
    }

    private static int checksum(int result, String value) {
        return checksum(result, value == null ? 0 : value.hashCode());
    }

    private static void emitInt(String id, IntCall call) {
        try {
            System.out.println("JME_DIFF\t" + id + "\tI\t" + call.run());
        } catch (Throwable error) {
            System.out.println("JME_DIFF\t" + id + "\tE\t"
                    + error.getClass().getName());
        }
    }

    private static void emitLong(String id, LongCall call) {
        try {
            System.out.println("JME_DIFF\t" + id + "\tJ\t" + call.run());
        } catch (Throwable error) {
            System.out.println("JME_DIFF\t" + id + "\tE\t"
                    + error.getClass().getName());
        }
    }

    protected void startApp() {
        emitInt("command", new IntCall() {
            public int run() { return commandSemantics(); }
        });
        emitInt("form-items", new IntCall() {
            public int run() { return formItemSemantics(); }
        });
        emitInt("choice-group", new IntCall() {
            public int run() { return choiceGroupSemantics(); }
        });
        emitInt("list", new IntCall() {
            public int run() { return listSemantics(); }
        });
        emitInt("text-field", new IntCall() {
            public int run() { return textFieldSemantics(); }
        });
        emitInt("gauge-date-spacer", new IntCall() {
            public int run() { return gaugeDateSpacerSemantics(); }
        });
        emitInt("font", new IntCall() {
            public int run() { return fontSemantics(); }
        });
        emitInt("image-graphics", new IntCall() {
            public int run() { return imageGraphicsSemantics(); }
        });
        emitInt("sprite", new IntCall() {
            public int run() { return spriteSemantics(); }
        });
        emitInt("tiled-layer", new IntCall() {
            public int run() { return tiledLayerSemantics(); }
        });
        emitLong("rms", new LongCall() {
            public long run() throws Throwable { return rmsSemantics(); }
        });
        System.out.flush();
        notifyDestroyed();
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }

    public static int commandSemantics() {
        Command command = new Command("Go", "Go now", Command.OK, 7);
        int result = command.getCommandType();
        result = checksum(result, command.getPriority());
        result = checksum(result, command.getLabel());
        result = checksum(result, command.getLongLabel());
        return result;
    }

    public static int formItemSemantics() {
        StringItem first = new StringItem("label", "text", Item.BUTTON);
        Form form = new Form("Form", new Item[] {first});
        TextField field = new TextField("input", "abc", 8, TextField.ANY);
        Gauge gauge = new Gauge("level", true, 10, 4);
        int result = form.size();
        result = checksum(result, form.append(field));
        form.insert(1, gauge);
        result = checksum(result, form.size());
        result = checksum(result, ((StringItem) form.get(0)).getText());
        result = checksum(result, ((Gauge) form.get(1)).getValue());
        form.set(1, new StringItem(null, "replacement"));
        result = checksum(result, ((StringItem) form.get(1)).getText());
        form.delete(0);
        result = checksum(result, form.size());
        first.setLabel("changed");
        first.setText("updated");
        first.setLayout(Item.LAYOUT_RIGHT | Item.LAYOUT_NEWLINE_BEFORE);
        result = checksum(result, first.getLabel());
        result = checksum(result, first.getText());
        result = checksum(result, first.getLayout());
        return result;
    }

    public static int choiceGroupSemantics() {
        ChoiceGroup choice = new ChoiceGroup(
                "Choice",
                Choice.MULTIPLE,
                new String[] {"one", "two", "three"},
                null);
        choice.setSelectedIndex(0, true);
        choice.setSelectedIndex(2, true);
        choice.setFitPolicy(Choice.TEXT_WRAP_ON);
        choice.insert(1, "middle", null);
        choice.set(2, "changed", null);
        choice.delete(3);
        boolean[] flags = new boolean[8];
        int selected = choice.getSelectedFlags(flags);
        int result = choice.size();
        result = checksum(result, selected);
        result = checksum(result, choice.getSelectedIndex());
        result = checksum(result, choice.getFitPolicy());
        for (int i = 0; i < choice.size(); i++) {
            result = checksum(result, choice.getString(i));
            result = checksum(result, choice.isSelected(i) ? 1 : 0);
        }
        return result;
    }

    public static int listSemantics() {
        List list = new List(
                "List",
                Choice.EXCLUSIVE,
                new String[] {"alpha", "beta", "gamma"},
                null);
        list.setSelectedIndex(1, true);
        list.append("delta", null);
        list.insert(2, "inserted", null);
        list.delete(0);
        list.setFitPolicy(Choice.TEXT_WRAP_OFF);
        Command select = new Command("Open", Command.ITEM, 3);
        list.setSelectCommand(select);
        int result = list.size();
        result = checksum(result, list.getSelectedIndex());
        result = checksum(result, list.getFitPolicy());
        for (int i = 0; i < list.size(); i++) {
            result = checksum(result, list.getString(i));
        }
        result = checksum(result, List.SELECT_COMMAND.getCommandType());
        return result;
    }

    public static int textFieldSemantics() {
        TextField field = new TextField(
                "Number", "12345", 8,
                TextField.NUMERIC | TextField.NON_PREDICTIVE);
        int result = field.size();
        result = checksum(result, field.getMaxSize());
        result = checksum(result, field.getConstraints());
        field.setString("77");
        result = checksum(result, field.getString());
        result = checksum(result, field.setMaxSize(4));
        field.setConstraints(TextField.DECIMAL);
        field.setInitialInputMode("MIDP_LOWERCASE_LATIN");
        result = checksum(result, field.getConstraints());
        result = checksum(result, field.getCaretPosition());
        return result;
    }

    public static int gaugeDateSpacerSemantics() {
        Gauge gauge = new Gauge("Gauge", true, 10, 4);
        gauge.setValue(30);
        int result = gauge.getValue();
        gauge.setMaxValue(6);
        result = checksum(result, gauge.getMaxValue());
        result = checksum(result, gauge.getValue());
        result = checksum(result, gauge.isInteractive() ? 1 : 0);

        DateField date = new DateField(
                "Date", DateField.DATE_TIME, TimeZone.getTimeZone("GMT"));
        date.setDate(new Date(123456789L));
        result = checksum(result, date.getInputMode());
        result = checksum(result, (int) (date.getDate().getTime() ^
                (date.getDate().getTime() >>> 32)));
        date.setInputMode(DateField.TIME);
        result = checksum(result, date.getInputMode());

        Spacer spacer = new Spacer(3, 5);
        spacer.setMinimumSize(7, 11);
        result = checksum(result, spacer.getMinimumWidth());
        result = checksum(result, spacer.getMinimumHeight());

        Ticker ticker = new Ticker("tick");
        result = checksum(result, ticker.getString());
        ticker.setString("changed");
        result = checksum(result, ticker.getString());
        return result;
    }

    public static int fontSemantics() {
        Font font = Font.getFont(
                Font.FACE_MONOSPACE,
                Font.STYLE_BOLD | Font.STYLE_ITALIC,
                Font.SIZE_SMALL);
        int result = font.getFace();
        result = checksum(result, font.getStyle());
        result = checksum(result, font.getSize());
        result = checksum(result, font.isPlain() ? 1 : 0);
        result = checksum(result, font.isBold() ? 1 : 0);
        result = checksum(result, font.isItalic() ? 1 : 0);
        result = checksum(result, font.isUnderlined() ? 1 : 0);
        result = checksum(result, font.getHeight());
        result = checksum(result, font.getBaselinePosition());
        result = checksum(result, font.charWidth('W'));
        result = checksum(result, font.stringWidth("Wiệt"));
        result = checksum(result, font.substringWidth("abcdef", 1, 3));
        return result;
    }

    public static int imageGraphicsSemantics() {
        Image image = Image.createImage(4, 3);
        Graphics graphics = image.getGraphics();
        graphics.setColor(0x112233);
        graphics.fillRect(0, 0, 4, 3);
        graphics.setColor(0xAA5500);
        graphics.fillRect(1, 1, 2, 1);
        graphics.setClip(1, 0, 3, 3);
        graphics.translate(1, 1);
        graphics.setColor(0x0000FF);
        graphics.drawLine(-1, -1, 2, 1);
        int[] pixels = new int[12];
        image.getRGB(pixels, 0, 4, 0, 0, 4, 3);
        int result = image.getWidth();
        result = checksum(result, image.getHeight());
        result = checksum(result, image.isMutable() ? 1 : 0);
        for (int i = 0; i < pixels.length; i++) {
            result = checksum(result, pixels[i]);
        }
        Image copy = Image.createImage(image);
        result = checksum(result, copy.isMutable() ? 1 : 0);
        result = checksum(result, graphics.getTranslateX());
        result = checksum(result, graphics.getTranslateY());
        result = checksum(result, graphics.getClipX());
        result = checksum(result, graphics.getClipY());
        result = checksum(result, graphics.getClipWidth());
        result = checksum(result, graphics.getClipHeight());
        return result;
    }

    public static int spriteSemantics() {
        Image image = Image.createImage(4, 2);
        Sprite sprite = new Sprite(image, 2, 2);
        sprite.setFrameSequence(new int[] {1, 0});
        sprite.setFrame(1);
        sprite.nextFrame();
        sprite.prevFrame();
        sprite.setRefPixelPosition(20, 30);
        sprite.defineReferencePixel(1, 1);
        sprite.setTransform(Sprite.TRANS_ROT90);
        int result = sprite.getFrame();
        result = checksum(result, sprite.getFrameSequenceLength());
        result = checksum(result, sprite.getRawFrameCount());
        result = checksum(result, sprite.getRefPixelX());
        result = checksum(result, sprite.getRefPixelY());
        result = checksum(result, sprite.getX());
        result = checksum(result, sprite.getY());
        result = checksum(result, sprite.getWidth());
        result = checksum(result, sprite.getHeight());
        return result;
    }

    public static int tiledLayerSemantics() {
        Image tiles = Image.createImage(4, 4);
        TiledLayer layer = new TiledLayer(3, 2, tiles, 2, 2);
        int animated = layer.createAnimatedTile(3);
        layer.setAnimatedTile(animated, 4);
        layer.fillCells(0, 0, 3, 2, 1);
        layer.setCell(1, 0, animated);
        layer.setCell(2, 1, 2);
        layer.setPosition(7, 9);
        int result = layer.getColumns();
        result = checksum(result, layer.getRows());
        result = checksum(result, layer.getCellWidth());
        result = checksum(result, layer.getCellHeight());
        result = checksum(result, layer.getCell(1, 0));
        result = checksum(result, layer.getAnimatedTile(animated));
        result = checksum(result, layer.getCell(2, 1));
        result = checksum(result, layer.getX());
        result = checksum(result, layer.getY());
        result = checksum(result, layer.getWidth());
        result = checksum(result, layer.getHeight());
        return result;
    }

    public static long rmsSemantics() throws Throwable {
        final String name = "jme-differential-rms";
        try {
            RecordStore.deleteRecordStore(name);
        } catch (Throwable ignored) {
        }

        RecordStore store = RecordStore.openRecordStore(name, true);
        long result = store.getNextRecordID();
        int first = store.addRecord(new byte[] {1, 2, 3}, 0, 3);
        int second = store.addRecord(new byte[] {9, 8}, 0, 2);
        result = result * 31L + first;
        result = result * 31L + second;
        result = result * 31L + store.getNumRecords();
        result = result * 31L + store.getNextRecordID();
        result = result * 31L + store.getVersion();

        store.setRecord(first, new byte[] {7, 6, 5, 4}, 1, 2);
        byte[] data = store.getRecord(first);
        result = result * 31L + data.length;
        for (int i = 0; i < data.length; i++) {
            result = result * 31L + (data[i] & 0xFF);
        }

        RecordEnumeration enumeration = store.enumerateRecords(
                null, null, false);
        result = result * 31L + enumeration.numRecords();
        while (enumeration.hasNextElement()) {
            result = result * 31L + enumeration.nextRecordId();
        }
        enumeration.destroy();

        store.deleteRecord(second);
        result = result * 31L + store.getNumRecords();
        result = result * 31L + store.getVersion();
        store.closeRecordStore();

        store = RecordStore.openRecordStore(name, false);
        result = result * 31L + store.getNumRecords();
        result = result * 31L + store.getNextRecordID();
        result = result * 31L + store.getRecordSize(first);
        store.closeRecordStore();
        RecordStore.deleteRecordStore(name);
        return result;
    }
}
