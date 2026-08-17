package corefixture;

import java.lang.reflect.Method;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Enumeration;
import java.util.Properties;
import java.util.zip.Inflater;
import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;

public final class CompatibilityApiOps {
    private CompatibilityApiOps() {
    }

    public static final class ReflectTarget {
        public int add(int left, int right) {
            return left + right;
        }
    }

    private static boolean isDigit(char value) {
        return value >= '0' && value <= '9';
    }

    private static boolean sameBytes(byte[] left, byte[] right) {
        if (left == null || right == null || left.length != right.length) {
            return false;
        }
        for (int index = 0; index < left.length; index++) {
            if (left[index] != right[index]) return false;
        }
        return true;
    }

    public static int run() throws Exception {
        int result = 0;

        Runtime runtime = Runtime.getRuntime();
        long maximum = runtime.maxMemory();
        if (maximum > 0L && maximum >= runtime.totalMemory()
                && runtime.freeMemory() <= maximum) {
            result |= 1;
        }
        if (Integer.bitCount(0xF0F0F0F0) == 16) {
            result |= 2;
        }

        Properties properties = new Properties();
        properties.setProperty("alpha", "1");
        properties.setProperty("beta", "2");
        Enumeration names = properties.propertyNames();
        int propertyCount = 0;
        while (names.hasMoreElements()) {
            Object name = names.nextElement();
            if ("alpha".equals(name) || "beta".equals(name)) {
                propertyCount++;
            }
        }
        if (propertyCount == 2) {
            result |= 4;
        }

        Method method = ReflectTarget.class.getMethod(
            "add", new Class[] {Integer.TYPE, Integer.TYPE});
        Object reflected = method.invoke(
            new ReflectTarget(),
            new Object[] {Integer.valueOf(7), Integer.valueOf(5)});
        if (reflected instanceof Integer && ((Integer)reflected).intValue() == 12) {
            result |= 8;
        }

        String formatted = new SimpleDateFormat("dd/MM/yyyy HH:mm")
            .format(new Date(0L));
        if (formatted.length() == 16
                && formatted.charAt(2) == '/'
                && formatted.charAt(5) == '/'
                && formatted.charAt(10) == ' '
                && formatted.charAt(13) == ':'
                && isDigit(formatted.charAt(0))
                && isDigit(formatted.charAt(15))) {
            result |= 16;
        }

        byte[] compressed = new byte[] {
            120, (byte)156, 43, (byte)200, (byte)200, (byte)207,
            75, (byte)245, 117, 85, (byte)200, (byte)204,
            75, (byte)203, 73, 44, 73, 45, 82, 40,
            (byte)207, 47, (byte)202, 46, 6, 0, 95, (byte)205, 8, 120
        };
        Inflater inflater = new Inflater();
        inflater.setInput(compressed);
        byte[] inflated = new byte[64];
        int inflatedCount = inflater.inflate(inflated);
        inflater.end();
        byte[] expectedInflated = "phoneME inflater works".getBytes("UTF-8");
        boolean inflatedMatches = inflatedCount == expectedInflated.length;
        for (int index = 0; inflatedMatches && index < inflatedCount; index++) {
            inflatedMatches = inflated[index] == expectedInflated[index];
        }
        if (inflatedMatches) {
            result |= 32;
        }

        byte[] keyBytes = new byte[] {
            0, 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15
        };
        byte[] plaintext = new byte[] {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            (byte)0x88, (byte)0x99, (byte)0xAA, (byte)0xBB,
            (byte)0xCC, (byte)0xDD, (byte)0xEE, (byte)0xFF
        };
        byte[] knownFirstBlock = new byte[] {
            0x69, (byte)0xC4, (byte)0xE0, (byte)0xD8,
            0x6A, 0x7B, 0x04, 0x30,
            (byte)0xD8, (byte)0xCD, (byte)0xB7, (byte)0x80,
            0x70, (byte)0xB4, (byte)0xC5, 0x5A
        };
        SecretKeySpec key = new SecretKeySpec(keyBytes, "AES");
        Cipher encrypt = Cipher.getInstance("AES/ECB/PKCS5Padding");
        encrypt.init(Cipher.ENCRYPT_MODE, key);
        byte[] encrypted = encrypt.doFinal(plaintext);
        boolean knownBlockMatches = encrypted.length == 32;
        for (int index = 0; knownBlockMatches && index < 16; index++) {
            knownBlockMatches = encrypted[index] == knownFirstBlock[index];
        }
        Cipher decrypt = Cipher.getInstance("AES/ECB/PKCS7Padding");
        decrypt.init(Cipher.DECRYPT_MODE, key);
        byte[] decrypted = decrypt.doFinal(encrypted);
        if (knownBlockMatches && sameBytes(plaintext, decrypted)
                && "AES".equals(key.getAlgorithm())
                && "RAW".equals(key.getFormat())
                && sameBytes(keyBytes, key.getEncoded())) {
            result |= 64;
        }

        return result;
    }
}
