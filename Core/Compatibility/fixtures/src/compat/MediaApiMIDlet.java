package compat;

import javax.microedition.media.Manager;
import javax.microedition.media.Player;
import javax.microedition.media.control.VolumeControl;
import javax.microedition.midlet.MIDlet;

public final class MediaApiMIDlet extends MIDlet {
    private Player player;

    protected void startApp() {
        try {
            player = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
            player.realize();
            player.prefetch();
            VolumeControl volume = (VolumeControl) player.getControl("VolumeControl");
            if (volume != null) {
                volume.setLevel(40);
            }
            player.start();
            Manager.playTone(69, 80, 40);
            System.out.println("COMPAT_MEDIA:tone-start");
            System.out.println("COMPAT_MILESTONE:media-started");
        } catch (Exception error) {
            throw new RuntimeException(error.toString());
        }
    }

    protected void pauseApp() {
        try {
            if (player != null) {
                player.stop();
            }
        } catch (Exception ignored) {
        }
    }

    protected void destroyApp(boolean unconditional) {
        if (player != null) {
            player.close();
            player = null;
        }
        System.out.println("COMPAT_MILESTONE:media-destroy");
    }
}
