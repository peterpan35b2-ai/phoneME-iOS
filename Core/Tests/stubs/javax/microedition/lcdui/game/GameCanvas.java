package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;

public abstract class GameCanvas extends Canvas {
    public static final int UP_PRESSED = 1 << UP;
    public static final int LEFT_PRESSED = 1 << LEFT;
    public static final int RIGHT_PRESSED = 1 << RIGHT;
    public static final int DOWN_PRESSED = 1 << DOWN;
    public static final int FIRE_PRESSED = 1 << FIRE;
    public static final int GAME_A_PRESSED = 1 << GAME_A;
    public static final int GAME_B_PRESSED = 1 << GAME_B;
    public static final int GAME_C_PRESSED = 1 << GAME_C;
    public static final int GAME_D_PRESSED = 1 << GAME_D;

    protected GameCanvas(boolean suppressKeyEvents) { }

    public int getKeyStates() { return 0; }
    protected Graphics getGraphics() { return null; }
    public void flushGraphics() { }
    public void flushGraphics(int x, int y, int width, int height) { }
}
