package compat.javame;

import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

/**
 * Independent oracle for the CLDC Calendar/DateField operations exercised by
 * JavaMeDifferentialMIDlet. The phoneME ARM64 reference build currently used
 * by the test harness has a broken long comparison path: its own
 * long-cutover-compare health probe reports 0 for 0L >= -12219292800000L.
 * This oracle is used only when that defect is detected at runtime.
 */
public final class JavaMeCalendarOracle {
    private static final TimeZone GMT = TimeZone.getTimeZone("GMT");

    private JavaMeCalendarOracle() {
    }

    private static void emitInt(String id, int value) {
        System.out.println("JME_DIFF\t" + id + "\tI\t" + value);
    }

    private static void emitLong(String id, long value) {
        System.out.println("JME_DIFF\t" + id + "\tJ\t" + value);
    }

    private static Calendar setDateTime(long value) {
        Calendar calendar = Calendar.getInstance(GMT);
        calendar.setTime(new Date(value));
        // MIDP DateField always discards seconds and milliseconds.
        calendar.set(Calendar.SECOND, 0);
        calendar.set(Calendar.MILLISECOND, 0);
        return calendar;
    }

    private static long dateAfterSet() {
        return setDateTime(123456789L).getTime().getTime();
    }

    private static long dateAfterTimeMode() {
        Calendar calendar = setDateTime(123456789L);
        // MIDP TIME mode moves the existing wall-clock time onto the local
        // zero-epoch date while preserving hour/minute values.
        calendar.set(Calendar.YEAR, 1970);
        calendar.set(Calendar.MONTH, Calendar.JANUARY);
        calendar.set(Calendar.DATE, 1);
        return calendar.getTime().getTime();
    }

    private static long dateFieldSemantics() {
        long result = dateAfterSet();
        result = result * 31L + 3L; // DateField.DATE_TIME
        result = result * 31L + 2L; // DateField.TIME
        result = result * 31L + dateAfterTimeMode();
        return result;
    }

    private static long calendarTimeReset() {
        Calendar calendar = Calendar.getInstance(GMT);
        calendar.setTime(new Date(123456789L));
        calendar.set(Calendar.SECOND, 0);
        calendar.set(Calendar.MILLISECOND, 0);
        calendar.set(Calendar.YEAR, 1970);
        calendar.set(Calendar.MONTH, Calendar.JANUARY);
        calendar.set(Calendar.DATE, 1);
        return calendar.getTime().getTime();
    }

    public static void main(String[] arguments) {
        emitLong("date-field", dateFieldSemantics());
        emitLong("date-after-set", dateAfterSet());
        emitLong("date-after-time-mode", dateAfterTimeMode());
        emitLong("calendar-time-reset", calendarTimeReset());
        emitInt("long-cutover-compare",
                0L >= -12219292800000L ? 1 : 0);
    }
}
