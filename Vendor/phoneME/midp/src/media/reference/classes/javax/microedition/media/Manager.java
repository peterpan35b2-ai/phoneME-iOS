/*
 * Copyright 1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * iOS backend implementation for phoneME.
 */
package javax.microedition.media;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import javax.microedition.io.Connector;
import javax.microedition.io.HttpConnection;
import javax.microedition.media.protocol.DataSource;
import javax.microedition.media.protocol.SourceStream;

/** Entry point for the JSR-135 media implementation used by phoneME on iOS. */
public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone";
    public static final String MIDI_DEVICE_LOCATOR = "device://midi";

    private static final String[] CONTENT_TYPES = {
        "audio/mpeg",
        "audio/mp3",
        "audio/x-wav",
        "audio/wav",
        "audio/basic",
        "audio/aac",
        "audio/mp4",
        "audio/x-m4a",
        "audio/midi",
        "audio/x-midi",
        "audio/sp-midi",
        "audio/amr",
        "audio/amr-wb",
        "audio/x-tone-seq"
    };

    private static final String[] PROTOCOLS = {
        "file", "http", "https", "resource", "device"
    };

    private static final int MAX_BUFFERED_REMOTE_MEDIA_BYTES = 32 * 1024 * 1024;
    private static final int MAX_HTTP_REDIRECTS = 5;

    private static final TimeBase SYSTEM_TIME_BASE = new TimeBase() {
        private long lastTime;

        public synchronized long getTime() {
            long now = System.currentTimeMillis() * 1000L;
            if (now < lastTime) {
                now = lastTime;
            }
            lastTime = now;
            return now;
        }
    };

    private Manager() {
    }

    public static String[] getSupportedContentTypes(String protocol) {
        if (protocol != null && !isSupportedProtocol(protocol)) {
            return new String[0];
        }
        return copy(CONTENT_TYPES);
    }

    public static String[] getSupportedProtocols(String contentType) {
        if (contentType != null && !IOSPlayer.isSupportedContentType(contentType)) {
            return new String[0];
        }
        return copy(PROTOCOLS);
    }

    public static Player createPlayer(String locator)
            throws IOException, MediaException {
        if (locator == null) {
            throw new IllegalArgumentException("locator is null");
        }

        if (locator.startsWith("resource:")) {
            String path = locator.substring("resource:".length());
            if (path.length() == 0) {
                throw new MediaException("Empty resource locator");
            }
            if (path.charAt(0) != '/') {
                path = "/" + path;
            }
            InputStream stream = Manager.class.getResourceAsStream(path);
            if (stream == null) {
                throw new IOException("Resource not found: " + path);
            }
            try {
                return createPlayer(stream, typeFromName(path));
            } finally {
                try {
                    stream.close();
                } catch (IOException ignored) {
                }
            }
        }

        if (TONE_DEVICE_LOCATOR.equals(locator)) {
            return IOSPlayer.createTonePlayer();
        }
        if (MIDI_DEVICE_LOCATOR.equals(locator)) {
            return IOSPlayer.createMIDIPlayer();
        }

        int separator = locator.indexOf(':');
        if (separator <= 0) {
            throw new MediaException("Unsupported media locator: " + locator);
        }
        String protocol = locator.substring(0, separator).toLowerCase();
        if (!isSupportedProtocol(protocol)) {
            throw new MediaException("Unsupported media locator: " + locator);
        }
        if ("http".equals(protocol) || "https".equals(protocol)) {
            return createRemotePlayer(locator);
        }
        return new IOSPlayer(locator, typeFromName(locator));
    }

    public static Player createPlayer(InputStream stream, String type)
            throws IOException, MediaException {
        if (stream == null) {
            throw new IllegalArgumentException("stream is null");
        }

        byte[] data = readAll(stream);
        String resolvedType = type;
        if (resolvedType == null || resolvedType.length() == 0) {
            resolvedType = sniffContentType(data);
        }
        if (resolvedType == null || !IOSPlayer.isSupportedContentType(resolvedType)) {
            throw new MediaException("Unsupported or unknown content type: " + type);
        }
        return new IOSPlayer(data, resolvedType);
    }

    public static Player createPlayer(DataSource source)
            throws IOException, MediaException {
        if (source == null) {
            throw new IllegalArgumentException("source is null");
        }

        boolean connected = false;
        boolean started = false;
        try {
            source.connect();
            connected = true;
            source.start();
            started = true;

            SourceStream[] streams = source.getStreams();
            if (streams == null || streams.length == 0 || streams[0] == null) {
                throw new MediaException("DataSource contains no SourceStream");
            }

            SourceStream mediaStream = streams[0];
            int transferSize = mediaStream.getTransferSize();
            if (transferSize < 256 || transferSize > 65536) {
                transferSize = 4096;
            }
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[transferSize];
            int count;
            while ((count = mediaStream.read(buffer, 0, buffer.length)) != -1) {
                if (count > 0) {
                    output.write(buffer, 0, count);
                }
            }

            String contentType = source.getContentType();
            if (contentType == null && mediaStream.getContentDescriptor() != null) {
                contentType = mediaStream.getContentDescriptor().getContentType();
            }
            byte[] data = output.toByteArray();
            if (contentType == null) {
                contentType = sniffContentType(data);
            }
            if (contentType == null || !IOSPlayer.isSupportedContentType(contentType)) {
                throw new MediaException("Unsupported DataSource content type");
            }
            return new IOSPlayer(data, contentType);
        } finally {
            if (started) {
                try {
                    source.stop();
                } catch (IOException ignored) {
                }
            }
            if (connected) {
                source.disconnect();
            }
        }
    }

    public static void playTone(int note, int duration, int volume)
            throws MediaException {
        if (note < 0 || note > 127 || duration <= 0) {
            throw new IllegalArgumentException("bad tone parameters");
        }
        if (!IOSPlayer.playTone(note, duration, volume)) {
            throw new MediaException("Unable to play tone");
        }
    }

    public static TimeBase getSystemTimeBase() {
        return SYSTEM_TIME_BASE;
    }

    private static boolean isSupportedProtocol(String protocol) {
        for (int i = 0; i < PROTOCOLS.length; i++) {
            if (PROTOCOLS[i].equals(protocol)) {
                return true;
            }
        }
        return false;
    }

    private static String[] copy(String[] source) {
        String[] result = new String[source.length];
        System.arraycopy(source, 0, result, 0, source.length);
        return result;
    }

    private static Player createRemotePlayer(String locator)
            throws IOException, MediaException {
        String current = locator;
        for (int redirects = 0; redirects <= MAX_HTTP_REDIRECTS; redirects++) {
            HttpConnection connection = null;
            InputStream stream = null;
            try {
                connection = (HttpConnection)Connector.open(
                        current, Connector.READ, true);
                connection.setRequestProperty("Accept",
                        "audio/*, application/octet-stream;q=0.8, */*;q=0.1");
                connection.setRequestProperty("User-Agent", "phoneME-iOS/1.0");

                int responseCode = connection.getResponseCode();
                if (isRedirect(responseCode)) {
                    String location = connection.getHeaderField("location");
                    if (location == null || location.length() == 0) {
                        throw new IOException("HTTP redirect without Location: " +
                                responseCode);
                    }
                    current = resolveRedirect(current, location);
                    continue;
                }
                if (responseCode < 200 || responseCode >= 300) {
                    throw new IOException("HTTP media request failed: " +
                            responseCode);
                }

                String headerType = normalizeServerContentType(connection.getType());
                String nameType = typeFromName(current);
                String initialType = isUsableContentType(headerType) ?
                        headerType : nameType;
                long contentLength = connection.getLength();
                if (contentLength > MAX_BUFFERED_REMOTE_MEDIA_BYTES) {
                    return new IOSPlayer(current, initialType);
                }

                stream = connection.openInputStream();
                byte[] data = readAllLimited(stream,
                        MAX_BUFFERED_REMOTE_MEDIA_BYTES);
                if (data == null) {
                    return new IOSPlayer(current, initialType);
                }

                String sniffedType = sniffContentType(data);
                String resolvedType = isUsableContentType(sniffedType) ?
                        sniffedType : headerType;
                if (!isUsableContentType(resolvedType)) {
                    resolvedType = nameType;
                }
                if (!isUsableContentType(resolvedType)) {
                    throw new MediaException(
                            "Unable to resolve remote media content type: " + current);
                }
                return new IOSPlayer(data, resolvedType);
            } finally {
                if (stream != null) {
                    try {
                        stream.close();
                    } catch (IOException ignored) {
                    }
                }
                if (connection != null) {
                    try {
                        connection.close();
                    } catch (IOException ignored) {
                    }
                }
            }
        }
        throw new IOException("Too many HTTP redirects: " + locator);
    }

    private static boolean isRedirect(int responseCode) {
        return responseCode == 301 || responseCode == 302 ||
                responseCode == 303 || responseCode == 307 ||
                responseCode == 308;
    }

    private static String resolveRedirect(String base, String location)
            throws IOException {
        location = location.trim();
        if (location.startsWith("http://") || location.startsWith("https://")) {
            return location;
        }

        int schemeEnd = base.indexOf("://");
        if (schemeEnd <= 0) {
            throw new IOException("Invalid redirect base URL: " + base);
        }
        String scheme = base.substring(0, schemeEnd);
        if (location.startsWith("//")) {
            return scheme + ":" + location;
        }

        int authorityStart = schemeEnd + 3;
        int authorityEnd = base.indexOf('/', authorityStart);
        String origin = authorityEnd < 0 ? base : base.substring(0, authorityEnd);
        if (location.startsWith("/")) {
            return origin + location;
        }

        int query = base.indexOf('?');
        if (query >= 0) {
            base = base.substring(0, query);
        }
        int fragment = base.indexOf('#');
        if (fragment >= 0) {
            base = base.substring(0, fragment);
        }
        int slash = base.lastIndexOf('/');
        if (slash < authorityStart) {
            return origin + "/" + location;
        }
        return base.substring(0, slash + 1) + location;
    }

    private static String normalizeServerContentType(String type) {
        if (type == null) {
            return null;
        }
        int separator = type.indexOf(';');
        if (separator >= 0) {
            type = type.substring(0, separator);
        }
        type = type.trim().toLowerCase();
        if (type.length() == 0 || "application/octet-stream".equals(type) ||
                "binary/octet-stream".equals(type) ||
                "application/download".equals(type) ||
                "application/force-download".equals(type)) {
            return null;
        }
        if ("audio/mp3".equals(type)) {
            return "audio/mpeg";
        }
        if ("audio/wav".equals(type)) {
            return "audio/x-wav";
        }
        if ("audio/x-midi".equals(type) || "audio/sp-midi".equals(type)) {
            return "audio/midi";
        }
        if ("audio/x-m4a".equals(type)) {
            return "audio/mp4";
        }
        if ("audio/x-aac".equals(type) || "audio/aacp".equals(type) ||
                "audio/mp4a-latm".equals(type)) {
            return "audio/aac";
        }
        return type;
    }

    private static boolean isUsableContentType(String type) {
        return type != null && IOSPlayer.isSupportedContentType(type);
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        return readAllLimited(stream, Integer.MAX_VALUE);
    }

    private static byte[] readAllLimited(InputStream stream, int maximumBytes)
            throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int total = 0;
        int count;
        while ((count = stream.read(buffer)) != -1) {
            if (count <= 0) {
                continue;
            }
            if (total > maximumBytes - count) {
                return null;
            }
            output.write(buffer, 0, count);
            total += count;
        }
        return output.toByteArray();
    }

    private static String typeFromName(String value) {
        int query = value.indexOf('?');
        if (query >= 0) {
            value = value.substring(0, query);
        }
        String lower = value.toLowerCase();
        if (lower.endsWith(".mid") || lower.endsWith(".midi")) {
            return "audio/midi";
        }
        if (lower.endsWith(".mp3")) {
            return "audio/mpeg";
        }
        if (lower.endsWith(".wav")) {
            return "audio/x-wav";
        }
        if (lower.endsWith(".aac")) {
            return "audio/aac";
        }
        if (lower.endsWith(".m4a") || lower.endsWith(".mp4")) {
            return "audio/mp4";
        }
        if (lower.endsWith(".amr")) {
            return "audio/amr";
        }
        if (lower.endsWith(".tone") || lower.endsWith(".jts")) {
            return "audio/x-tone-seq";
        }
        return null;
    }

    private static String sniffContentType(byte[] data) {
        if (data == null || data.length < 4) {
            return null;
        }
        if (data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd') {
            return "audio/midi";
        }
        if (data.length >= 12 && data[0] == 'R' && data[1] == 'I' &&
                data[2] == 'F' && data[3] == 'F' && data[8] == 'W' &&
                data[9] == 'A' && data[10] == 'V' && data[11] == 'E') {
            return "audio/x-wav";
        }
        if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
            return "audio/mpeg";
        }
        if ((data[0] & 0xff) == 0xff &&
                ((data[1] & 0xf6) == 0xf0)) {
            return "audio/aac";
        }
        if ((data[0] & 0xff) == 0xff && ((data[1] & 0xe0) == 0xe0)) {
            return "audio/mpeg";
        }
        if (data.length >= 6 && data[0] == '#' && data[1] == '!' &&
                data[2] == 'A' && data[3] == 'M' && data[4] == 'R') {
            return "audio/amr";
        }
        if (data.length >= 12 && data[4] == 'f' && data[5] == 't' &&
                data[6] == 'y' && data[7] == 'p') {
            return "audio/mp4";
        }
        return null;
    }
}
