# phoneME Real-Game Compatibility Corpus

This directory turns compatibility claims into reproducible evidence. A corpus item passes only when every configured observable milestone is present. “The process did not crash” is not a pass condition.

## Quick commands

```sh
# Build the standalone runner, build project-authored fixture JARs, run enabled items,
# and refresh the generated evidence section in docs/J2ME_API_COVERAGE.md.
bash Core/Compatibility/run-corpus.sh

# Run one category or one exact item.
bash Core/Compatibility/run-corpus.sh --filter canvas
bash Core/Compatibility/run-corpus.sh --filter fixture-rms

# Scan class/method references without executing a MIDlet.
bash Core/Compatibility/run-corpus.sh --static-only

# Show all enabled and disabled entries.
bash Core/Compatibility/run-corpus.sh --list

# Run with ASan/UBSan.
bash Core/Compatibility/run-corpus.sh --sanitize
```

Every run uses an isolated output root under `Core/build/compatibility-17/` unless `PHONEME_COMPAT_ROOT` or `--output` overrides it. The report is written to `report.md`; `report.json` and `api-coverage.json` are the machine-readable forms.

## Corpus policy

Only JARs with a documented legal right to use in the test environment may be added. Project-authored fixtures are built from source and the generated JARs are ignored by Git. Commercial or community game JARs must remain under `Core/Compatibility/local/` or another local path and must not be committed unless redistribution permission is explicit.

Each manifest entry records:

- stable item ID and categories;
- JAR path and SHA-256;
- main MIDlet class;
- profile/configuration, device profile, viewport, locale, input capabilities, and required API surface;
- license kind, source, provenance, SHA-256, and whether redistribution is permitted;
- reproducible input sequence/scenario;
- expected install result, startup bound, app state, milestones, frame evidence, network/media actions, and exit state.

A local JAR must use an exact SHA-256. `"auto"` is reserved for project-authored generated fixtures. Do not enable placeholder entries until the class name, hash, expected milestones, and legal source are filled in.

## Required representative categories

`expected-results.json` contains either an executable project fixture or a disabled local placeholder for:

- offline Canvas;
- threaded GameCanvas;
- Sprite/TiledLayer/LayerManager;
- LCDUI Form/Command;
- RMS persistence;
- HTTP/HTTPS;
- socket/UDP online behavior;
- media/MMAPI;
- obfuscated or preverified Nokia-era class files.

Real-game additions should prefer multiple small, independently licensed JARs instead of one large collection. The analyzer ranks missing APIs and failures by the number of affected items, so diversity matters more than raw JAR count.

## Observable milestones

The built-in runner always emits structural milestones such as:

```text
runtime-configured
jar-installed
system-started
midlet-started
app-active
canvas-created
canvas-shown
lcdui-component-created
lcdui-screen-shown
frame-produced
frame-nonblank
midlet-destroyed
```

A fixture or runner adapter can add semantic milestones to stdout:

```text
COMPAT_MILESTONE:title-screen
COMPAT_MILESTONE:login-screen
COMPAT_MILESTONE:playable
COMPAT_NETWORK:socket-connect
COMPAT_NETWORK:http-get
COMPAT_MEDIA:tone-start
```

For a real game, use milestones that prove a meaningful state: title screen, menu navigation, login response, map entered, first controllable frame, RMS data restored, media started, or clean game-initiated exit. A generic `started` token is not sufficient for a complex game.

## Golden frames

The C++ runner writes a deterministic PPM snapshot. The analyzer computes SHA-256 and compares it with `expected.frame_hashes` when the list is non-empty. Use frame hashes only for scenes with deterministic rendering and state. Keep the original screenshot outside Git when licensing does not permit redistribution; the hash is sufficient for exact regression matching.

When a scene legitimately varies, use semantic milestones plus `min_frames` rather than weakening the test with a broad image tolerance.

## Differential runner protocol

`--reference-runner` accepts an adapter command for a legally usable reference implementation. Record the exact implementation/build with `--reference-name` and `--reference-version` (or `PHONEME_REFERENCE_RUNTIME` and `PHONEME_REFERENCE_VERSION`). Both current and reference runners receive the same arguments:

```text
--jar FILE
--main CLASS
--runtime-home DIR
--result FILE
--frame FILE
--width N
--height N
```

The analyzer also writes `scenario.json` beside each run and exposes its absolute path through `PHONEME_COMPAT_SCENARIO`. An adapter must replay the listed input sequence in order and retain its stdout, stderr, result JSON, frame, command line, runtime name, and runtime version under the item result directory.

The runner must write a JSON object containing at least:

```json
{
  "install": "success",
  "app_state": "active",
  "exit_code": 0,
  "startup_ms": 120,
  "frames_produced": 1,
  "milestones": ["title-screen", "playable"],
  "network_actions": [],
  "media_actions": []
}
```

The analyzer compares install state, app state, exit code, frame count/hash, milestones, network actions, and media actions. Set `require_reference_match` on an item only when those observables are expected to be identical. Timing is reported but not treated as differential equality.

## Failure taxonomy

The analyzer recognizes and aggregates:

- missing class;
- missing method;
- missing native;
- install/manifest/suite failure;
- verifier/ClassFormat/StackMap failure;
- thread/scheduler/monitor failure;
- LCDUI callback/bridge failure;
- graphics/framebuffer/render failure;
- RMS/persistence failure;
- uncaught Java exception;
- network failure;
- media failure;
- timeout and performance/memory-budget failure;
- ASan/UBSan/native crash;
- missing JAR or hash mismatch;
- milestone, app-state, startup, frame, or exit mismatch.

For every important compatibility bug, add a minimized source fixture under `fixtures/src/` when possible. Keep a large game JAR only as confirmation, not as the only reproduction.

## Adding a licensed real game

1. Put the JAR under `Core/Compatibility/local/` or another ignored local directory.
2. Record its SHA-256 and main class.
3. Add one disabled manifest entry with its legal source and required APIs.
4. Run `--static-only --include-disabled --filter <id>` to inspect references.
5. Define observable milestones and any stable frame hash.
6. Enable the entry only after the runner adapter can produce those observables.
7. When a failure is found, create a minimized fixture and link its ID in the bug report.

Do not modify a game JAR to hide a Core failure. Any optional compatibility patch must be explicit, separately controlled, and documented as a compatibility rule rather than silently changing the original corpus artifact.
