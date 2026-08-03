package javax.bluetooth;

public interface ServiceRecord {
    int NOAUTHENTICATE_NOENCRYPT = 0;
    int AUTHENTICATE_NOENCRYPT = 1;
    int AUTHENTICATE_ENCRYPT = 2;

    RemoteDevice getHostDevice();
    int[] getAttributeIDs();
    DataElement getAttributeValue(int attrID);
    boolean populateRecord(int[] attrIDs) throws java.io.IOException;
    boolean setAttributeValue(int attrID, DataElement attrValue);
    String getConnectionURL(int requiredSecurity, boolean mustBeMaster);
    void setDeviceServiceClasses(int classes);
}
