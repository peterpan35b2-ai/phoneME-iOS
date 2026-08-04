package compat.diff;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Classifies native handler signatures against the exact OpenJDK 8 runtime
 * selected by test-vm-differential.sh. It does not invoke methods; semantic
 * execution coverage is supplied by VmDifferentialHarness telemetry.
 */
public final class OpenJdk8SignatureOracle {
    private OpenJdk8SignatureOracle() {}

    private static final class DescriptorCursor {
        private final String descriptor;
        private int index;

        DescriptorCursor(String descriptor) {
            this.descriptor = descriptor;
        }

        boolean atEnd() {
            return index == descriptor.length();
        }

        char current() {
            return descriptor.charAt(index);
        }

        void expect(char expected) {
            if (atEnd() || current() != expected) {
                throw new IllegalArgumentException("Expected '" + expected
                    + "' in descriptor " + descriptor + " at " + index);
            }
            index++;
        }

        Class<?> parseType(boolean allowVoid) throws ClassNotFoundException {
            if (atEnd()) {
                throw new IllegalArgumentException("Truncated descriptor: " + descriptor);
            }
            char kind = descriptor.charAt(index++);
            switch (kind) {
                case 'V':
                    if (!allowVoid) {
                        throw new IllegalArgumentException("void parameter in " + descriptor);
                    }
                    return Void.TYPE;
                case 'Z': return Boolean.TYPE;
                case 'B': return Byte.TYPE;
                case 'C': return Character.TYPE;
                case 'S': return Short.TYPE;
                case 'I': return Integer.TYPE;
                case 'J': return Long.TYPE;
                case 'F': return Float.TYPE;
                case 'D': return Double.TYPE;
                case 'L': {
                    int end = descriptor.indexOf(';', index);
                    if (end < 0) {
                        throw new IllegalArgumentException("Unterminated object type: " + descriptor);
                    }
                    String name = descriptor.substring(index, end).replace('/', '.');
                    index = end + 1;
                    return Class.forName(name, false,
                        OpenJdk8SignatureOracle.class.getClassLoader());
                }
                case '[': {
                    int start = index - 1;
                    while (!atEnd() && current() == '[') {
                        index++;
                    }
                    if (atEnd()) {
                        throw new IllegalArgumentException("Truncated array type: " + descriptor);
                    }
                    if (current() == 'L') {
                        int end = descriptor.indexOf(';', index + 1);
                        if (end < 0) {
                            throw new IllegalArgumentException("Unterminated array type: " + descriptor);
                        }
                        index = end + 1;
                    } else {
                        index++;
                    }
                    String binary = descriptor.substring(start, index).replace('/', '.');
                    return Class.forName(binary, false,
                        OpenJdk8SignatureOracle.class.getClassLoader());
                }
                default:
                    throw new IllegalArgumentException("Unknown descriptor kind '" + kind
                        + "' in " + descriptor);
            }
        }
    }

    private static final class ParsedDescriptor {
        final Class<?>[] parameters;
        final Class<?> returnType;

        ParsedDescriptor(Class<?>[] parameters, Class<?> returnType) {
            this.parameters = parameters;
            this.returnType = returnType;
        }
    }

    private static ParsedDescriptor parseMethodDescriptor(String descriptor)
            throws ClassNotFoundException {
        DescriptorCursor cursor = new DescriptorCursor(descriptor);
        cursor.expect('(');
        List<Class<?>> parameters = new ArrayList<Class<?>>();
        while (cursor.current() != ')') {
            parameters.add(cursor.parseType(false));
        }
        cursor.expect(')');
        Class<?> returnType = cursor.parseType(true);
        if (!cursor.atEnd()) {
            throw new IllegalArgumentException("Trailing descriptor data: " + descriptor);
        }
        return new ParsedDescriptor(
            parameters.toArray(new Class<?>[parameters.size()]), returnType);
    }

    private static Method findDeclaredOrInherited(
            Class<?> owner,
            String name,
            Class<?>[] parameters,
            Class<?> returnType,
            Set<Class<?>> visited) {
        if (owner == null || !visited.add(owner)) {
            return null;
        }
        try {
            Method method = owner.getDeclaredMethod(name, parameters);
            if (method.getReturnType().equals(returnType)) {
                return method;
            }
        } catch (NoSuchMethodException ignored) {
            // Continue through the hierarchy.
        }
        Class<?>[] interfaces = owner.getInterfaces();
        for (int index = 0; index < interfaces.length; index++) {
            Method method = findDeclaredOrInherited(
                interfaces[index], name, parameters, returnType, visited);
            if (method != null) {
                return method;
            }
        }
        return findDeclaredOrInherited(
            owner.getSuperclass(), name, parameters, returnType, visited);
    }

    private static String[] classify(String ownerName,
                                     String methodName,
                                     String descriptor) {
        if (!ownerName.startsWith("java/")) {
            return new String[] {"NON_OPENJDK_NAMESPACE", ""};
        }
        String binaryOwner = ownerName.replace('/', '.');
        final Class<?> owner;
        try {
            owner = Class.forName(binaryOwner, false,
                OpenJdk8SignatureOracle.class.getClassLoader());
        } catch (Throwable failure) {
            return new String[] {"MISSING_CLASS", failure.getClass().getName()};
        }
        if ("<clinit>".equals(methodName)) {
            return new String[] {"OPENJDK8_CLASS_INITIALIZER", owner.getName()};
        }
        final ParsedDescriptor parsed;
        try {
            parsed = parseMethodDescriptor(descriptor);
        } catch (Throwable failure) {
            return new String[] {"UNRESOLVED_DESCRIPTOR", failure.getClass().getName()
                + ":" + String.valueOf(failure.getMessage())};
        }
        if ("<init>".equals(methodName)) {
            if (!Void.TYPE.equals(parsed.returnType)) {
                return new String[] {"INVALID_CONSTRUCTOR_DESCRIPTOR", owner.getName()};
            }
            try {
                Constructor<?> constructor = owner.getDeclaredConstructor(parsed.parameters);
                return new String[] {"OPENJDK8_DECLARED", constructor.getDeclaringClass().getName()};
            } catch (NoSuchMethodException failure) {
                return new String[] {"MISSING_MEMBER", owner.getName()};
            }
        }
        Method method = findDeclaredOrInherited(owner, methodName, parsed.parameters,
            parsed.returnType, new HashSet<Class<?>>());
        if (method == null) {
            return new String[] {"MISSING_MEMBER", owner.getName()};
        }
        String status = method.getDeclaringClass().equals(owner)
            ? "OPENJDK8_DECLARED" : "OPENJDK8_INHERITED";
        return new String[] {status, method.getDeclaringClass().getName()};
    }

    public static void main(String[] arguments) throws Exception {
        if (arguments.length != 2) {
            System.err.println("usage: OpenJdk8SignatureOracle COVERAGE.tsv OUTPUT.tsv");
            System.exit(2);
        }
        BufferedReader input = new BufferedReader(new InputStreamReader(
            new FileInputStream(arguments[0]), "UTF-8"));
        PrintWriter output = new PrintWriter(arguments[1], "UTF-8");
        try {
            String header = input.readLine();
            if (!"owner\tname\tdescriptor\tinvocations".equals(header)) {
                throw new IllegalArgumentException("Unexpected coverage header: " + header);
            }
            output.println("owner\tname\tdescriptor\tinvocations\tstatus\tdeclaring_class");
            String line;
            while ((line = input.readLine()) != null) {
                if (line.length() == 0) continue;
                String[] fields = line.split("\\t", -1);
                if (fields.length != 4) {
                    throw new IllegalArgumentException("Malformed coverage row: " + line);
                }
                String[] classification = classify(fields[0], fields[1], fields[2]);
                output.println(fields[0] + "\t" + fields[1] + "\t" + fields[2]
                    + "\t" + fields[3] + "\t" + classification[0]
                    + "\t" + classification[1].replace('\t', ' '));
            }
        } finally {
            input.close();
            output.close();
        }
    }
}
