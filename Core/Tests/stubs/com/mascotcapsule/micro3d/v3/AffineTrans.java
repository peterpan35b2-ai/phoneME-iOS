package com.mascotcapsule.micro3d.v3;

public class AffineTrans {
    public int m00, m01, m02, m03;
    public int m10, m11, m12, m13;
    public int m20, m21, m22, m23;

    public AffineTrans() {}
    public AffineTrans(AffineTrans source) {}
    public AffineTrans(int[] values) {}
    public AffineTrans(int[] values, int offset) {}
    public AffineTrans(int[][] values) {}
    public AffineTrans(int m00, int m01, int m02, int m03,
                       int m10, int m11, int m12, int m13,
                       int m20, int m21, int m22, int m23) {}
    public final void get(int[] values) {}
    public final void get(int[] values, int offset) {}
    public final void mul(AffineTrans right) {}
    public final void mul(AffineTrans left, AffineTrans right) {}
    public final void multiply(AffineTrans right) {}
    public final void multiply(AffineTrans left, AffineTrans right) {}
    public final void rotationX(int angle) {}
    public final void rotationY(int angle) {}
    public final void rotationZ(int angle) {}
    public final void setIdentity() {}
    public final void setRotationX(int angle) {}
    public final void setRotationY(int angle) {}
    public final void setRotationZ(int angle) {}
    public final Vector3D transform(Vector3D value) { return null; }
    public final Vector3D transPoint(Vector3D value) { return null; }
}
