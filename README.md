# phoneME iOS

Native SwiftUI front end for the phoneME J2ME runtime.

## Current milestone

- One multiplatform Xcode target for iOS 16+ and macOS 13+.
- Native game library with `.jar` import and persistent metadata.
- Native emulator shell with scalable framebuffer surface and J2ME keypad.
- A small C ABI contract loaded through `dlsym`, allowing the phoneME VM to be linked into the app without VNC, a child window, or SDL UI.
- Native settings UI.

The app deliberately does not bundle the whole sibling `phoneME` checkout. The next core milestone is to compile only the CLDC/MIDP runtime, networking, RMS, media and the framebuffer/input ports into a static library exporting the symbols documented in `phoneME/Runtime/PhoneMECAPI.swift`.

## Build

```bash
xcodebuild -project phoneME.xcodeproj -target phoneME -sdk macosx build
xcodebuild -project phoneME.xcodeproj -target phoneME -sdk iphonesimulator build
```

Open `phoneME.xcodeproj` in Xcode to run the native app.

## Runtime ABI

The embedded runtime must export:

```c
void *phoneme_create(void);
void phoneme_destroy(void *runtime);
int32_t phoneme_start_jar(void *runtime, const char *jar_path);
void phoneme_stop(void *runtime);
void phoneme_send_key(void *runtime, int32_t key_code, int32_t pressed);
int32_t phoneme_copy_frame_rgba(
    void *runtime,
    uint8_t *destination,
    int32_t capacity,
    int32_t *width,
    int32_t *height
);
```

`phoneme_copy_frame_rgba` returns the required byte count. It may be called once with a null destination to query the current frame size. Pixels are tightly packed RGBA8888.

## License

Copyright 2026 Duy Pham. Licensed under the **Apache License, Version 2.0** with the **Commons Clause License Condition v1.0**.

This software is **not for commercial sale**. You may use, copy, modify, merge, publish, and distribute it under the Apache 2.0 terms, **but you may not Sell the Software** — i.e. you may not provide to third parties, for a fee or other consideration, a product or service whose value derives entirely or substantially from the function of this software (including paid hosting or paid consulting/support for it). All other rights and conditions of the Apache License remain in effect.

See [`LICENSE`](LICENSE) for the full terms.
