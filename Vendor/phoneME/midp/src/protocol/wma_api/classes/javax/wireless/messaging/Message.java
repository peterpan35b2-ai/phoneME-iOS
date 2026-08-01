/*
 * Copyright 1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 *
 * Minimal JSR-120 public API retained for class linking and preverification.
 */

package javax.wireless.messaging;

public interface Message {
    String getAddress();
    void setAddress(String address);
    java.util.Date getTimestamp();
}
