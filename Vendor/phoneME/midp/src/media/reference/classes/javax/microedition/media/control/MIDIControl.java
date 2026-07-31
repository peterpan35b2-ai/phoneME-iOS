package javax.microedition.media.control;

import javax.microedition.media.Control;
import javax.microedition.media.MediaException;

public interface MIDIControl extends Control {
    int NOTE_ON = 0x90;
    int CONTROL_CHANGE = 0xB0;

    boolean isBankQuerySupported();
    int[] getProgram(int channel) throws MediaException;
    int getChannelVolume(int channel);
    void setProgram(int channel, int bank, int program);
    void setChannelVolume(int channel, int volume);
    int[] getBankList(boolean custom) throws MediaException;
    int[] getProgramList(int bank) throws MediaException;
    String getProgramName(int bank, int program) throws MediaException;
    String getKeyName(int bank, int program, int key) throws MediaException;
    void shortMidiEvent(int type, int data1, int data2);
    int longMidiEvent(byte[] data, int offset, int length);
}
