import javax.microedition.midlet.MIDlet;

import main.GameMidlet;

/** Exercises an array reference kept below an invokestatic return value. */
public final class LP64InvokeArrayProbe extends MIDlet {
    private static final String[] OWN = { "OWN_OK" };

    private static int zero() {
        return 0;
    }

    private static int zeroWithArg(String ignored) {
        return 0;
    }

    private static int collectWithArg(String ignored) {
        for (int i = 0; i < 256; i++) {
            byte[] pressure = new byte[4096];
            pressure[0] = (byte) i;
        }
        System.gc();
        return 0;
    }

    protected void startApp() {
        try {
            String own = OWN[zero()];
            System.out.println("LP64_OWN_ARRAY=" + own);

            String ownArg = OWN[zeroWithArg("x")];
            System.out.println("LP64_OWN_ARG_ARRAY=" + ownArg);

            String[] local = { "LOCAL_OK" };
            String localGc = local[collectWithArg("x")];
            System.out.println("LP64_LOCAL_GC_ARRAY=" + localGc);

            GameMidlet.d();
            String game = GameMidlet.l[w.c("indServer")];
            System.out.println("LP64_GAME_ARRAY=" + game);
            System.out.println("LP64_INVOKE_ARRAY_OK");
        } catch (Throwable t) {
            System.out.println("LP64_INVOKE_ARRAY_FAILED: " + t);
            t.printStackTrace();
        } finally {
            notifyDestroyed();
        }
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}
