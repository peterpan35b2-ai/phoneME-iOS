package com.sun.midp.security;

public final class PermissionGate {
    private PermissionGate() {
    }

    public static int checkPermission(String permission) {
        return -1;
    }

    public static int requestPermission(String permission,
                                        String resource,
                                        boolean userInitiated) {
        return 0;
    }

    public static void requirePermission(String permission,
                                         String resource,
                                         boolean userInitiated) {
    }
}
