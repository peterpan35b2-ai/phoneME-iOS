# phoneME CLDC native macOS arm64

This target builds phoneME's CLDC VM directly for Apple Silicon. It does not use Docker, Rosetta, an x86 binary, or an external JVM at runtime.

## Design

- Mach-O arm64 executable.
- Portable C interpreter compiled as C++17.
- Java primitive widths remain unchanged (`int` is 32-bit, `long` is 64-bit).
- VM words, object references, native handles, frames and KNI carriers are pointer-sized on the LP64 host.
- Java object oop maps and the GC bitmap use 4-byte Java-word units.
- Native VM metadata oop maps use 8-byte host-word units.
- The old ARM32 compiler/JIT backend is disabled. A dedicated AArch64 code generator is not included in this port.
- The target is currently non-ROMized (`ROMIZING=false`).

## Requirements

- Apple Silicon Mac (`uname -m` must report `arm64`).
- Xcode Command Line Tools or Xcode.
- JDK 8. Set `JDK_DIR` when `/usr/libexec/java_home -v 1.8` cannot locate it.

Example:

```bash
export JDK_DIR="$HOME/Library/Java/JavaVirtualMachines/amazon-corretto-8.jdk/Contents/Home"
```

## Build

Debug VM with assertions:

```bash
bash tools/macos-arm64/build-cldc.sh debug
```

Release VM:

```bash
bash tools/macos-arm64/build-cldc.sh release
```

Generated artifacts:

```text
cldc/build/darwin_c/dist/bin/cldc_vm_g   debug VM
cldc/build/darwin_c/dist/bin/cldc_vm_r   release VM
cldc/build/darwin_c/dist/bin/preverify   native arm64 preverifier
cldc/build/darwin_c/dist/lib/cldc_classes.zip
```

The VM sources use C++17. The original preverifier is a separate legacy C utility and is intentionally compiled in GNU89 mode.

## Verify the port

Run the basic test and the GC/reference/monitor stress test against both builds:

```bash
bash tools/macos-arm64/smoke-test.sh both
```

The stress test covers:

- object and object-array references;
- 64-bit `long` values;
- String and StringBuffer operations;
- `System.arraycopy`;
- synchronized monitor enter/exit;
- weak-reference storage;
- repeated compacting collections;
- the VM's internal `+VerifyGC` heap verifier.

Expected final output includes:

```text
phoneME native arm64: 42
object-ok
arm64-stress-ok:160
checksum:340848605035094
Native arm64 smoke test passed (both).
```

## Run an application

Supply preverified application classes through `PHONE_ME_CLASSPATH`:

```bash
PHONE_ME_CLASSPATH=/path/to/verified/classes \
  bash tools/macos-arm64/run-cldc.sh com.example.Main arg1 arg2
```

The runner uses the release VM by default. Select another build with:

```bash
PHONE_ME_MODE=debug PHONE_ME_CLASSPATH=/path/to/classes \
  bash tools/macos-arm64/run-cldc.sh com.example.Main
```

VM flags can be placed before the main class, for example:

```bash
PHONE_ME_CLASSPATH=/path/to/classes \
  bash tools/macos-arm64/run-cldc.sh =HeapCapacity8M com.example.Main
```

## Current limitation

This is a native AArch64 interpreter port, not an AArch64 JIT port. Code executes natively inside the arm64 VM process, but Java bytecodes are handled by the portable C interpreter. Implementing a phoneME AArch64 assembler/compiler backend would be a separate project.
