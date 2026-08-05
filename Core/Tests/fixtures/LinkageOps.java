package corefixture;

public final class LinkageOps {
    private static Object nullable;

    private interface Attempt {
        void run();
    }

    private static String describe(Throwable error) {
        return error.getClass().getName() + ":" + error.getMessage();
    }

    private static String repeat(Attempt attempt) {
        String first;
        String second;
        try {
            attempt.run();
            first = "NO_ERROR";
        } catch (Throwable error) {
            first = describe(error);
        }
        try {
            attempt.run();
            second = "NO_ERROR";
        } catch (Throwable error) {
            second = describe(error);
        }
        return first + "|" + second;
    }

    public static String missingClassTrace() {
        return repeat(new Attempt() {
            public void run() {
                new LinkageMissing();
            }
        });
    }

    public static String missingArrayTrace() {
        return repeat(new Attempt() {
            public void run() {
                LinkageMissing[] ignored = new LinkageMissing[1];
                if (ignored.length == Integer.MIN_VALUE) {
                    throw new AssertionError();
                }
            }
        });
    }

    public static String missingTypeTrace() {
        return repeat(new Attempt() {
            public void run() {
                LinkageMissing ignored = (LinkageMissing) nullable;
                if (ignored != null) {
                    throw new AssertionError();
                }
            }
        });
    }

    public static String missingInstanceofTrace() {
        return repeat(new Attempt() {
            public void run() {
                if (nullable instanceof LinkageMissing) {
                    throw new AssertionError();
                }
            }
        });
    }

    public static String missingMultiArrayTrace() {
        return repeat(new Attempt() {
            public void run() {
                LinkageMissing[][] ignored = new LinkageMissing[1][1];
                if (ignored.length == Integer.MIN_VALUE) {
                    throw new AssertionError();
                }
            }
        });
    }

    public static String missingMethodTrace() {
        return repeat(new Attempt() {
            public void run() {
                LinkageTarget.call();
            }
        });
    }

    public static String missingFieldTrace() {
        return repeat(new Attempt() {
            public void run() {
                int ignored = LinkageTarget.value;
                if (ignored == Integer.MIN_VALUE) {
                    throw new AssertionError();
                }
            }
        });
    }
}
