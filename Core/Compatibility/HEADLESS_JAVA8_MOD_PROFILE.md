# Headless Java 8 Compatibility Profile

This profile supports J2ME games and applications that were decompiled, modified,
and recompiled with a Java 8 toolchain while still targeting the phoneME MIDP
runtime. It is intentionally not a Java SE desktop runtime.

## Inclusion rules

An API may be added only when all of the following are true:

1. It is useful to headless/mobile game code or commonly emitted by Java 8
   compilers and decompilers.
2. It can be implemented safely inside the existing iOS sandbox and phoneME VM.
3. Its class, method, and field signatures are covered by the API audit or by a
   focused compatibility fixture.
4. Its behavior has a Core regression test.
5. It does not require a desktop window system, server container, process
   execution, unrestricted host filesystem access, or unrestricted reflection.

## Included surface

### Compiler/runtime compatibility

- `StringBuilder`
- suppressed exceptions on `Throwable`
- `Iterable`, `Iterator`, `Comparable`
- `AutoCloseable`, `Closeable`
- selected Java 8 class-file and invocation behavior already handled by the VM

### Collections

- `List`, `Collection`, `ArrayList`
- `Map`, `HashMap`
- `Set`, `HashSet`
- common `Arrays`, `Collections`, and `Objects` operations

The implementations are optimized for the small collections typical of mobile
mods. `HashMap` currently uses compact linear storage rather than the complete
OpenJDK hashing implementation. Iterators and collection views are snapshots,
which avoids retaining complex fail-fast bookkeeping in the mobile VM.

### Data and I/O

- sandboxed `java.io.File` and file stream constructors
- `BufferedInputStream` and `BufferedOutputStream` as lightweight passthrough
  wrappers
- charset bridges already used by string and reader/writer APIs
- basic RFC 4648 Base64 encoder and decoder

Only the basic Base64 alphabet is supported. URL-safe and MIME variants are not
part of this profile until demanded by real mobile software.

### Time

- the minimal `LocalTime` subset needed by rebuilt J2ME software

## Explicit exclusions

The profile must not automatically import or emulate these Java SE areas:

- AWT, Swing, JavaFX, applets, desktop printing, desktop clipboard, or desktop
  drag-and-drop
- RMI, JMX, CORBA, naming/directory services, servlet or application-server APIs
- JDBC, SQL, scripting engines, compiler/tool APIs, process execution, or shell
  integration
- unrestricted `java.nio.file` host filesystem access
- full `java.util.concurrent`, parallel streams, fork/join, or desktop-scale
  executors
- Java Stream API and lambda utility surface unless a real modded JAR requires a
  narrowly implementable subset
- full regex, locale, formatting, or timezone databases unless demanded by the
  compatibility corpus

## Current coverage baseline

The August 4, 2026 corpus audit covers 2,349 JAR files and 2,348 MIDlet
entrypoints. For every reachable external API reference in that corpus, the
runtime registry currently resolves:

- 243 of 243 classes
- 1,678 of 1,678 method signatures
- 19 of 19 field signatures

The remaining corpus diagnostics are malformed input files, not API gaps:
`ZombieInfection2_SonyEricsson_C905_EN_IGP_ATT_MRC_127.jar` is not a ZIP
archive, and `SuzyWongTwist.jar` contains a class entry with invalid class-file
magic.

A separate comparison against the complete Corretto Java 8 `rt.jar` scans
20,411 classes. Only the mobile/headless subset is in scope. The missing Java
SE surface is intentionally dominated by desktop UI, server, management,
platform-internal, and tooling APIs; it must not be copied wholesale into the
iOS emulator.

## Verification

Run these checks after changing the profile:

```sh
bash Core/Tools/test-builtin-registry.sh
bash Core/Tools/test-host.sh
python3 Core/Tools/audit-api-coverage.py \
  --jar-dir jar_test \
  --output Core/build/headless-api-audit \
  --all-midlets

bash Core/Tools/audit-api-surface.sh \
  "$JAVA8_HOME/jre/lib/rt.jar" \
  Core/build/jdk8-full-surface-audit.md
```

A new API is accepted only when missing class, method, and field signatures are
explained, implemented, and protected by tests. The profile should remain small
even as the main J2ME/JSR compatibility surface grows.
