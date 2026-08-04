package corefixture;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import javax.microedition.media.Control;
import javax.microedition.media.Manager;
import javax.microedition.media.MediaException;
import javax.microedition.media.Player;
import javax.microedition.media.PlayerListener;
import javax.microedition.media.TimeBase;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;
import javax.microedition.media.protocol.ContentDescriptor;
import javax.microedition.media.protocol.DataSource;
import javax.microedition.media.protocol.SourceStream;

public final class MediaOps implements PlayerListener {
    private static int events;

    private static final class MemorySource extends DataSource {
        private final MemoryStream stream;
        private boolean connected;
        private boolean started;

        MemorySource(byte[] data) {
            super("memory://fixture.wav");
            stream = new MemoryStream(data);
        }

        public String getContentType() { return "audio/x-wav"; }
        public void connect() { connected = true; }
        public void disconnect() { connected = false; }
        public void start() throws IOException {
            if (!connected) throw new IOException("not connected");
            started = true;
        }
        public void stop() { started = false; }
        public SourceStream[] getStreams() {
            return new SourceStream[] {stream};
        }
        public Control[] getControls() { return new Control[0]; }
        public Control getControl(String type) { return null; }
    }

    private static final class MemoryStream implements SourceStream {
        private final byte[] data;
        private int position;

        MemoryStream(byte[] data) { this.data = data; }
        public ContentDescriptor getContentDescriptor() {
            return new ContentDescriptor("audio/x-wav");
        }
        public long getContentLength() { return data.length; }
        public int read(byte[] buffer, int offset, int length) {
            if (position >= data.length) return -1;
            int count = Math.min(length, data.length - position);
            System.arraycopy(data, position, buffer, offset, count);
            position += count;
            return count;
        }
        public int getTransferSize() { return 512; }
        public long seek(long where) {
            if (where < 0) where = 0;
            if (where > data.length) where = data.length;
            position = (int)where;
            return position;
        }
        public long tell() { return position; }
        public int getSeekType() { return RANDOM_ACCESSIBLE; }
        public Control[] getControls() { return new Control[0]; }
        public Control getControl(String type) { return null; }
    }

    private static byte[] oneSampleWave() {
        byte[] wave = new byte[45];
        wave[0] = 'R'; wave[1] = 'I'; wave[2] = 'F'; wave[3] = 'F';
        wave[4] = 37;
        wave[8] = 'W'; wave[9] = 'A'; wave[10] = 'V'; wave[11] = 'E';
        wave[12] = 'f'; wave[13] = 'm'; wave[14] = 't'; wave[15] = ' ';
        wave[16] = 16;
        wave[20] = 1; wave[22] = 1;
        wave[24] = 64; wave[25] = 31;
        wave[28] = 64; wave[29] = 31;
        wave[32] = 1; wave[34] = 8;
        wave[36] = 'd'; wave[37] = 'a'; wave[38] = 't'; wave[39] = 'a';
        wave[40] = 1;
        wave[44] = (byte)128;
        return wave;
    }

    private static int dataSourceCompatibility() throws Exception {
        int stage = 0;
        try {
            stage = 1;
            ContentDescriptor descriptor = new ContentDescriptor("audio/x-wav");
            if (!"audio/x-wav".equals(descriptor.getContentType())) return -16;
            try {
                new ContentDescriptor(null);
                return -17;
            } catch (IllegalArgumentException expected) {
            }
            stage = 2;
            MemorySource source = new MemorySource(oneSampleWave());
            if (!"memory://fixture.wav".equals(source.getLocator())) return -18;
            stage = 3;
            Player streamed = Manager.createPlayer(source);
            if (streamed == null || source.connected || source.started) return -19;
            stage = 4;
            streamed.realize();
            if (!"audio/x-wav".equals(streamed.getContentType())) return -20;
            stage = 5;
            streamed.close();
            return 0;
        } catch (IllegalStateException failure) {
            return -100 - stage;
        }
    }

    public void playerUpdate(Player player, String event, Object eventData) {
        if (PlayerListener.STARTED.equals(event)) {
            events |= 1;
        } else if (PlayerListener.STOPPED.equals(event)) {
            events |= 2;
        } else if (PlayerListener.CLOSED.equals(event)) {
            events |= 4;
        } else if (PlayerListener.VOLUME_CHANGED.equals(event)) {
            events |= 8;
        }
    }

    public static int run() throws Exception {
        String[] types = Manager.getSupportedContentTypes("device");
        String[] protocols = Manager.getSupportedProtocols("audio/x-tone-seq");
        if (types.length == 0 || protocols.length == 0) {
            return -1;
        }

        TimeBase timeBase = Manager.getSystemTimeBase();
        if (timeBase == null || timeBase.getTime() <= 0L) {
            return -2;
        }

        Player player = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
        if (player == null || player.getState() != Player.UNREALIZED) {
            return -3;
        }

        MediaOps listener = new MediaOps();
        player.addPlayerListener(listener);
        player.realize();
        if (player.getState() != Player.REALIZED) {
            return -4;
        }
        if (!"audio/x-tone-seq".equals(player.getContentType())) {
            return -5;
        }

        Control volumeControl = player.getControl("VolumeControl");
        Control toneControl = player.getControl(
                "javax.microedition.media.control.ToneControl");
        if (!(volumeControl instanceof VolumeControl) ||
                !(toneControl instanceof ToneControl)) {
            return -6;
        }

        VolumeControl volume = (VolumeControl)volumeControl;
        ToneControl tone = (ToneControl)toneControl;
        if (volume.setLevel(42) != 42 || volume.getLevel() != 42) {
            return -7;
        }
        volume.setMute(true);
        if (!volume.isMuted()) {
            return -8;
        }
        volume.setMute(false);

        tone.setSequence(new byte[] {
            ToneControl.VERSION, 1,
            ToneControl.TEMPO, 30,
            ToneControl.RESOLUTION, 64,
            69, 8,
            ToneControl.SILENCE, 4,
            ToneControl.SET_VOLUME, 50,
            72, 8
        });

        player.setLoopCount(2);
        player.prefetch();
        if (player.getState() != Player.PREFETCHED) {
            return -9;
        }
        if (player.getControls().length != 2) {
            return -10;
        }

        player.start();
        if (player.getState() != Player.STARTED) {
            return -11;
        }
        if (player.getDuration() <= 0L) {
            return -12;
        }
        player.stop();
        if (player.getState() != Player.PREFETCHED) {
            return -13;
        }
        player.close();
        if (player.getState() != Player.CLOSED) {
            return -14;
        }

        Manager.playTone(69, 1, 25);

        try {
            Manager.createPlayer(
                new ByteArrayInputStream(new byte[0]), "audio/midi");
            return -15;
        } catch (MediaException expected) {
            // phoneME reports malformed/empty stream payloads as MediaException.
        }
        int dataSourceResult = dataSourceCompatibility();
        if (dataSourceResult != 0) return dataSourceResult;
        return events;
    }
}
