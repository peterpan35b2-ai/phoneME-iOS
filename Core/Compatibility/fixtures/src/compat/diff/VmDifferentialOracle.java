package compat.diff;

public final class VmDifferentialOracle {
    private interface IntCall {
        int run();
    }

    private interface LongCall {
        long run();
    }

    private interface StringCall {
        String run();
    }

    private interface ThrowingCall {
        void run() throws Throwable;
    }

    private VmDifferentialOracle() {
    }

    private static void emitInt(String id, IntCall call) {
        System.out.println(id + "\tI\t" + call.run());
    }

    private static void emitLong(String id, LongCall call) {
        System.out.println(id + "\tJ\t" + call.run());
    }

    private static void emitString(String id, StringCall call) {
        System.out.println(id + "\tS\t" + call.run());
    }

    private static void emitException(String id, ThrowingCall call) {
        try {
            call.run();
            System.out.println(id + "\tE\t<none>");
        } catch (Throwable error) {
            System.out.println(id + "\tE\t"
                    + error.getClass().getName().replace('.', '/'));
        }
    }

    public static void main(String[] arguments) {
        emitInt("int-overflow", new IntCall() {
            public int run() { return VmDifferentialOps.intOverflow(); }
        });
        emitInt("int-div-rem", new IntCall() {
            public int run() { return VmDifferentialOps.intDivisionRemainder(); }
        });
        emitInt("int-shift-mask", new IntCall() {
            public int run() { return VmDifferentialOps.intShiftMasking(); }
        });
        emitLong("long-overflow", new LongCall() {
            public long run() { return VmDifferentialOps.longOverflow(); }
        });
        emitLong("long-shift-mask", new LongCall() {
            public long run() { return VmDifferentialOps.longShiftMasking(); }
        });
        emitInt("float-nan-bits", new IntCall() {
            public int run() { return VmDifferentialOps.floatNanBits(); }
        });
        emitInt("float-negative-zero", new IntCall() {
            public int run() { return VmDifferentialOps.floatNegativeZeroBits(); }
        });
        emitLong("double-nan-bits", new LongCall() {
            public long run() { return VmDifferentialOps.doubleNanBits(); }
        });
        emitLong("double-negative-zero", new LongCall() {
            public long run() { return VmDifferentialOps.doubleNegativeZeroBits(); }
        });
        emitInt("floating-conversions", new IntCall() {
            public int run() { return VmDifferentialOps.floatingConversions(); }
        });
        emitLong("double-long-conversions", new LongCall() {
            public long run() { return VmDifferentialOps.doubleToLongConversions(); }
        });
        emitInt("dense-switch", new IntCall() {
            public int run() { return VmDifferentialOps.denseSwitch(); }
        });
        emitInt("sparse-switch", new IntCall() {
            public int run() { return VmDifferentialOps.sparseSwitch(); }
        });
        emitInt("primitive-arrays", new IntCall() {
            public int run() { return VmDifferentialOps.primitiveArrays(); }
        });
        emitInt("multi-array", new IntCall() {
            public int run() { return VmDifferentialOps.multiArray(); }
        });
        emitInt("reference-arrays-casts", new IntCall() {
            public int run() { return VmDifferentialOps.referenceArraysAndCasts(); }
        });
        emitInt("exception-finally", new IntCall() {
            public int run() { return VmDifferentialOps.exceptionAndFinally(); }
        });
        emitInt("dispatch", new IntCall() {
            public int run() { return VmDifferentialOps.dispatch(); }
        });
        emitInt("class-initialization", new IntCall() {
            public int run() { return VmDifferentialOps.classInitialization(); }
        });
        emitInt("unicode-string", new IntCall() {
            public int run() { return VmDifferentialOps.unicodeString(); }
        });
        emitInt("string-operations", new IntCall() {
            public int run() { return VmDifferentialOps.stringOperations(); }
        });
        emitInt("string-buffer", new IntCall() {
            public int run() { return VmDifferentialOps.stringBufferOperations(); }
        });
        emitInt("vector", new IntCall() {
            public int run() { return VmDifferentialOps.vectorOperations(); }
        });
        emitInt("hashtable", new IntCall() {
            public int run() { return VmDifferentialOps.hashtableOperations(); }
        });
        emitInt("tokenizer", new IntCall() {
            public int run() { return VmDifferentialOps.tokenizerOperations(); }
        });
        emitInt("tokenizer-delimiter-change", new IntCall() {
            public int run() { return VmDifferentialOps.tokenizerDelimiterChange(); }
        });
        emitInt("tokenizer-return-delimiters", new IntCall() {
            public int run() { return VmDifferentialOps.tokenizerReturnDelimiters(); }
        });
        emitException("tokenizer-exhaustion", new ThrowingCall() {
            public void run() { VmDifferentialOps.uncaughtTokenizerExhaustion(); }
        });
        emitLong("data-stream", new LongCall() {
            public long run() { return VmDifferentialOps.dataStreamRoundTrip(); }
        });
        emitInt("random", new IntCall() {
            public int run() { return VmDifferentialOps.randomSequence(); }
        });
        emitInt("system-arraycopy", new IntCall() {
            public int run() { return VmDifferentialOps.systemArrayCopy(); }
        });
        emitInt("wrapper-semantics", new IntCall() {
            public int run() { return VmDifferentialOps.wrapperSemantics(); }
        });
        emitLong("math-semantics", new LongCall() {
            public long run() { return VmDifferentialOps.mathSemantics(); }
        });
        emitInt("stack", new IntCall() {
            public int run() { return VmDifferentialOps.stackOperations(); }
        });
        emitInt("enumeration", new IntCall() {
            public int run() { return VmDifferentialOps.enumerationOperations(); }
        });
        emitLong("date", new LongCall() {
            public long run() { return VmDifferentialOps.dateOperations(); }
        });
        emitInt("calendar-utc", new IntCall() {
            public int run() { return VmDifferentialOps.calendarUtcOperations(); }
        });
        emitInt("timezone", new IntCall() {
            public int run() { return VmDifferentialOps.timeZoneOperations(); }
        });
        emitInt("modified-utf", new IntCall() {
            public int run() { return VmDifferentialOps.modifiedUtfRoundTrip(); }
        });
        emitInt("reader-writer", new IntCall() {
            public int run() { return VmDifferentialOps.readerWriterRoundTrip(); }
        });
        emitInt("class-semantics", new IntCall() {
            public int run() { return VmDifferentialOps.classSemantics(); }
        });
        emitInt("thread-runnable-join", new IntCall() {
            public int run() { return VmDifferentialOps.runnableThreadJoin(); }
        });
        emitInt("thread-synchronized", new IntCall() {
            public int run() { return VmDifferentialOps.synchronizedThreadCounters(); }
        });
        emitString("exception-constructors", new StringCall() {
            public String run() { return VmDifferentialOps.exceptionConstructorSurfaceTrace(); }
        });
        emitString("print-writer-surface", new StringCall() {
            public String run() { return VmDifferentialOps.printWriterSurfaceTrace(); }
        });
        emitString("reader-input-surface", new StringCall() {
            public String run() { return VmDifferentialOps.readerInputSurfaceTrace(); }
        });
        emitString("throwable-thread-permission-file", new StringCall() {
            public String run() { return VmDifferentialOps.throwableThreadPermissionFileTrace(); }
        });
        emitString("legacy-util-full", new StringCall() {
            public String run() { return VmDifferentialOps.legacyUtilFullTrace(); }
        });
        emitString("local-time", new StringCall() {
            public String run() { return VmDifferentialOps.localTimeTrace(); }
        });
        emitString("wrapper-full", new StringCall() {
            public String run() { return VmDifferentialOps.wrapperFullTrace(); }
        });
        emitString("math-full", new StringCall() {
            public String run() { return VmDifferentialOps.mathFullTrace(); }
        });
        emitString("string-api", new StringCall() {
            public String run() { return VmDifferentialOps.stringApiTrace(); }
        });
        emitString("string-builder-api", new StringCall() {
            public String run() { return VmDifferentialOps.stringBuilderTrace(); }
        });
        emitString("string-buffer-api", new StringCall() {
            public String run() { return VmDifferentialOps.stringBufferExtendedTrace(); }
        });
        emitString("headless-collections", new StringCall() {
            public String run() { return VmDifferentialOps.headlessCollectionsTrace(); }
        });
        emitString("headless-arrays", new StringCall() {
            public String run() { return VmDifferentialOps.headlessArraysTrace(); }
        });
        emitString("headless-base64-objects", new StringCall() {
            public String run() { return VmDifferentialOps.headlessBase64ObjectsTrace(); }
        });
        emitString("headless-io", new StringCall() {
            public String run() { return VmDifferentialOps.headlessIoTrace(); }
        });
        emitException("exception-npe", new ThrowingCall() {
            public void run() { VmDifferentialOps.uncaughtNullPointer(); }
        });
        emitException("exception-array-bounds", new ThrowingCall() {
            public void run() { VmDifferentialOps.uncaughtArrayBounds(); }
        });
        emitException("exception-class-cast", new ThrowingCall() {
            public void run() { VmDifferentialOps.uncaughtClassCast(); }
        });
        emitException("exception-arithmetic", new ThrowingCall() {
            public void run() { VmDifferentialOps.uncaughtArithmetic(); }
        });
        emitException("broken-init-first", new ThrowingCall() {
            public void run() { VmDifferentialOps.brokenInitializationFirst(); }
        });
        emitException("broken-init-second", new ThrowingCall() {
            public void run() { VmDifferentialOps.brokenInitializationSecond(); }
        });
    }
}
