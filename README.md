# phoneME iOS

A J2ME (Java ME / MIDP 2.0) emulator for iOS and the web, built on a
from-scratch C++23 runtime. Play classic mobile-phone Java games and apps
natively on modern devices.

![Platform](https://img.shields.io/badge/platform-iOS%20%7C%20Web-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-green)

## Features

- **Complete J2ME runtime** — CLDC 1.1 and MIDP 2.x implemented in C++23, with
  no external class archives required at boot.
- **High-performance VM** — bytecode interpreter plus a baseline JIT with
  inline resolvers and OSR (on-stack replacement), tuned for real games.
- **Wide API coverage** — LCDUI (Canvas, Form, List, TextBox, Game API:
  Sprite, TiledLayer, LayerManager), RMS record stores, HTTP/HTTPS/sockets/
  datagrams/file connections (GCF), JSR-135 Mobile Media, and more. See
  [docs/J2ME_API_COVERAGE.md](docs/J2ME_API_COVERAGE.md) for the full audit.
- **Native iOS app** (SwiftUI, iOS 15+) — game library with `.jar` import,
  per-game profiles and workspaces, J2ME keypad, scalable framebuffer.
- **Web app** — the same core compiled to WebAssembly with a Material UI
  frontend, running entirely in the browser.

## Repository layout

```text
Core/       J2ME runtime written in C++23 (VM, JIT, graphics, network,
            media, RMS, JAR/classfile, security) exposed through a C ABI
phoneME/    Native iOS app (SwiftUI) embedding the core as a static library
web/        Vite + TypeScript frontend and WebAssembly build of the core
docs/       J2ME API coverage audit
Scripts/    Helper scripts
```

## Building

### iOS app

Requirements: macOS with Xcode, CMake, and an arm64 Apple device or simulator.

```bash
open phoneME.xcodeproj   # then select the phoneME target and run
```

The Xcode build compiles `Core/` into a static library automatically via
`Core/Tools/build-iphoneos.sh` (arm64, iOS 15.0+, C++23).

Command line:

```bash
xcodebuild -project phoneME.xcodeproj -target phoneME -sdk iphonesimulator build
```

### Web app

Requirements: Node.js, CMake, Emscripten (`emcc`, `emcmake`).

```bash
cd web
npm install
npm run dev       # development server (adds COOP/COEP headers for threads)
npm run build     # Wasm build + type-check + production bundle
```

## Testing

```bash
bash Core/Tools/test-host.sh                       # host test suite
PHONEME_SANITIZE=1 bash Core/Tools/test-host.sh    # ASan/UBSan
bash Core/Tools/test-full-regression.sh            # everything: host suites,
                                                   # sanitizers, arm64 archive,
                                                   # iOS app Debug+Release
```

## Runtime ABI

The core exposes a versioned C ABI (`Core/include/PhoneMECore.h`,
`PHONEME_C_API_VERSION`), consumed by both the iOS app
(`phoneME/Runtime/PhoneMECAPI.swift`) and the Wasm web build:

```c
uint32_t phoneme_c_api_version(void);
void    *phoneme_create(void);
void     phoneme_destroy(void *runtime);
int32_t  phoneme_start_jar(void *runtime, const char *jar_path);
void     phoneme_stop(void *runtime);
void     phoneme_send_key(void *runtime, int32_t key_code, int32_t pressed);
int32_t  phoneme_copy_frame_rgba(void *runtime, uint8_t *destination,
         int32_t capacity, int32_t *width, int32_t *height);
```

`phoneme_copy_frame_rgba` returns the required byte count; call it with a null
destination to query the current frame size (tightly packed RGBA8888). The ABI
is additive: new surface bumps the minor version, breaking changes require a
major bump, and hosts reject incompatible majors at load time.

## License

Copyright © 2026 Duy Pham.

This project is licensed under the [GNU General Public License v3.0](LICENSE).
