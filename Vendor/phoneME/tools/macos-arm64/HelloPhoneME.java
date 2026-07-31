public final class HelloPhoneME {
    public static void main(String[] args) {
        Object marker = new Object();
        int value = 40 + 2;
        System.out.println("phoneME native arm64: " + value);
        System.out.println(marker != null ? "object-ok" : "object-failed");
    }
}
