/*
 * iOS native media player for phoneME.
 */
package javax.microedition.media;

import java.util.Hashtable;
import java.util.Vector;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

final class IOSPlayer implements Player, Runnable {
    private static final String TONE_TYPE = "audio/x-tone-seq";
    private static final String MIDI_TYPE = "audio/midi";

    private final byte[] mediaData;
    private final String locator;
    private final String contentType;
    private final Vector listeners = new Vector();
    private final IOSVolumeControl volumeControl = new IOSVolumeControl();
    private final IOSToneControl toneControl;

    private int nativeHandle;
    private int state = UNREALIZED;
    private int loopCount = 1;
    private TimeBase timeBase = Manager.getSystemTimeBase();
    private volatile boolean monitorStopRequested;
    private volatile boolean toneStopRequested;
    private volatile boolean explicitlyStopped;
    private byte[] toneSequence;

    IOSPlayer(byte[] data, String type) {
        mediaData = data;
        locator = null;
        contentType = normalizeContentType(type);
        toneControl = TONE_TYPE.equals(contentType) ? new IOSToneControl() : null;
        if (toneControl != null && data != null) {
            toneSequence = copyBytes(data);
        }
    }

    IOSPlayer(String mediaLocator, String type) {
        mediaData = null;
        locator = mediaLocator;
        contentType = normalizeContentType(type);
        toneControl = TONE_TYPE.equals(contentType) ? new IOSToneControl() : null;
    }

    static Player createTonePlayer() {
        return new IOSPlayer((byte[])null, TONE_TYPE);
    }

    static Player createMIDIPlayer() {
        return new IOSPlayer((byte[])null, MIDI_TYPE);
    }

    static boolean isSupportedContentType(String type) {
        if (type == null || type.trim().length() == 0) {
            return false;
        }
        String normalized = normalizeContentType(type);
        return "audio/mpeg".equals(normalized) ||
                "audio/x-wav".equals(normalized) ||
                "audio/basic".equals(normalized) ||
                "audio/aac".equals(normalized) ||
                "audio/mp4".equals(normalized) ||
                "audio/midi".equals(normalized) ||
                "audio/amr".equals(normalized) ||
                "audio/amr-wb".equals(normalized) ||
                TONE_TYPE.equals(normalized);
    }

    static boolean playTone(int note, int duration, int volume) {
        if (volume < 0) {
            volume = 0;
        } else if (volume > 100) {
            volume = 100;
        }
        return nPlayTone(note, duration, volume);
    }

    public synchronized void realize() throws MediaException {
        checkNotClosed();
        if (state >= REALIZED) {
            return;
        }

        if (toneControl != null) {
            state = REALIZED;
            return;
        }

        if (mediaData == null && locator == null) {
            state = REALIZED;
            return;
        }

        if (mediaData != null) {
            nativeHandle = nCreateData(mediaData, contentType);
        } else {
            nativeHandle = nCreateLocator(locator, contentType);
        }
        if (nativeHandle == 0) {
            throw new MediaException("Unsupported or corrupt media: " + contentType);
        }
        nSetLoopCount(nativeHandle, loopCount);
        nSetVolume(nativeHandle, volumeControl.level);
        nSetMute(nativeHandle, volumeControl.muted);
        state = REALIZED;
    }

    public synchronized void prefetch() throws MediaException {
        checkNotClosed();
        if (state == UNREALIZED) {
            realize();
        }
        if (state < PREFETCHED) {
            state = PREFETCHED;
        }
    }

    public void start() throws MediaException {
        synchronized (this) {
            checkNotClosed();
            if (state == STARTED) {
                return;
            }
            if (state < PREFETCHED) {
                prefetch();
            }
            explicitlyStopped = false;
            monitorStopRequested = false;
            toneStopRequested = false;

            if (toneControl != null) {
                if (toneSequence == null || toneSequence.length == 0) {
                    throw new MediaException("Tone sequence is not set");
                }
                state = STARTED;
                notifyListeners(PlayerListener.STARTED, new Long(0L));
                new Thread(this, "phoneME tone player").start();
                return;
            }

            if (nativeHandle == 0 || !nStart(nativeHandle)) {
                throw new MediaException("Unable to start media");
            }
            state = STARTED;
            notifyListeners(PlayerListener.STARTED, new Long(getMediaTime()));
            new Thread(this, "phoneME media monitor").start();
        }
    }

    public void stop() throws MediaException {
        long mediaTime;
        synchronized (this) {
            checkNotClosed();
            if (state != STARTED) {
                return;
            }
            explicitlyStopped = true;
            monitorStopRequested = true;
            toneStopRequested = true;
            if (nativeHandle != 0 && !nStop(nativeHandle)) {
                throw new MediaException("Unable to stop media");
            }
            state = PREFETCHED;
            mediaTime = getMediaTimeLocked();
        }
        notifyListeners(PlayerListener.STOPPED, new Long(mediaTime));
    }

    public synchronized void deallocate() {
        if (state == CLOSED || state == UNREALIZED) {
            return;
        }
        monitorStopRequested = true;
        toneStopRequested = true;
        explicitlyStopped = true;
        if (nativeHandle != 0) {
            nStop(nativeHandle);
        }
        state = REALIZED;
    }

    public void close() {
        boolean sendEvent;
        synchronized (this) {
            if (state == CLOSED) {
                return;
            }
            monitorStopRequested = true;
            toneStopRequested = true;
            explicitlyStopped = true;
            if (nativeHandle != 0) {
                nClose(nativeHandle);
                nativeHandle = 0;
            }
            state = CLOSED;
            sendEvent = true;
        }
        if (sendEvent) {
            notifyListeners(PlayerListener.CLOSED, null);
        }
    }

    public synchronized void setTimeBase(TimeBase master) throws MediaException {
        checkNotClosed();
        if (state == STARTED) {
            throw new IllegalStateException("Player is started");
        }
        timeBase = master == null ? Manager.getSystemTimeBase() : master;
    }

    public synchronized TimeBase getTimeBase() {
        checkRealized();
        return timeBase;
    }

    public synchronized long setMediaTime(long now) throws MediaException {
        checkRealized();
        if (now < 0) {
            now = 0;
        }
        if (toneControl != null) {
            return 0;
        }
        if (nativeHandle == 0) {
            return 0;
        }
        return nSetMediaTime(nativeHandle, now);
    }

    public synchronized long getMediaTime() {
        if (state == CLOSED) {
            throw new IllegalStateException("Player is closed");
        }
        return getMediaTimeLocked();
    }

    public synchronized int getState() {
        return state;
    }

    public synchronized long getDuration() {
        if (state == CLOSED) {
            throw new IllegalStateException("Player is closed");
        }
        if (toneControl != null) {
            return toneSequence == null ? TIME_UNKNOWN : toneDurationMicros(toneSequence);
        }
        if (nativeHandle == 0) {
            return TIME_UNKNOWN;
        }
        long value = nGetDuration(nativeHandle);
        return value > 0 ? value : TIME_UNKNOWN;
    }

    public synchronized String getContentType() {
        checkRealized();
        return contentType;
    }

    public synchronized void setLoopCount(int count) {
        checkNotClosed();
        if (count == 0) {
            throw new IllegalArgumentException("loop count cannot be zero");
        }
        if (state == STARTED) {
            throw new IllegalStateException("Player is started");
        }
        loopCount = count;
        if (nativeHandle != 0) {
            nSetLoopCount(nativeHandle, count);
        }
    }

    public synchronized void addPlayerListener(PlayerListener listener) {
        checkNotClosed();
        if (listener != null && !listeners.contains(listener)) {
            listeners.addElement(listener);
        }
    }

    public synchronized void removePlayerListener(PlayerListener listener) {
        checkNotClosed();
        listeners.removeElement(listener);
    }

    public synchronized Control[] getControls() {
        checkRealized();
        if (toneControl != null) {
            return new Control[] { volumeControl, toneControl };
        }
        return new Control[] { volumeControl };
    }

    public synchronized Control getControl(String controlType) {
        checkRealized();
        if (controlType == null) {
            throw new IllegalArgumentException("control type is null");
        }
        if ("VolumeControl".equals(controlType) ||
                "javax.microedition.media.control.VolumeControl".equals(controlType)) {
            return volumeControl;
        }
        if (toneControl != null && ("ToneControl".equals(controlType) ||
                "javax.microedition.media.control.ToneControl".equals(controlType))) {
            return toneControl;
        }
        return null;
    }

    public void run() {
        if (toneControl != null) {
            runToneSequence();
        } else {
            monitorNativePlayback();
        }
    }

    private void monitorNativePlayback() {
        for (;;) {
            try {
                Thread.sleep(40L);
            } catch (InterruptedException ignored) {
            }

            long mediaTime;
            boolean failed;
            synchronized (this) {
                if (monitorStopRequested || state != STARTED || nativeHandle == 0) {
                    return;
                }
                failed = nHasError(nativeHandle);
                if (!failed) {
                    if (nIsPlaying(nativeHandle)) {
                        continue;
                    }
                    if (!nHasEnded(nativeHandle)) {
                        continue;
                    }
                }
                state = PREFETCHED;
                mediaTime = getMediaTimeLocked();
            }
            if (failed) {
                notifyListeners(PlayerListener.ERROR,
                        "Unable to load or play media: " +
                        (locator == null ? contentType : locator));
            } else {
                notifyListeners(PlayerListener.END_OF_MEDIA,
                        new Long(mediaTime));
            }
            return;
        }
    }

    private void runToneSequence() {
        int remainingLoops = loopCount;
        boolean infinite = remainingLoops == -1;
        try {
            do {
                playToneSequenceOnce(toneSequence);
                if (toneStopRequested) {
                    return;
                }
                if (!infinite) {
                    remainingLoops--;
                }
            } while (infinite || remainingLoops > 0);
        } catch (Throwable error) {
            synchronized (this) {
                if (state == STARTED) {
                    state = PREFETCHED;
                }
            }
            notifyListeners(PlayerListener.ERROR, error.toString());
            return;
        }

        synchronized (this) {
            if (toneStopRequested || explicitlyStopped || state != STARTED) {
                return;
            }
            state = PREFETCHED;
        }
        notifyListeners(PlayerListener.END_OF_MEDIA,
                new Long(toneDurationMicros(toneSequence)));
    }

    private void playToneSequenceOnce(byte[] sequence) throws MediaException {
        ToneProgram program = ToneProgram.parse(sequence);
        playToneRange(program, 0, program.eventCount, 0);
    }

    private void playToneRange(ToneProgram program, int start, int end, int depth)
            throws MediaException {
        if (depth > 8) {
            throw new MediaException("Tone block nesting is too deep");
        }
        int tempo = 120;
        int resolution = 64;
        int volume = volumeControl.level;

        for (int i = start; i < end && !toneStopRequested; i++) {
            int command = program.commands[i];
            int argument = program.arguments[i];
            if (command == ToneControl.VERSION) {
                continue;
            } else if (command == ToneControl.TEMPO) {
                tempo = clamp(argument, 5, 127) * 4;
            } else if (command == ToneControl.RESOLUTION) {
                resolution = clamp(argument, 1, 127);
            } else if (command == ToneControl.SET_VOLUME) {
                volume = clamp(argument, 0, 100);
            } else if (command == ToneControl.PLAY_BLOCK) {
                int[] range = (int[])program.blocks.get(new Integer(argument));
                if (range != null) {
                    playToneRange(program, range[0], range[1], depth + 1);
                }
            } else if (command == ToneControl.REPEAT) {
                if (i + 1 >= end) {
                    throw new MediaException("Invalid REPEAT event");
                }
                int repeatCount = clamp(argument, 2, 127);
                int nextCommand = program.commands[++i];
                int nextArgument = program.arguments[i];
                for (int repeat = 0; repeat < repeatCount && !toneStopRequested; repeat++) {
                    playToneEvent(nextCommand, nextArgument, tempo, resolution, volume);
                }
            } else if (command == ToneControl.BLOCK_START ||
                    command == ToneControl.BLOCK_END) {
                continue;
            } else {
                playToneEvent(command, argument, tempo, resolution, volume);
            }
        }
    }

    private void playToneEvent(int note, int ticks, int tempo, int resolution, int volume)
            throws MediaException {
        int duration = durationMillis(ticks, tempo, resolution);
        if (note != ToneControl.SILENCE) {
            if (note < 0 || note > 127) {
                throw new MediaException("Invalid tone note: " + note);
            }
            if (!playTone(note, duration, volume)) {
                throw new MediaException("Unable to play tone event");
            }
        }
        sleepTone(duration);
    }

    private void sleepTone(int duration) {
        long deadline = System.currentTimeMillis() + duration;
        while (!toneStopRequested) {
            long remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0) {
                return;
            }
            try {
                Thread.sleep(remaining > 30L ? 30L : remaining);
            } catch (InterruptedException ignored) {
            }
        }
    }

    private synchronized long getMediaTimeLocked() {
        if (toneControl != null || nativeHandle == 0) {
            return 0;
        }
        return nGetMediaTime(nativeHandle);
    }

    private void notifyListeners(String event, Object data) {
        PlayerListener[] snapshot;
        synchronized (this) {
            snapshot = new PlayerListener[listeners.size()];
            for (int i = 0; i < snapshot.length; i++) {
                snapshot[i] = (PlayerListener)listeners.elementAt(i);
            }
        }
        for (int i = 0; i < snapshot.length; i++) {
            try {
                snapshot[i].playerUpdate(this, event, data);
            } catch (Throwable ignored) {
            }
        }
    }

    private synchronized void checkNotClosed() {
        if (state == CLOSED) {
            throw new IllegalStateException("Player is closed");
        }
    }

    private synchronized void checkRealized() {
        checkNotClosed();
        if (state == UNREALIZED) {
            throw new IllegalStateException("Player is unrealized");
        }
    }

    private static int clamp(int value, int minimum, int maximum) {
        if (value < minimum) {
            return minimum;
        }
        if (value > maximum) {
            return maximum;
        }
        return value;
    }

    private static int durationMillis(int ticks, int tempo, int resolution) {
        if (ticks < 1) {
            ticks = 1;
        }
        long result = (60000L * ticks) / ((long)tempo * (long)resolution);
        if (result < 1L) {
            result = 1L;
        }
        if (result > 600000L) {
            result = 600000L;
        }
        return (int)result;
    }

    private static long toneDurationMicros(byte[] sequence) {
        try {
            ToneProgram program = ToneProgram.parse(sequence);
            int tempo = 120;
            int resolution = 64;
            long total = 0L;
            for (int i = 0; i < program.eventCount; i++) {
                int command = program.commands[i];
                int argument = program.arguments[i];
                if (command == ToneControl.TEMPO) {
                    tempo = clamp(argument, 5, 127) * 4;
                } else if (command == ToneControl.RESOLUTION) {
                    resolution = clamp(argument, 1, 127);
                } else if (command >= ToneControl.SILENCE) {
                    total += durationMillis(argument, tempo, resolution) * 1000L;
                } else if (command == ToneControl.REPEAT && i + 1 < program.eventCount) {
                    int count = clamp(argument, 2, 127);
                    int duration = program.arguments[i + 1];
                    total += (long)durationMillis(duration, tempo, resolution) *
                            (long)count * 1000L;
                    i++;
                }
            }
            return total;
        } catch (Throwable ignored) {
            return TIME_UNKNOWN;
        }
    }

    private static byte[] copyBytes(byte[] value) {
        byte[] copy = new byte[value.length];
        System.arraycopy(value, 0, copy, 0, value.length);
        return copy;
    }

    private static String normalizeContentType(String type) {
        if (type == null || type.trim().length() == 0) {
            return "application/octet-stream";
        }
        int separator = type.indexOf(';');
        if (separator >= 0) {
            type = type.substring(0, separator);
        }
        type = type.trim().toLowerCase();
        if ("audio/mp3".equals(type)) {
            return "audio/mpeg";
        }
        if ("audio/wav".equals(type)) {
            return "audio/x-wav";
        }
        if ("audio/x-midi".equals(type) || "audio/sp-midi".equals(type)) {
            return "audio/midi";
        }
        if ("audio/x-m4a".equals(type)) {
            return "audio/mp4";
        }
        if ("audio/x-aac".equals(type) || "audio/aacp".equals(type) ||
                "audio/mp4a-latm".equals(type)) {
            return "audio/aac";
        }
        return type;
    }

    private final class IOSVolumeControl implements VolumeControl {
        private int level = 100;
        private boolean muted;

        public void setMute(boolean value) {
            boolean changed;
            synchronized (IOSPlayer.this) {
                changed = muted != value;
                muted = value;
                if (nativeHandle != 0) {
                    nSetMute(nativeHandle, muted);
                }
            }
            if (changed) {
                notifyListeners(PlayerListener.VOLUME_CHANGED, this);
            }
        }

        public boolean isMuted() {
            synchronized (IOSPlayer.this) {
                return muted;
            }
        }

        public int setLevel(int value) {
            value = clamp(value, 0, 100);
            boolean changed;
            synchronized (IOSPlayer.this) {
                changed = level != value;
                level = value;
                if (nativeHandle != 0) {
                    nSetVolume(nativeHandle, level);
                }
            }
            if (changed) {
                notifyListeners(PlayerListener.VOLUME_CHANGED, this);
            }
            return value;
        }

        public int getLevel() {
            synchronized (IOSPlayer.this) {
                return level;
            }
        }
    }

    private final class IOSToneControl implements ToneControl {
        public void setSequence(byte[] sequence) {
            if (sequence == null) {
                throw new IllegalArgumentException("sequence is null");
            }
            ToneProgram.parse(sequence);
            synchronized (IOSPlayer.this) {
                if (state >= PREFETCHED) {
                    throw new IllegalStateException("Player is prefetched or started");
                }
                toneSequence = copyBytes(sequence);
            }
        }
    }

    private static final class ToneProgram {
        final int[] commands;
        final int[] arguments;
        final int eventCount;
        final Hashtable blocks;

        ToneProgram(int[] parsedCommands, int[] parsedArguments,
                int parsedEventCount, Hashtable parsedBlocks) {
            commands = parsedCommands;
            arguments = parsedArguments;
            eventCount = parsedEventCount;
            blocks = parsedBlocks;
        }

        static ToneProgram parse(byte[] sequence) {
            if (sequence == null || sequence.length < 2 ||
                    (sequence.length & 1) != 0) {
                throw new IllegalArgumentException("Invalid tone sequence length");
            }
            int count = sequence.length / 2;
            int[] commands = new int[count];
            int[] arguments = new int[count];
            Hashtable blocks = new Hashtable();
            Hashtable blockStarts = new Hashtable();

            for (int i = 0; i < count; i++) {
                int command = sequence[i * 2];
                int argument = sequence[(i * 2) + 1] & 0xff;
                commands[i] = command;
                arguments[i] = argument;
                if (command == ToneControl.BLOCK_START) {
                    blockStarts.put(new Integer(argument), new Integer(i + 1));
                } else if (command == ToneControl.BLOCK_END) {
                    Integer start = (Integer)blockStarts.get(new Integer(argument));
                    if (start == null) {
                        throw new IllegalArgumentException("BLOCK_END without BLOCK_START");
                    }
                    blocks.put(new Integer(argument),
                            new int[] { start.intValue(), i });
                } else if (command >= 0 && command > 127) {
                    throw new IllegalArgumentException("Invalid tone note");
                }
            }
            return new ToneProgram(commands, arguments, count, blocks);
        }
    }

    private static native int nCreateData(byte[] data, String contentType);
    private static native int nCreateLocator(String locator, String contentType);
    private static native boolean nStart(int handle);
    private static native boolean nStop(int handle);
    private static native void nClose(int handle);
    private static native void nSetLoopCount(int handle, int count);
    private static native void nSetVolume(int handle, int level);
    private static native void nSetMute(int handle, boolean muted);
    private static native long nSetMediaTime(int handle, long microseconds);
    private static native long nGetMediaTime(int handle);
    private static native long nGetDuration(int handle);
    private static native boolean nIsPlaying(int handle);
    private static native boolean nHasEnded(int handle);
    private static native boolean nHasError(int handle);
    private static native boolean nPlayTone(int note, int duration, int volume);
}
