# phoneME Core

`Core/` is the only source tree for the rewritten J2ME runtime.

## Layout

```text
Core/
├── include/          Public C ABI and C++ headers
├── src/              C++23 implementation
├── Tests/            Host tests and Java fixture sources
├── Tools/            Build, verification and test scripts
├── CMakeLists.txt
└── README.md
```

Production code must not include, compile, link or load imported phoneME
snapshots, `classes.zip`, simulator targets, host-platform ports or legacy
phoneME make files. Vendor code may only be read separately as behavioral
reference while a module is rewritten.

## Target contract

- iPhoneOS only
- arm64 only
- iOS 15.0 or newer
- C++23 and libc++
- one object per `.cpp`
- no merged source
- boot classes are declared and implemented by package-specific C++ registry
  modules under `src/vm/*BuiltinClasses.cpp`; `BuiltinClasses.cpp` only composes
  those registries and performs lookup
- no external CLDC/MIDP runtime archive is required at startup
- no native pointer stored in a Java value, object handle or serialized state
- Java `long` and `double` remain Java category-2 values independent of native
  pointer size
- production builds use `-fno-exceptions` and `-fno-rtti`

## Commands

```sh
bash Core/Tools/test-c-api-host.sh
bash Core/Tools/test-builtin-registry.sh
bash Core/Tools/test-host.sh
PHONEME_SANITIZE=1 bash Core/Tools/test-host.sh
bash Core/Tools/test-all-host.sh
bash Core/Tools/build-iphoneos.sh
bash Core/Tools/verify-iphoneos.sh
bash Core/Tools/test-full-regression.sh
```

`test-full-regression.sh` is the integration-owner entrypoint. It runs the host
and standalone module suites, ASan/UBSan, rebuilds the arm64 iPhoneOS archive at
the path consumed by the app project, verifies archive provenance/symbols, then
builds the iOS app in Debug and Release without code signing.

`test-c-api-host.sh` compiles the public header as strict C11. The public ABI is
additive and exposes `PHONEME_C_API_VERSION` plus
`phoneme_c_api_version()` so the Swift/Objective-C host can reject incompatible
major versions before creating a runtime.

`test-builtin-registry.sh` is intentionally independent of `Machine`; it checks
package ownership, class hierarchy, native field layout and critical CLDC method
descriptors while other VM subsystems are being ported in parallel.

## Built-in class registry ownership

`BuiltinClassRegistry` accepts one factory per package module. New declarations
must be added to the owning module instead of extending the central lookup file:

- `LangBuiltinClasses.cpp`: `java.lang`, String and character conversion surface
- `IOBuiltinClasses.cpp`: `java.io`, byte/data streams and modified UTF contracts
- `UtilBuiltinClasses.cpp`: collections, Enumeration, Random and time classes
- `LcduiBuiltinClasses.cpp`: `javax.microedition.lcdui`
- `GameBuiltinClasses.cpp`: `javax.microedition.lcdui.game`
- separate MIDlet, RMS, filesystem, network, media, push and security modules
  follow the same registration contract

Each module exports exactly one `register_*_classes(BuiltinClassRegistry&)`
function. `BuiltinClasses.cpp` must remain composition-only so parallel agents do
not edit the same declaration switch.

The iPhoneOS archive is generated as:

```text
Core/libphoneMECore.a
```

## Implemented VM correctness

The production `Machine` now includes:

- JVM exception-table parsing, handler lookup and frame-by-frame unwind
- implicit Java exceptions for null access, division by zero, array bounds,
  negative array sizes, failed casts, array stores, stack overflow and OOM paths
- correct uncaught-throwable propagation across the C++ invocation boundary
- erroneous-class initialization semantics, including
  `ExceptionInInitializerError` and subsequent `NoClassDefFoundError`
- stack/category-2 operations, numeric conversions, comparisons, switches,
  legacy `jsr`/`ret`, casts, array checks and `multianewarray`
- virtual, special, static and interface dispatch with abstract/native/linkage
  error handling
- canonical class mirrors shared by class literals and static synchronized methods
- CLDC `StackMap` and Java `StackMapTable` parsing
- structural bytecode verification plus control-flow type-state verification for
  locals, operand stacks, branches, handlers, returns and constructor state
- reentrant object monitors, `monitorenter`/`monitorexit`, and instance/static
  synchronized-method monitor lifetime through return and exception unwind
- mark/sweep root publication for active frames, static fields, interned strings,
  class mirrors and held monitors, with allocation retry at VM safepoints
- MIDlet lifecycle failure isolation: a failing app is left in `error` state and
  cannot poison the next app VM

The host corpus exercises real `javac` output for exceptions, `finally`, dense
and sparse switches, floating point, multidimensional arrays, interface calls,
class initialization, class literals, monitors, synchronized methods and GC
pressure. ASan and UBSan are supported by the host test script.

## Remaining work

This is not yet a full J2ME implementation. The main remaining areas are:

- suspendable Java execution contexts and a real scheduler for `Thread.start`,
  `sleep`, `yield`, wait/notify and contended monitor handoff
- emergency OOM throwable reserve, complete native-handle roots and additional
  GC safepoints around every allocating native
- the complete CLDC/MIDP Java library and native surface
- RMS, filesystem sandbox policy and crash-safe persistence
- asynchronous networking, TLS and background lifecycle policy
- LCDUI, Canvas, GameCanvas, graphics command transport and input mapping
- media/audio lifecycle and background playback integration
- broad real-device compatibility testing against representative game and app
  JARs

Missing functionality must be implemented in this tree. Falling back to the old
phoneME core is forbidden.
