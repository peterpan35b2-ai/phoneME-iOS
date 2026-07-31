package com.nokia.mid.sound;

import java.io.ByteArrayInputStream;
import java.util.Vector;
import javax.microedition.media.Control;
import javax.microedition.media.Manager;
import javax.microedition.media.MediaException;
import javax.microedition.media.Player;
import javax.microedition.media.PlayerListener;
import javax.microedition.media.control.VolumeControl;

/** Nokia Sound compatibility implementation backed by JSR-135. */
public class Sound implements PlayerListener, Runnable {
    public static final int FORMAT_TONE = 1;
    public static final int FORMAT_WAV = 5;

    public static final int SOUND_PLAYING = 0;
    public static final int SOUND_STOPPED = 1;
    public static final int SOUND_UNINITIALIZED = 3;

    private static final int[] FREQUENCIES = {
        220, 233, 247, 262, 277, 294, 311, 330, 349, 370, 392, 416,
        440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784, 831,
        880, 932, 988, 1047, 1109, 1175, 1245, 1319, 1397, 1480,
        1568, 1661, 1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637,
        2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186, 4434, 4698,
        4978, 5274, 5588, 5920, 6272, 6644, 7040, 7458, 7902, 8372,
        8870, 9396, 9956, 10548, 11176, 11840, 12544, 13288
    };

    private Player player;
    private ToneSong toneSong;
    private int gain = 255;
    private int state = SOUND_UNINITIALIZED;
    private int requestedLoops = 1;
    private boolean stopRequested;
    private SoundListener listener;
    private Thread toneThread;

    public Sound(byte[] data, int type) {
        init(data, type);
    }

    public Sound(int frequency, long duration) {
        init(frequency, duration);
    }

    public static int[] getSupportedFormats() {
        return new int[] { FORMAT_TONE, FORMAT_WAV };
    }

    public static int getConcurrentSoundCount(int type) {
        if (type != FORMAT_TONE && type != FORMAT_WAV) {
            return 0;
        }
        return 8;
    }

    public synchronized void init(int frequency, long duration) {
        releaseInternal(false);
        if (frequency < 0 || frequency > 13288 || duration <= 0) {
            state = SOUND_UNINITIALIZED;
            throw new IllegalArgumentException("Invalid tone parameters");
        }
        ToneEvent event = new ToneEvent(frequencyToMIDINote(frequency),
                duration > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int)duration,
                gainToVolume(gain));
        toneSong = new ToneSong(new ToneEvent[] { event }, false);
        state = SOUND_STOPPED;
    }

    public synchronized void init(byte[] data, int type) {
        releaseInternal(false);
        if (data == null) {
            state = SOUND_UNINITIALIZED;
            throw new NullPointerException("sound data is null");
        }
        try {
            if (type == FORMAT_WAV) {
                player = Manager.createPlayer(new ByteArrayInputStream(data),
                        "audio/x-wav");
                player.addPlayerListener(this);
                player.realize();
                applyGain();
            } else if (type == FORMAT_TONE) {
                toneSong = OTARingtoneParser.parse(data);
            } else {
                throw new IllegalArgumentException("Unsupported sound format");
            }
            state = SOUND_STOPPED;
        } catch (Exception error) {
            releaseInternal(false);
            state = SOUND_UNINITIALIZED;
            if (error instanceof IllegalArgumentException) {
                throw (IllegalArgumentException)error;
            }
            throw new IllegalArgumentException(error.toString());
        }
    }

    public void play(int loop) {
        if (loop < 0) {
            throw new IllegalArgumentException("Negative loop count");
        }

        synchronized (this) {
            if (state == SOUND_UNINITIALIZED) {
                return;
            }
            if (state == SOUND_PLAYING) {
                stopInternal(true);
            }
            requestedLoops = loop;
            stopRequested = false;
            if (player != null) {
                try {
                    player.stop();
                    player.setMediaTime(0);
                    player.setLoopCount(loop == 0 ? -1 : loop);
                    player.start();
                    setState(SOUND_PLAYING);
                    return;
                } catch (MediaException error) {
                    setState(SOUND_STOPPED);
                    return;
                }
            }
            toneThread = new Thread(this, "Nokia Sound");
            setState(SOUND_PLAYING);
            toneThread.start();
        }
    }

    public synchronized void stop() {
        stopInternal(true);
    }

    public synchronized void resume() {
        if (state != SOUND_STOPPED) {
            return;
        }
        if (player != null) {
            try {
                player.start();
                setState(SOUND_PLAYING);
            } catch (MediaException ignored) {
            }
        } else if (toneSong != null) {
            play(requestedLoops);
        }
    }

    public synchronized void release() {
        releaseInternal(true);
    }

    public synchronized int getState() {
        return state;
    }

    public synchronized void setGain(int newGain) {
        if (newGain < 0) newGain = 0;
        if (newGain > 255) newGain = 255;
        gain = newGain;
        applyGain();
    }

    public synchronized int getGain() {
        return gain;
    }

    public synchronized void setSoundListener(SoundListener newListener) {
        listener = newListener;
    }

    public void run() {
        int loops = requestedLoops;
        boolean infinite = loops == 0 || (toneSong != null && toneSong.infinite);
        try {
            do {
                ToneEvent[] events;
                synchronized (this) {
                    if (stopRequested || toneSong == null) return;
                    events = toneSong.events;
                }
                for (int i = 0; i < events.length; i++) {
                    ToneEvent event = events[i];
                    synchronized (this) {
                        if (stopRequested) return;
                    }
                    if (event.note >= 0) {
                        Manager.playTone(event.note, event.soundDuration,
                                gainToVolume(gain) * event.volume / 100);
                    }
                    sleepInterruptibly(event.totalDuration);
                }
                if (!infinite) loops--;
            } while (infinite || loops > 0);
        } catch (Throwable ignored) {
        }

        synchronized (this) {
            toneThread = null;
            if (!stopRequested && state == SOUND_PLAYING) {
                setState(SOUND_STOPPED);
            }
        }
    }

    public synchronized void playerUpdate(Player source, String event,
            Object eventData) {
        if (source != player) {
            return;
        }
        if (PlayerListener.END_OF_MEDIA.equals(event) ||
                PlayerListener.ERROR.equals(event) ||
                PlayerListener.CLOSED.equals(event)) {
            if (state != SOUND_UNINITIALIZED) {
                setState(SOUND_STOPPED);
            }
        }
    }

    private void sleepInterruptibly(int milliseconds) {
        long deadline = System.currentTimeMillis() + milliseconds;
        while (true) {
            synchronized (this) {
                if (stopRequested) return;
            }
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) return;
            try {
                Thread.sleep(remaining > 30 ? 30 : remaining);
            } catch (InterruptedException ignored) {
            }
        }
    }

    private void stopInternal(boolean notify) {
        if (state != SOUND_PLAYING) {
            return;
        }
        stopRequested = true;
        if (player != null) {
            try {
                player.stop();
            } catch (MediaException ignored) {
            }
        }
        toneThread = null;
        if (notify) setState(SOUND_STOPPED);
        else state = SOUND_STOPPED;
    }

    private void releaseInternal(boolean notify) {
        stopRequested = true;
        if (player != null) {
            player.removePlayerListener(this);
            player.close();
            player = null;
        }
        toneSong = null;
        toneThread = null;
        if (notify) setState(SOUND_UNINITIALIZED);
        else state = SOUND_UNINITIALIZED;
    }

    private void applyGain() {
        if (player == null || player.getState() < Player.REALIZED) {
            return;
        }
        Control control = player.getControl("VolumeControl");
        if (control instanceof VolumeControl) {
            ((VolumeControl)control).setLevel(gainToVolume(gain));
        }
    }

    private void setState(int newState) {
        if (state == newState) return;
        state = newState;
        SoundListener current = listener;
        if (current != null) {
            try {
                current.soundStateChanged(this, newState);
            } catch (Throwable ignored) {
            }
        }
    }

    private static int gainToVolume(int value) {
        if (value <= 0) return 0;
        return 1 + (value - 1) * 99 / 254;
    }

    private static int frequencyToMIDINote(int frequency) {
        if (frequency == 0) return -1;
        int nearest = 0;
        int bestDistance = Math.abs(FREQUENCIES[0] - frequency);
        for (int i = 1; i < FREQUENCIES.length; i++) {
            int distance = Math.abs(FREQUENCIES[i] - frequency);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = i;
            }
        }
        return 57 + nearest;
    }

    private static final class ToneEvent {
        final int note;
        final int totalDuration;
        final int soundDuration;
        final int volume;

        ToneEvent(int note, int duration, int volume) {
            this(note, duration, duration, volume);
        }

        ToneEvent(int note, int totalDuration, int soundDuration, int volume) {
            this.note = note;
            this.totalDuration = totalDuration < 1 ? 1 : totalDuration;
            this.soundDuration = soundDuration < 1 ? 1 : soundDuration;
            this.volume = volume;
        }
    }

    private static final class ToneSong {
        final ToneEvent[] events;
        final boolean infinite;

        ToneSong(ToneEvent[] events, boolean infinite) {
            this.events = events;
            this.infinite = infinite;
        }
    }

    /** Decoder for Nokia Smart Messaging binary OTA ringtones. */
    private static final class OTARingtoneParser {
        private static final int[] TEMPOS = {
            25, 28, 31, 35, 40, 45, 50, 56,
            63, 70, 80, 90, 100, 112, 125, 140,
            160, 180, 200, 225, 250, 285, 320, 355,
            400, 450, 500, 565, 635, 715, 800, 900
        };
        private static final int[] DIVISORS = { 1, 2, 4, 8, 16, 32 };

        static ToneSong parse(byte[] data) {
            int soundOffset = findSoundCommand(data);
            if (soundOffset < 0) {
                throw new IllegalArgumentException("Invalid Nokia OTA ringtone");
            }
            BitReader reader = new BitReader(data, soundOffset * 8 + 7);
            int songType = reader.read(3);
            if (songType == 1) {
                int titleLength = reader.read(4);
                reader.skip(titleLength * 8);
            } else if (songType != 2) {
                throw new IllegalArgumentException("Unsupported OTA song type");
            }

            int sequenceLength = reader.read(8);
            if (sequenceLength <= 0) {
                throw new IllegalArgumentException("Empty OTA ringtone");
            }

            Vector output = new Vector();
            Vector[] patterns = new Vector[4];
            boolean infinite = false;
            int scale = 1;
            int style = 0;
            int tempo = 63;
            int volume = 100;

            for (int sequence = 0; sequence < sequenceLength; sequence++) {
                if (reader.read(3) != 0) {
                    throw new IllegalArgumentException("Invalid OTA pattern header");
                }
                int patternID = reader.read(2);
                int loop = reader.read(4);
                int instructionCount = reader.read(8);
                Vector pattern;
                if (instructionCount == 0) {
                    pattern = patterns[patternID];
                    if (pattern == null) {
                        throw new IllegalArgumentException("Undefined OTA pattern");
                    }
                } else {
                    pattern = new Vector();
                    for (int instruction = 0; instruction < instructionCount;
                            instruction++) {
                        int instructionID = reader.read(3);
                        if (instructionID == 1) {
                            int noteValue = reader.read(4);
                            int durationCode = reader.read(3);
                            int durationSpecifier = reader.read(2);
                            if (noteValue > 12 || durationCode > 5) {
                                throw new IllegalArgumentException("Invalid OTA note");
                            }
                            int duration = noteDuration(tempo,
                                    DIVISORS[durationCode], durationSpecifier);
                            int soundDuration = styledDuration(duration, style);
                            int note = noteValue == 0 ? -1 :
                                    59 + noteValue + scale * 12;
                            pattern.addElement(new ToneEvent(note, duration,
                                    soundDuration, volume));
                        } else if (instructionID == 2) {
                            scale = reader.read(2);
                        } else if (instructionID == 3) {
                            style = reader.read(2);
                            if (style == 3) style = 0;
                        } else if (instructionID == 4) {
                            tempo = TEMPOS[reader.read(5)];
                        } else if (instructionID == 5) {
                            volume = reader.read(4) * 100 / 15;
                        } else {
                            throw new IllegalArgumentException(
                                    "Unsupported OTA instruction");
                        }
                    }
                    patterns[patternID] = pattern;
                }

                if (loop == 15) {
                    infinite = true;
                    append(output, pattern, 1);
                } else {
                    append(output, pattern, loop + 1);
                }
            }

            ToneEvent[] events = new ToneEvent[output.size()];
            output.copyInto(events);
            return new ToneSong(events, infinite);
        }

        private static int findSoundCommand(byte[] data) {
            if (data.length < 3 || (data[0] & 0xff) < 2) return -1;
            for (int i = 1; i < data.length; i++) {
                if (((data[i] & 0xff) >>> 1) == 0x1d) return i;
            }
            return -1;
        }

        private static void append(Vector output, Vector pattern, int repeats) {
            if (repeats > 16) repeats = 16;
            for (int repeat = 0; repeat < repeats; repeat++) {
                for (int i = 0; i < pattern.size(); i++) {
                    output.addElement(pattern.elementAt(i));
                }
            }
        }

        private static int noteDuration(int tempo, int divisor,
                int specifier) {
            long duration = 240000L / ((long)tempo * (long)divisor);
            if (specifier == 1) duration = duration * 3 / 2;
            else if (specifier == 2) duration = duration * 7 / 4;
            else if (specifier == 3) duration = duration * 2 / 3;
            if (duration < 1) duration = 1;
            if (duration > Integer.MAX_VALUE) duration = Integer.MAX_VALUE;
            return (int)duration;
        }

        private static int styledDuration(int duration, int style) {
            if (style == 1) return duration;
            if (style == 2) return duration * 3 / 4;
            return duration * 7 / 8;
        }
    }

    private static final class BitReader {
        private final byte[] data;
        private int bitPosition;

        BitReader(byte[] data, int bitPosition) {
            this.data = data;
            this.bitPosition = bitPosition;
        }

        int read(int count) {
            if (count < 0 || bitPosition + count > data.length * 8) {
                throw new IllegalArgumentException("Truncated OTA ringtone");
            }
            int result = 0;
            for (int i = 0; i < count; i++) {
                int byteIndex = bitPosition >>> 3;
                int bitIndex = 7 - (bitPosition & 7);
                result = (result << 1) |
                        ((data[byteIndex] >>> bitIndex) & 1);
                bitPosition++;
            }
            return result;
        }

        void skip(int count) {
            read(count);
        }
    }
}
