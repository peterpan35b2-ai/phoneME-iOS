package corefixture;

import com.mascotcapsule.micro3d.v3.ActionTable;
import com.mascotcapsule.micro3d.v3.AffineTrans;
import com.mascotcapsule.micro3d.v3.Effect3D;
import com.mascotcapsule.micro3d.v3.Figure;
import com.mascotcapsule.micro3d.v3.FigureLayout;
import com.mascotcapsule.micro3d.v3.Graphics3D;
import com.mascotcapsule.micro3d.v3.Util3D;
import com.mascotcapsule.micro3d.v3.Vector3D;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public final class Micro3dOps {
    private Micro3dOps() {}

    public static int vectorOps() {
        Vector3D left = new Vector3D(2, 3, 4);
        Vector3D right = new Vector3D(5, 6, 7);
        int dot = left.innerProduct(right);
        Vector3D cross = Vector3D.outerProduct(left, right);
        return dot + cross.getX() * 100 + cross.getY() * 10 + cross.getZ();
    }

    public static int affineOps() {
        AffineTrans transform = new AffineTrans(
            4096, 0, 0, 10,
            0, 4096, 0, 20,
            0, 0, 4096, 30);
        Vector3D result = transform.transform(new Vector3D(1, 2, 3));
        return result.getX() * 10000 + result.getY() * 100 + result.getZ();
    }

    public static int rotationOps() {
        AffineTrans transform = new AffineTrans();
        transform.rotationZ(1024);
        Vector3D result = transform.transform(new Vector3D(4096, 0, 0));
        return result.getX() + result.getY();
    }

    public static int utilOps() {
        return Util3D.sqrt(65536) + Util3D.sin(1024) + Util3D.cos(0);
    }

    public static int rotationTranslationOps() {
        AffineTrans transform = new AffineTrans(
            4096, 0, 0, 10,
            0, 4096, 0, 20,
            0, 0, 4096, 30);
        transform.rotationZ(1024);
        Vector3D result = transform.transform(new Vector3D(4096, 0, 0));
        return result.getX() * 1000000 + result.getY() * 100 + result.getZ();
    }

    public static int resourceFormatOps() {
        byte[] mtra = new byte[30];
        mtra[0] = 'M';
        mtra[1] = 'T';
        mtra[2] = 2;
        mtra[4] = 1;
        mtra[28] = 3;
        ActionTable actions = new ActionTable(mtra);

        byte[] mbac = new byte[12];
        mbac[0] = 'M';
        mbac[1] = 'B';
        mbac[2] = 2;
        Figure figure = new Figure(mbac);

        return actions.getNumActions() * 1000000
            + actions.getNumFrames(0)
            + figure.getNumPattern() * 100
            + figure.getNumTextures();
    }

    public static int invalidFormatOps() {
        try {
            new ActionTable(new byte[] {0, 1, 2, 3});
            return -1;
        } catch (IllegalArgumentException expected) {
            return 1;
        }
    }

    public static int primitiveRenderOps() {
        Image image = Image.createImage(32, 32);
        Graphics target = image.getGraphics();
        target.setColor(0);
        target.fillRect(0, 0, 32, 32);

        Graphics3D graphics = new Graphics3D();
        FigureLayout layout = new FigureLayout();
        layout.setCenter(16, 16);
        layout.setScale(4096, 4096);
        Effect3D effect = new Effect3D();
        int[] coordinates = {
            -8, -8, 0,
             8, -8, 0,
             0,  8, 0
        };
        graphics.bind(target);
        graphics.renderPrimitives(
            null, 0, 0, layout, effect,
            Graphics3D.PRIMITVE_TRIANGLES |
                Graphics3D.PDATA_COLOR_PER_COMMAND,
            1, coordinates, new int[0], new int[0],
            new int[] {0x00ff0000});
        graphics.flush();
        graphics.release(target);

        int[] pixel = new int[1];
        image.getRGB(pixel, 0, 1, 16, 16, 1, 1);
        int red = (pixel[0] >>> 16) & 255;
        int green = (pixel[0] >>> 8) & 255;
        int blue = pixel[0] & 255;
        return red > 200 && green < 32 && blue < 32 ? 1 : 0;
    }

    public static int classAndConstantOps() {
        Graphics3D graphics = new Graphics3D();
        Effect3D effect = new Effect3D();
        boolean valid = graphics != null && effect != null
            && Graphics3D.COMMAND_END == 0x80000000
            && Graphics3D.COMMAND_LIST_VERSION_1_0 == 0xFE000001
            && Graphics3D.ENV_ATTR_LIGHTING == 1
            && Graphics3D.PATTR_BLEND_ADD == 64
            && Graphics3D.PDATA_NORMAL_PER_VERTEX == 768
            && Graphics3D.PRIMITVE_TRIANGLES == 0x03000000
            && Effect3D.NORMAL_SHADING == 0
            && Effect3D.TOON_SHADING == 1;
        return valid ? 77 : -1;
    }
}
