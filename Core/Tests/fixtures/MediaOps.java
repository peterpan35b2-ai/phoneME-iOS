package corefixture;

import java.io.ByteArrayInputStream;
import javax.microedition.media.Control;
import javax.microedition.media.Manager;
import javax.microedition.media.MediaException;
import javax.microedition.media.Player;
import javax.microedition.media.PlayerListener;
import javax.microedition.media.TimeBase;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

public final class MediaOps implements PlayerListener {
    private static int events;

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
        return events;
    }
}
