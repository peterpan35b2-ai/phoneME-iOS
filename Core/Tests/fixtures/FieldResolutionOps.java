package corefixture;

interface FieldResolutionConstants {
    String INHERITED_TEXT = "interface-field";
    int INHERITED_NUMBER = 73;
}

public final class FieldResolutionOps implements FieldResolutionConstants {
    private FieldResolutionOps() {}

    public static int inheritedInterfaceField() {
        return FieldResolutionOps.INHERITED_NUMBER +
               FieldResolutionOps.INHERITED_TEXT.length();
    }
}
