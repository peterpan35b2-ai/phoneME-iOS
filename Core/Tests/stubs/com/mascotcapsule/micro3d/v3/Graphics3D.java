package com.mascotcapsule.micro3d.v3;

import javax.microedition.lcdui.Graphics;

public class Graphics3D {
    public static final int COMMAND_END = 0x80000000;
    public static final int COMMAND_FLUSH = 0x82000000;
    public static final int COMMAND_LIST_VERSION_1_0 = 0xFE000001;
    public static final int ENV_ATTR_LIGHTING = 1;
    public static final int PATTR_BLEND_ADD = 64;
    public static final int PDATA_COLOR_PER_COMMAND = 0x00000400;
    public static final int PDATA_COLOR_PER_FACE = 0x00000800;
    public static final int PDATA_COLOR_PER_VERTEX = 0x00000C00;
    public static final int PDATA_NORMAL_PER_VERTEX = 768;
    public static final int PRIMITVE_POINTS = 0x01000000;
    public static final int PRIMITVE_LINES = 0x02000000;
    public static final int PRIMITVE_TRIANGLES = 0x03000000;
    public static final int PRIMITVE_QUADS = 0x04000000;

    public Graphics3D() {}
    public final void bind(Graphics graphics) {}
    public final void release(Graphics graphics) {}
    public final void dispose() {}
    public final void flush() {}
    public final void renderPrimitives(
        Texture texture, int x, int y, FigureLayout layout, Effect3D effect,
        int command, int count, int[] coordinates, int[] normals,
        int[] texcoords, int[] colors) {}
    public final void drawCommandList(
        Texture texture, int x, int y, FigureLayout layout, Effect3D effect,
        int[] commands) {}
    public final void drawCommandList(
        Texture[] textures, int x, int y, FigureLayout layout, Effect3D effect,
        int[] commands) {}
}
