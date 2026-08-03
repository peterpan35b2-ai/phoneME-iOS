# J2ME API Coverage on iOS

Last audited: 2026-08-01

This document records the API surface actually packaged in `PhoneMERuntime/classes.zip`, the iOS/native backend status, and the remaining compatibility gaps. A Java class being present does **not** automatically mean its backend works; each section distinguishes real runtime support from interface-only compatibility.

## Status legend

- **Working**: Java API, native/backend implementation, and build/runtime validation are present.
- **Partial**: Core behavior works, but some optional operations or platform-dependent codecs are unavailable.
- **Interface only**: Classes exist so MIDlets can load, but `getControl()` returns `null` or the operation is unsupported.
- **Missing**: Public API package and backend are not currently packaged.

## Core platform

| API | Status | Notes |
| --- | --- | --- |
| CLDC 1.1 | Working | ARM64 phoneME VM, Java core libraries, threads, collections, streams, math and networking primitives. |
| MIDP 2.x MIDlet lifecycle | Working | Start, pause, destroy, exit and VM cleanup are bridged to the iOS host. |
| `MIDlet.platformRequest()` | Working | Opens supported HTTP/HTTPS, telephone, SMS, email and registered URL handlers through `UIApplication`; unsupported handlers raise `ConnectionNotFoundException`. |
| LCDUI | Working/ongoing refinement | Canvas, Form, List, Alert, TextBox, Items, Commands and native iOS display bridge are packaged. Mapping/layout correctness is maintained separately from this API audit. |
| LCDUI Game API | Working | GameCanvas, Sprite, TiledLayer, Layer and LayerManager are packaged. |
| RMS | Working | RecordStore and related enumeration/listener APIs are packaged. |
| PKI public interfaces | Packaged | Certificate and CertificateException are present. TLS transport is listed separately below. |

## Networking and Generic Connection Framework

| API / protocol | Status | Notes |
| --- | --- | --- |
| `http://` | Working | MIDP HTTP implementation and native socket backend are packaged. |
| `socket://` | Working | Client socket connection is packaged. |
| `serversocket://` | Working | Server socket support is enabled. |
| `datagram://` | Working | UDP datagram implementation is packaged. |
| `file://` / JSR-75 FileConnection | Working | Implemented in this audit; details below. |
| `https://` | Working | iOS-native `HttpsConnection` backed by `URLSession`: GET/POST/HEAD, request/response headers, body, redirects, timeout, system certificate validation and `SecurityInfo`. Responses are buffered with a 64 MB safety limit. |
| raw `ssl://` | Missing | No TLS stream protocol backend. |
| `comm://` | Missing | Serial-port access is not applicable to the current iOS sandbox. |
| PushRegistry | Partial/unverified | Public/internal classes are packaged, but alarm wake-up and background network activation still need device-level validation against iOS lifecycle restrictions. |

## JSR-135 Mobile Media API

### Working audio/media behavior

- `Manager.createPlayer(String)` for local file, HTTP/HTTPS URL, resource, tone device and MIDI device locators.
- `Manager.createPlayer(InputStream, String)` with complete stream ingestion and content sniffing.
- `Manager.createPlayer(DataSource)` and the public media protocol interfaces.
- Player lifecycle: `realize`, `prefetch`, `start`, `stop`, `deallocate`, `close`.
- Loop count, media time seeking, current time, duration and system `TimeBase`.
- `PlayerListener` events for start, stop, end-of-media, close, error and volume changes.
- `VolumeControl` for PCM/compressed audio; mute and level changes are applied to the iOS player.
- `ToneControl` sequence playback and `Manager.playTone()` using generated PCM tones.
- Local/in-memory MP3, WAV/PCM, AAC/M4A/MP4 audio through AVFoundation.
- MIDI playback through `AVMIDIPlayer`.
- HTTP/HTTPS streaming through `AVPlayer` when media is opened directly by MMAPI.
- VM lifecycle cleanup closes all active players so audio cannot leak into the next MIDlet.

### Partial or platform-dependent

- AMR/AMR-WB are advertised for compatibility, but actual decoding depends on the codec support available in the running iOS release.
- `AVMIDIPlayer` has no direct per-player volume API, so MIDI playback works but MMAPI volume adjustment is not equivalent to PCM audio.
- Network media opened by `AVPlayer` follows iOS streaming behavior rather than the MIDP HTTP protocol stack.

### Interface-only MMAPI controls

The following standard interfaces are packaged to prevent `ClassNotFoundError`, but the current audio-only player returns `null` for unsupported controls:

- `VideoControl`
- `RecordControl`
- `FramePositioningControl`
- `MetaDataControl`
- `MIDIControl`
- `PitchControl`
- `RateControl`
- `TempoControl`
- `StopTimeControl`
- generic `GUIControl`

Camera capture, microphone recording and video rendering are therefore not yet implemented.

## JSR-75 FileConnection

Status: **Working**

The implementation exposes a sandboxed virtual root:

```text
file:///Phone/
```

It maps to a `jsr75/Phone` directory inside the phoneME storage root. Path normalization rejects `..`, NUL and unknown roots, so a MIDlet cannot escape into arbitrary iOS application files.

Implemented behavior:

- `FileSystemRegistry.listRoots()` and listener registration.
- `exists`, `isDirectory`, `getName`, `getPath`, `getURL`, `lastModified`.
- `create`, `mkdir`, `delete`, `rename`, `truncate`.
- `openInputStream`, `openDataInputStream`, positional `openOutputStream`, `openDataOutputStream`.
- Output writes preserve the untouched tail of an existing file and commit on flush/close.
- Directory listing with `*`/`?` wildcard filtering and hidden-file filtering.
- File size, recursive/non-recursive directory size, total/available/used storage.
- Readable/writable attributes and dot-file hidden naming.
- POSIX-backed native I/O with `fsync` on committed writes.

Runtime validation is provided by `Tests/Compatibility/FileConnectionSmokeMIDlet.java`. It validates create, write, positional overwrite, read, wildcard list, rename, truncate and delete inside the real phoneME VM. Expected output:

```text
FILECONNECTION_SMOKE_OK
```

JSR-75 PIM (`javax.microedition.pim`) remains missing.

## Nokia compatibility APIs

Status: **Working for common game usage**

Packaged APIs:

- `com.nokia.mid.ui.FullCanvas`
- `com.nokia.mid.ui.DirectGraphics`
- `com.nokia.mid.ui.DirectUtils`
- `com.nokia.mid.ui.DeviceControl`
- `com.nokia.mid.sound.Sound`
- `com.nokia.mid.sound.SoundListener`

Implemented behavior:

- Full-screen Canvas and Nokia key constants.
- ARGB color and alpha tracking.
- Raw byte, short and int pixel formats.
- `drawPixels` and native `getPixels` for framebuffer and mutable images.
- Horizontal/vertical flip and 90/180/270-degree rotation.
- Image drawing, triangles and polygon outline/fill.
- Mutable image helpers.
- Vibration and light/idle-timer control through the iOS host.
- WAV playback, generated frequency tones, gain, state listeners, loop, stop/resume/release.
- Nokia Smart Messaging OTA ringtone decoding: song type, pattern, loop, scale, style, tempo, volume and note duration modifiers.

## iOS device feedback

| Feature | Status | Notes |
| --- | --- | --- |
| MIDP vibration | Working | Annunciator backend calls iOS vibration and supports stop/cancellation generation. |
| Nokia vibration | Working | Frequency is mapped to pulse interval; duration and cancellation are honored. |
| Backlight / keep-awake | Working approximation | Maps to `UIApplication.idleTimerDisabled`; iOS does not expose arbitrary hardware backlight control to apps. |
| MIDP alert sounds | Intentionally disabled | LCDUI alerts and validation warnings are silent on iOS; game media and tone APIs remain available. |

## Missing optional JSRs and vendor APIs

These APIs are not currently packaged and require separate modules/backends. They should not be represented as implemented merely by adding empty classes.

| API | Status | Suggested iOS backend |
| --- | --- | --- |
| JSR-75 PIM | Missing | Contacts/EventKit with explicit user permission. |
| JSR-82 Bluetooth/OBEX | Missing | CoreBluetooth for BLE; classic RFCOMM/OBEX cannot be reproduced generally on iOS. |
| JSR-120/205 WMA SMS/MMS | Missing | iOS does not permit background arbitrary SMS transport; limited compose UI is not API-equivalent. |
| JSR-172 Web Services | Missing | Java SOAP/XML parser and URLSession transport. |
| JSR-177 SATSA | Missing | Keychain/Secure Enclave can cover only a subset; SIM-card APDU access is unavailable to normal iOS apps. |
| JSR-179 Location | Missing | Core Location. |
| JSR-180 SIP | Missing | Network.framework/VoIP implementation; significant lifecycle restrictions apply. |
| JSR-184 M3G | Missing | Metal/OpenGL compatibility renderer plus full M3G scene graph and loader. |
| JSR-211 CHAPI | Missing | iOS document/URL handlers and app intents; cross-MIDlet invocation requires custom routing. |
| JSR-226 SVG | Missing | SVG Tiny parser/renderer, likely CoreGraphics/Metal-backed. |
| JSR-234 Advanced Multimedia | Missing | AVFoundation audio/video controls beyond MMAPI audio. |
| JSR-239 OpenGL ES | Missing | Metal translation or restricted legacy OpenGL ES path. |
| JSR-256 Sensor | Missing | Core Motion and available iOS sensors. |
| JSR-257 Contactless | Missing | Core NFC supports only a subset and requires entitlements/device support. |
| JSR-272 Mobile Broadcast | Missing | No general equivalent iOS broadcast receiver API. |
| JSR-280 XML | Missing | XML parser module is not packaged. |
| Siemens extensions | Missing | Must be implemented per class usage found in target games. |
| Motorola extensions | Missing | Must be implemented per class usage found in target games. |
| Samsung extensions | Missing | Must be implemented per class usage found in target games. |
| Sony Ericsson extensions | Missing | Must be implemented per class usage found in target games. |

## Validation completed

- ARM64 simulator phoneME static core built with MMAPI, Nokia, FileConnection and iOS-native HTTPS/platform-request backends.
- ARM64 device phoneME static core/package rebuilt with the same native backends.
- `classes.zip`, generated native function table and static archives were inspected for `HttpsConnection`, HTTPS KNI entries and platform-request symbol binding.
- Full iOS Simulator app build: succeeded.
- Full `iphoneos` Release app build with code signing disabled: succeeded.
- AVFoundation WAV smoke test: create player, duration, seek, volume/mute, close and reset passed.
- FileConnection MIDlet smoke test inside the phoneME VM: passed and exited with code 0.
- `classes.zip` and generated native function table were inspected to ensure the public classes and KNI entries are actually packaged.

## Priority order for remaining compatibility work

1. Device-level PushRegistry/background lifecycle validation.
2. Raw `ssl://` stream support for the smaller set of MIDlets that bypass `HttpsConnection`.
3. JSR-179 Location and JSR-75 PIM, using permission-gated native bridges.
4. Game-driven vendor API implementation based on real missing-class logs.
5. JSR-184 M3G, as a separate renderer project rather than a placeholder API.
6. Camera/video/recording controls for MMAPI.

Do not claim universal J2ME compatibility until the remaining optional packages are either implemented or explicitly handled per game profile.

<!-- COMPATIBILITY_CORPUS_GENERATED:BEGIN -->
## Corpus-derived compatibility evidence

This section is generated by `Core/Compatibility/analyze-failures.py`. It reports observed corpus evidence and does not upgrade an API to Working by itself.

Last corpus run: `2026-08-03T02:49:29+00:00`

- PASS: 0
- FAIL: 1
- SKIP: 0
- STATIC-only: 0
- Distinct referenced J2ME API classes: 10

Most frequently referenced classes in the available corpus:

| Class | References |
| --- | ---: |
| `java.lang.System` | 2 |
| `java.io.PrintStream` | 2 |
| `javax.microedition.midlet.MIDlet` | 1 |
| `javax.microedition.lcdui.Display` | 1 |
| `javax.microedition.lcdui.game.GameCanvas` | 1 |
| `javax.microedition.lcdui.game.LayerManager` | 1 |
| `javax.microedition.lcdui.Image` | 1 |
| `javax.microedition.lcdui.game.Sprite` | 1 |
| `javax.microedition.lcdui.game.TiledLayer` | 1 |
| `javax.microedition.lcdui.Graphics` | 1 |

No missing class/method/native failure was observed in the latest run.

<!-- COMPATIBILITY_CORPUS_GENERATED:END -->
