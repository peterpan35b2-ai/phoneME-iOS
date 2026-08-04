package com.mascotcapsule.micro3d.v3;

public class Vector3D {
    public int x;
    public int y;
    public int z;

    public Vector3D() {}
    public Vector3D(int x, int y, int z) {}
    public Vector3D(Vector3D source) {}
    public final int getX() { return 0; }
    public final int getY() { return 0; }
    public final int getZ() { return 0; }
    public final int innerProduct(Vector3D value) { return 0; }
    public static int innerProduct(Vector3D left, Vector3D right) { return 0; }
    public final void outerProduct(Vector3D value) {}
    public static Vector3D outerProduct(Vector3D left, Vector3D right) { return null; }
    public final void set(int x, int y, int z) {}
    public final void set(Vector3D source) {}
    public final void setX(int value) {}
    public final void setY(int value) {}
    public final void setZ(int value) {}
    public final void unit() {}
}
