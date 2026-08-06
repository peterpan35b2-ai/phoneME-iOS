package com.mascotcapsule.micro3d.v3;

public class FigureLayout {
    public FigureLayout() {}
    public FigureLayout(AffineTrans affine, int scaleX, int scaleY,
                        int centerX, int centerY) {}
    public final void setCenter(int x, int y) {}
    public final void setScale(int x, int y) {}
    public final void setParallelSize(int width, int height) {}
    public final void setPerspective(int near, int far, int angle) {}
    public final void setPerspective(int near, int far, int width, int height) {}
}
