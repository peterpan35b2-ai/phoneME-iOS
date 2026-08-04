# phoneME C++ Core Optimization Handoff

Updated: 2026-08-04

## 1. Purpose

This document is the implementation handoff for optimizing the rewritten C++ J2ME core in `Core/` by learning from important performance techniques in `Vendor/phoneME`.

The goal is not to import or revive the legacy phoneME runtime. The goal is to preserve the current portable C++ architecture while adopting the performance ideas that remain valid:

- resolve expensive metadata once instead of on every bytecode execution;
- keep interpreter hot paths free of string construction, hashing and mutex churn;
- store Java primitive arrays in their native element widths;
- avoid C++ heap allocation for every Java call frame;
- make allocation and garbage collection cache-friendly;
- retain exact Java semantics, diagnostics, exception behavior and suite isolation;
- measure every optimization against representative real JARs before claiming success.

This handoff is intentionally specific enough that an implementer can begin work immediately without redesigning the plan first.

---

## 2. Non-negotiable project constraints

All work described here must obey `Core/README.md` and the following constraints.

1. `Core/` remains the only production runtime.
2. `Vendor/phoneME` is a behavioral and optimization reference only.
3. Production code must not compile, link, load or execute the legacy phoneME VM.
4. Target production architecture remains:
   - iPhoneOS;
   - arm64;
   - iOS 15 or newer;
   - C++23 and libc++;
   - `-fno-exceptions`;
   - `-fno-rtti`.
5. No Swift, Objective-C or native pointer may be stored in:
   - a Java field;
   - `Value`;
   - `ObjectRef`;
   - RMS;
   - suite cache serialized state.
6. Java `long` and `double` remain category-2 values regardless of native pointer size.
7. Existing Java exception semantics, verifier checks, class initialization rules, monitor lifetime and GC roots must remain correct.
8. LCDUI, Canvas and platform integration remain native iOS bridges. Do not port the legacy phoneME UI stack.
9. Do not introduce a JIT as part of this plan. iOS executable-memory policy, code signing and maintenance cost make the old ARM/Thumb JIT unsuitable as an initial optimization.
10. Do not overwrite unrelated uncommitted work. Always inspect `git status --short` before changing shared files, especially `Core/src/vm/Machine.cpp`.
11. Every phase must be independently testable and revertible.
12. No optimization may silently weaken bounds checks, reference generation checks, class compatibility checks or Java exception behavior.

---

## 3. Current architecture and observed hot-path costs

The current core already has several good foundations:

- class, method, field and assignability caches;
- `ObjectRef` generation checking;
- mark/sweep GC with explicit root publication;
- cooperative scheduler quanta;
- native method registry;
- C++23 error propagation without C++ exceptions;
- release builds using `-O3`, `-DNDEBUG` and `-fno-exceptions`;
- targeted native intrinsics for known expensive Java patterns.

However, static inspection shows the following recurring costs.

### 3.1 String-based metadata lookup remains in execution paths

Relevant files:

- `Core/src/vm/ClassRepository.cpp`
- `Core/src/vm/ClassLayout.cpp`
- `Core/src/vm/NativeMethodRegistry.cpp`
- `Core/src/vm/Machine.cpp`

Current cache keys are built by concatenating strings such as:

```text
owner + '\n' + method name + '\n' + descriptor
```

Even on a cache hit, the runtime may still perform:

1. class-name normalization;
2. dynamic `std::string` construction;
3. hashing;
4. mutex acquisition;
5. `unordered_map` lookup.

This occurs around field access, method dispatch, type checks and native invocation, all of which are common bytecodes.

### 3.2 Heap access locks per operation

Relevant files:

- `Core/include/phoneme/vm/Heap.hpp`
- `Core/src/vm/Heap.cpp`
- `Core/src/vm/Machine.cpp`

The public heap operations each acquire `Heap::mutex_`:

- `field`;
- `set_field`;
- `element`;
- `set_element`;
- `array_length`;
- `class_name`;
- byte-array reads and writes.

One array bytecode may therefore acquire the same heap mutex multiple times for class validation, length validation and element access.

`Machine::execute()` already serializes execution through `execution_mutex_`, so the interpreter is paying synchronization cost below an already serialized VM execution boundary.

### 3.3 Primitive arrays use 16-byte `Value` elements

Relevant files:

- `Core/include/phoneme/vm/Value.hpp`
- `Core/include/phoneme/vm/Heap.hpp`
- `Core/src/vm/Heap.cpp`

`Value` is currently 16 bytes and `Heap::Object::elements` is `std::vector<Value>` for every array type.

Consequences:

- a Java `byte[]` uses approximately 16 bytes per payload byte, excluding vector/object overhead;
- `short[]` and `char[]` use approximately eight times their natural width;
- `int[]` and `float[]` use approximately four times their natural width;
- primitive arrays are scanned as generic `Value` objects during GC even though they cannot contain references;
- byte-oriented network, image, RMS and media paths repeatedly convert between `Value` and native bytes.

### 3.4 Java call frames own dynamic vectors

Relevant files:

- `Core/include/phoneme/vm/SlotStorage.hpp`
- `Core/src/vm/Machine.cpp`, `ExecutionFrame`

Each frame owns:

- one `LocalVariables` vector;
- one `OperandStack` vector;
- descriptor state;
- frame metadata.

Method-heavy games therefore cause frequent native allocation, capacity initialization and cache-unfriendly storage.

### 3.5 Bytecode is decoded and validated repeatedly

Relevant file:

- `Core/src/vm/Machine.cpp`

The interpreter repeatedly:

- reads multi-byte operands;
- parses switch tables;
- resolves constant-pool references;
- checks method descriptors;
- computes branch targets;
- resolves fields, methods and classes;
- performs type-dispatch setup.

Correctness checks are necessary, but most can be performed once after class verification and represented as immutable decoded metadata.

### 3.6 Scheduler state changes are not O(1)

Relevant file:

- `Core/src/vm/Scheduler.cpp`

`update_queue_membership_locked()` removes an ID from multiple deques using erase/remove, making each state transition O(number of threads). This is not normally the largest bottleneck, but it becomes visible in network-heavy or worker-heavy MIDlets.

### 3.7 Targeted intrinsics cannot replace a generic fast path

`Machine.cpp` contains useful pattern-specific intrinsics. They should remain where they are correct and measured, but adding more per-game patterns is not a substitute for:

- direct resolved field access;
- direct method targets;
- typed arrays;
- low-overhead frames;
- low-overhead heap access.

Before adding more intrinsics, the generic execution path must be optimized.

---

## 4. Vendor/phoneME mechanisms worth learning from

The following legacy mechanisms are useful as design references.

### 4.1 Quickened bytecodes and resolved constant-pool entries

Reference files:

- `Vendor/phoneME/cldc/src/vm/share/interpreter/Bytecodes.cpp`
- `Vendor/phoneME/cldc/src/vm/share/interpreter/Bytecodes.hpp`
- `Vendor/phoneME/cldc/src/vm/cpu/c/Interpreter_c.cpp`
- `Vendor/phoneME/cldc/src/vm/share/ROM/ConstantPoolRewriter.cpp`
- `Vendor/phoneME/cldc/src/vm/share/ROM/BytecodeOptimizer.cpp`

phoneME replaces repeatedly resolved operations with fast forms such as:

- resolved `getfield` and `putfield` variants;
- resolved static field variants;
- resolved virtual, static, special and interface calls;
- fast native entry;
- resolved `new`, `anewarray`, `checkcast` and `instanceof`;
- superinstructions combining common bytecode sequences.

The current core should copy the concept, not the exact legacy bytecode encoding.

### 4.2 Inline/bump allocation and young-generation collection

Reference files:

- `Vendor/phoneME/cldc/src/vm/share/memory/ObjectHeap.hpp`
- `Vendor/phoneME/cldc/src/vm/share/memory/ObjectHeap.cpp`

Important ideas:

- contiguous allocation area;
- bump-pointer fast allocation;
- collection only after fast allocation fails or a safepoint requests it;
- young-generation collection;
- remembered set for old-to-young references;
- mark bitmap and compact storage;
- reusable GC work buffers.

The current core should not copy the legacy object-header model. It should preserve stable generation-checked handles while moving payload storage to arenas over time.

### 4.3 Continuous Java execution stack

phoneME stores frames, locals and operand stacks in a VM-managed execution stack instead of allocating independent native containers for each Java call.

The current core can gain most of this benefit while preserving `Value` and existing verifier semantics.

### 4.4 Scheduler caches and event-driven blocking

Reference files:

- `Vendor/phoneME/cldc/src/vm/share/runtime/Scheduler.cpp`
- `Vendor/phoneME/cldc/src/vm/share/runtime/Scheduler.hpp`
- `Vendor/phoneME/cldc/src/vm/share/runtime/Thread.cpp`
- `Vendor/phoneME/cldc/src/vm/share/runtime/Thread.hpp`

Useful ideas:

- cache the next runnable thread;
- maintain direct queue membership;
- sleep until an event or nearest timer rather than polling;
- separate runnable, sleeping, waiting and blocked states explicitly.

Do not port the old green-thread implementation. Adapt only the data-structure and event-wait principles.

### 4.5 Bytecode and method profiling

Reference files:

- `Vendor/phoneME/cldc/src/vm/share/utilities/BytecodeHistogram.cpp`
- `Vendor/phoneME/cldc/src/vm/share/utilities/BytecodeHistogram.hpp`
- compiler performance counters under `Vendor/phoneME/cldc/src/vm/share/compiler/`

The current core needs equivalent measurement before large optimizations are merged.

### 4.6 ROM/prelink processing

Reference files:

- `Vendor/phoneME/cldc/src/vm/share/ROM/ROMOptimizer.cpp`
- `Vendor/phoneME/cldc/src/vm/share/ROM/ROMInliner.cpp`
- `Vendor/phoneME/cldc/src/vm/share/ROM/ConstantPoolRewriter.cpp`
- `Vendor/phoneME/cldc/src/vm/share/ROM/BytecodeOptimizer.cpp`

Useful concept:

- parse, verify, resolve and optimize once before normal execution.

The replacement should be a versioned per-suite cache, not the legacy ROM image format.

---

## 5. Target architecture

The intended end state is:

```text
JAR / built-in classes
        |
        v
Class parser + verifier
        |
        v
Immutable Runtime Metadata
  - ClassId / MethodId / FieldId
  - parsed descriptors
  - class layouts
  - method indexes
  - decoded bytecode
  - resolved operands
        |
        v
Interpreter
  - decoded instruction dispatch
  - direct field offsets
  - direct static storage slots
  - method IDs and inline caches
  - direct native IDs
        |
        v
VM-owned execution stack
  - frame headers
  - locals
  - operand slots
        |
        v
Generation-checked handle heap
  - stable ObjectRef handle table
  - typed object payloads
  - typed primitive arrays
  - object arrays scanned by GC
  - eventually arena/nursery allocation
```

The design must preserve a mapping from every decoded instruction back to its original Java bytecode index, because exception tables, stack maps, diagnostics and compatibility behavior use bytecode indices.

---

## 6. Implementation sequence

Do not attempt all changes in one patch. Use the phases below in order.

Recommended commit sequence:

1. baseline profiler and benchmark harness;
2. immutable metadata IDs and indexes;
3. parsed descriptor cache;
4. decoded-method representation without changing execution semantics;
5. resolved field/class/static operands;
6. resolved method/native operands and call-site inline caches;
7. lock-free VM-thread heap access layer;
8. typed primitive and object arrays;
9. frame/slot arena;
10. scheduler O(1) queue membership;
11. optional suite prelink cache;
12. optional nursery and minor GC;
13. measured superinstructions or dispatch changes.

Each commit must pass the full correctness suite before the next phase begins.

### Implementation status — 2026-08-04

Implemented and validated in the current checkout:

- **Phase 0 complete for host profiling and reproducible reports**:
  - compile-time `PHONEME_ENABLE_VM_PROFILING` switch;
  - thread-local interpreter, metadata, heap/GC and scheduler counters;
  - synthetic benchmark, full-Core profile JSON and SHA-256-verified seven-JAR manifest;
  - profiling-disabled, profiling-enabled and ASan/UBSan host suites pass.
- **Phase 1 complete for the scoped metadata work in this handoff**:
  - strong `ClassId`, `MethodId`, `FieldId` and `NativeMethodId` types;
  - immutable class/method metadata publication with generation invalidation;
  - indexed declared method lookup;
  - parsed descriptor reuse;
  - native calls bound and invoked by stable ID;
  - resolved fields cached by constant-pool call site with direct slot/static metadata.
- **Phase 2 execution/decode foundation implemented behind `PHONEME_ENABLE_DECODED_EXECUTION`**:
  - immutable decoded instruction, operand, switch and exception metadata;
  - original BCI-to-instruction mapping, including legacy arbitrary switch padding and `jsr`/`ret`/`wide` targets;
  - decoded opcode/BCI dispatch integrated while preserving the original bytecode interpreter as the fallback semantic executor;
  - all immediate, local, constant-pool, branch, switch, array, type, `wide`, `multianewarray` and `invokedynamic` operands consumed directly from decoded metadata;
  - monomorphic virtual/interface call-site cache using receiver `ClassId` and target `MethodId`;
  - direct `invokestatic`/`invokespecial` call-site cache using stable `MethodId`;
  - runtime `PHONEME_USE_DECODED_EXECUTION=0` switch supports legacy-vs-decoded differential execution from one decoded-enabled binary;
  - deterministic differential fixtures and full host/sanitizer suites pass with decoded execution both disabled and enabled.

Latest reproducible host run:

```text
Core/build/performance/phase2-all-operands-final/benchmark-run.json
Core/build/performance/phase2-all-operands-final/full-core-profile.json
Core/build/performance/decoded-differential.json
```

Selected counters from that run:

```text
executed bytecodes                 5,083,323
method invocations                    2,775
native invocations                    2,305
decoded methods                         296
decoded instructions                 11,367
decoded opcode dispatches          5,083,323
decoded operand dispatches         1,724,183
virtual inline-cache hits / misses   821 / 648
direct-call cache hits / misses      452 / 353
method-resolution cache hits            465
declared-method cache hits               20
field-resolution cache hits             978
GC count / maximum host pause         45 / 38,041 ns
```

Compared with the pre-inline-cache Phase 1 profile, the virtual and direct call-site caches remove 1,273 repeated target lookups. Method-resolution cache hits fell from 1,414 to 465 and declared-method cache hits fell from 344 to 20 while the executed-bytecode total, exception count and opcode histogram remain unchanged.

`Core/Tools/test-decoded-differential-host.sh` runs one decoded-enabled binary in legacy and decoded modes. `vm-invocation`, `vm-extended` and `micro3d` currently match exactly for executed bytecodes, method/native calls, exception dispatches, class initialization, opcode histogram and deterministic heap/metadata/scheduler counters.

Remaining Phase 2 work:

- move successful field, class, static/special method and native target resolution into per-method decoded runtime side tables rather than the current Machine-local call-site maps;
- add explicit `unresolved -> resolving -> resolved/failed` linkage states and cache stable Java linkage failures at the original BCI;
- extend differential fixtures to compare selected heap-visible/static-field effects and stable thrown messages;
- re-run representative JAR corpus tests before changing decoded execution from default OFF.

Phases 3 and later have not started. Heap locking, generic `Value` arrays, frame vectors and scheduler deque scans remain intentionally unchanged until Phase 2 is finished and remeasured.

---

# Phase 0 — Measurement and reproducible baselines

## 7. Why this phase is mandatory

Optimization without measurements will cause regressions or move cost between subsystems. Existing targeted intrinsics show that real JAR behavior can differ substantially from microbenchmarks.

The first implementation must provide low-overhead profiling that can be disabled in production.

## 8. Proposed files

Create:

```text
Core/include/phoneme/vm/PerformanceCounters.hpp
Core/src/vm/PerformanceCounters.cpp
Core/Tests/PerformanceCounterTests.cpp
Core/Tests/VmPerformanceTests.cpp
Core/Tools/test-performance-host.sh
Core/Tools/benchmark-core-host.sh
```

Update:

```text
Core/CMakeLists.txt
Core/src/vm/Machine.cpp
Core/src/vm/Heap.cpp
Core/src/vm/Scheduler.cpp
```

Do not add a dependency on third-party benchmark libraries.

## 9. Required counters

At minimum collect:

### Interpreter

- total executed bytecodes;
- fixed array of 256 opcode counters;
- method invocation count;
- native invocation count;
- maximum Java call depth;
- exception dispatch count;
- class initialization count;
- instruction-budget exits;
- scheduler quantum count.

### Metadata

- class-cache hit/miss;
- method-resolution hit/miss;
- declared-method-resolution hit/miss;
- field-resolution hit/miss;
- assignability-cache hit/miss;
- native registry lookup count;
- virtual inline-cache hit/miss after that feature exists.

### Heap and GC

- object allocations by payload kind;
- allocated bytes by payload kind;
- failed allocations;
- public locked heap operation count;
- VM fast heap operation count after that feature exists;
- GC count;
- GC total time;
- GC maximum pause;
- roots scanned;
- objects scanned;
- objects reclaimed;
- primitive bytes scanned, which should become zero after typed arrays.

### Scheduler

- state transitions;
- queue erase scans;
- yields;
- sleeps;
- event wakeups;
- spurious wakeups when measurable.

## 10. Counter implementation rules

1. Production builds must be able to compile counters out with a macro such as:

```text
PHONEME_ENABLE_VM_PROFILING=0
```

2. Do not use an atomic increment per bytecode in the interpreter.
3. Use thread-local fixed-size counters during execution and merge at a safe boundary.
4. Do not allocate memory while incrementing counters.
5. Do not hold a global mutex inside the bytecode loop.
6. Export a snapshot structure for tests and optional app diagnostics.
7. Reset must happen only when no execution is active.
8. Counters must not keep Java objects alive.

## 11. Benchmark corpus

Create a checked-in benchmark manifest containing exact JAR paths, SHA-256 values, launch timeout and expected milestone. It should include representative categories:

- Canvas-heavy offline game;
- GameCanvas action game;
- image/resource decompression-heavy game;
- network-heavy online game;
- LCDUI application;
- RMS-heavy game;
- media-heavy application;
- M3G game if currently supported by the corpus;
- one obfuscated CLDC 1.0 title;
- one MIDP 2.0 title.

Do not claim compatibility from a benchmark title unless it reaches its expected milestone.

## 12. Baseline metrics to record

For each selected title record:

- startup wall time to first UI/frame;
- executed bytecodes to first UI/frame;
- interpreter throughput in bytecodes/second;
- process CPU over a fixed active interval;
- peak estimated Java heap;
- resident memory when available;
- GC count and maximum pause;
- average and p95 rendered frame interval when the host exposes it;
- scheduler yield/sleep counts;
- metadata cache hit ratios.

Store baseline output under a non-source artifact directory or a documented local path. Do not commit device-specific binary traces.

## 13. Phase 0 acceptance

- Normal host tests pass with profiling disabled.
- Profiling-enabled host tests pass.
- Counter values are deterministic for deterministic fixtures.
- No counter code allocates or locks once per bytecode.
- At least one benchmark run can produce a machine-readable report.
- Baseline results are captured before Phase 1 changes.

---

# Phase 1 — Stable runtime metadata IDs and indexes

## 14. Objective

Replace repeated string-based identity and linear lookup with stable integer IDs and immutable indexes while keeping existing public behavior.

This phase must not yet change the Java heap representation or bytecode format.

## 15. Proposed files

Create:

```text
Core/include/phoneme/vm/MetadataId.hpp
Core/include/phoneme/vm/RuntimeMetadata.hpp
Core/src/vm/RuntimeMetadata.cpp
Core/Tests/RuntimeMetadataTests.cpp
```

Update:

```text
Core/include/phoneme/classfile/ClassFile.hpp
Core/src/classfile/ClassFile.cpp
Core/include/phoneme/vm/ClassRepository.hpp
Core/src/vm/ClassRepository.cpp
Core/include/phoneme/vm/ClassLayout.hpp
Core/src/vm/ClassLayout.cpp
Core/include/phoneme/vm/NativeMethodRegistry.hpp
Core/src/vm/NativeMethodRegistry.cpp
Core/src/vm/Machine.cpp
```

## 16. Strong ID types

Use strong types rather than interchangeable integers:

```cpp
struct ClassId final {
    u32 value {0};
    friend constexpr bool operator==(ClassId, ClassId) noexcept = default;
};

struct MethodId final {
    u32 value {0};
    friend constexpr bool operator==(MethodId, MethodId) noexcept = default;
};

struct FieldId final {
    u32 value {0};
    friend constexpr bool operator==(FieldId, FieldId) noexcept = default;
};

struct NativeMethodId final {
    u32 value {0};
    friend constexpr bool operator==(NativeMethodId, NativeMethodId) noexcept = default;
};
```

Rules:

- ID zero is invalid/unresolved.
- IDs are valid only within one `Machine`/metadata generation unless explicitly serialized through the suite cache format.
- IDs must not be stored in Java-visible fields as a native pointer substitute.
- Runtime metadata objects become immutable after publication.
- Cache invalidation must occur when classpath archives or built-in registry generations change.

## 17. Class metadata

Each loaded class should have an immutable metadata record containing at least:

```cpp
struct RuntimeClass final {
    ClassId id;
    std::shared_ptr<const classfile::ClassFile> class_file;
    ClassId super_id;
    std::vector<ClassId> interface_ids;
    std::shared_ptr<const ClassLayout> layout;
    // Method and field lookup indexes.
};
```

Do not require all referenced classes to be loaded recursively at initial class parse. Resolution remains lazy where Java linkage semantics require it.

## 18. Method index

`ClassFile::find_method()` currently performs a linear scan. Add an immutable method index built once after parsing.

The index key should not allocate during lookup. Acceptable implementations:

- interned name ID + descriptor ID;
- a compact pair hash over immutable string views whose storage belongs to the class file;
- sorted vector and binary search if method counts are small and profiling proves it faster.

Do not store `std::string_view` keys pointing into temporary strings.

Required behavior:

- duplicate illegal method signatures remain rejected during class parsing/verifying;
- returned method addresses remain stable for the lifetime of the owning `ClassFile`;
- declared lookup and hierarchy lookup retain their distinct semantics.

## 19. Field index and layout

Build field lookup metadata once per class layout. A resolved field must contain:

```cpp
struct ResolvedField final {
    FieldId id;
    ClassId declaring_class;
    usize slot_index;
    ValueKind value_kind;
    bool is_static;
    // Static storage index when applicable.
};
```

After resolution, bytecode execution must not need the declaring class name, field name or descriptor string to access storage.

## 20. Native registry IDs

At registration time:

- assign each native method a `NativeMethodId`;
- retain the human-readable signature for diagnostics;
- expose a direct lookup from a resolved Java method to its native ID;
- invoke by ID in the fast path.

The slow string registry remains for initial binding and error messages only.

## 21. Parsed descriptor cache

Method and field descriptors must be parsed once and stored in metadata.

Required cached information for methods:

- argument types;
- Java argument slot count;
- whether a receiver is required;
- return kind;
- category-2 layout;
- invocation-interface operand count validation data.

`Machine::execute()` and nested invocation setup must not call `parse_method_descriptor()` repeatedly for an already loaded method.

## 22. Phase 1 acceptance

- All existing class, method, field and descriptor tests pass.
- New tests verify ID uniqueness and invalidation.
- No Java-visible behavior changes.
- Method lookup no longer performs a linear scan after metadata publication.
- Native invocation can use a prebound native ID.
- Repeated invocation of one method does not parse its descriptor again.
- Profiling confirms fewer string-key constructions and registry lookups.

---

# Phase 2 — Decoded methods and resolved operands

## 23. Objective

Create an immutable decoded representation of verified bytecode and execute it without repeatedly decoding operands or resolving metadata.

Do not mutate the original class-file bytecode. Keep it for diagnostics, differential tests and serialization compatibility.

## 24. Proposed files

Create:

```text
Core/include/phoneme/vm/DecodedMethod.hpp
Core/include/phoneme/vm/DecodedInstruction.hpp
Core/src/vm/DecodedMethod.cpp
Core/src/vm/DecodedInstruction.cpp
Core/Tests/DecodedMethodTests.cpp
Core/Tests/DecodedExecutionTests.cpp
```

Update:

```text
Core/include/phoneme/classfile/ClassFile.hpp
Core/include/phoneme/vm/Machine.hpp
Core/src/vm/Machine.cpp
Core/src/vm/Verifier.cpp
Core/CMakeLists.txt
```

## 25. Required decoded representation

A decoded method must preserve:

- original method identity;
- original bytecode size;
- one decoded instruction per executable bytecode instruction;
- mapping from instruction index to original BCI;
- mapping from every valid BCI target to decoded instruction index;
- exception table BCIs;
- switch payloads;
- parsed and validated operands;
- resolved side-table entries where resolution is legal;
- original BCI for error reporting.

Suggested structure:

```cpp
enum class DecodedOpcode : u16 {
    // Standard JVM operations plus internal resolved forms.
};

struct DecodedInstruction final {
    DecodedOpcode opcode;
    u32 bytecode_pc;
    u32 next_index;
    u32 operand_index;
    i32 immediate;
};

struct DecodedMethod final {
    MethodId method_id;
    std::vector<DecodedInstruction> instructions;
    std::vector<u32> bci_to_instruction;
    std::vector<ResolvedOperand> operands;
    std::vector<DecodedSwitchTable> switches;
};
```

The exact layout may change after measurement, but the first version should favor correctness and clear ownership over extreme packing.

## 26. Resolution categories

### Resolve during decode when safe

- branch targets;
- switch targets;
- local indexes;
- primitive array kind;
- constant kind;
- parsed method argument slot count;
- field reference symbolic components;
- class reference symbolic component;
- statically bound `invokestatic` and most `invokespecial` targets after required class linkage;
- native ID for a resolved method.

### Resolve lazily and cache

- class references whose loading/initialization timing affects Java semantics;
- `invokevirtual` target by receiver runtime class;
- `invokeinterface` target by receiver runtime class;
- fields or methods whose first access must throw a linkage error at the exact Java execution point;
- `checkcast` and `instanceof` assignability when referenced classes are not yet linked.

A lazy operand must have an explicit state machine:

```text
unresolved -> resolving -> resolved
                      \-> failed with stable linkage error
```

Because current VM execution is serialized, the initial implementation may guard this state through the VM execution lock. Do not introduce per-instruction mutexes.

## 27. Java linkage semantics

Do not eagerly throw errors during class loading if Java requires the error only when an instruction is first executed.

The decoded representation may store symbolic references first and resolve them at the execution point. Once resolved or failed, cache the result.

Stable failed resolution is important. Repeated execution should reproduce the correct Java linkage exception without repeating expensive hierarchy searches.

## 28. Exception and BCI requirements

Every thrown Java exception must continue to report and dispatch from the original bytecode PC.

The decoded loop must:

- update `current_instruction_pc` from `DecodedInstruction::bytecode_pc`;
- use original BCI ranges for exception handler matching;
- preserve `jsr`/`ret` semantics for legacy titles;
- preserve valid arbitrary switch padding behavior already supported by the current interpreter;
- preserve instruction-budget accounting as one count per original Java bytecode, unless a separately documented superinstruction policy is later introduced.

## 29. First execution strategy

The safest migration is dual execution:

1. Keep the existing bytecode interpreter intact.
2. Add a decoded interpreter path behind a runtime/build flag.
3. Run differential fixtures through both paths.
4. Compare:
   - returned value;
   - thrown class and message where stable;
   - heap-visible effects;
   - static field effects;
   - executed instruction count;
   - monitor and thread state.
5. Make decoded execution the default only after the corpus and sanitizer suites pass.

Do not delete the old path in the same commit that introduces the decoded path.

## 30. Virtual call-site inline cache

Each decoded `invokevirtual` and `invokeinterface` call site should contain a small cache.

Initial monomorphic cache:

```cpp
struct VirtualCallCache final {
    ClassId receiver_class;
    MethodId target_method;
    bool valid {false};
};
```

Execution:

1. Obtain receiver `ClassId` without constructing a class-name string.
2. If it equals the cached receiver class, call cached method directly.
3. Otherwise perform normal resolution and update the cache.

Requirements:

- constructors and `invokespecial` must not use virtual dispatch;
- interface semantics must still reject incompatible receivers;
- abstract and incompatible method errors remain correct;
- cache lifetime is bounded by metadata generation;
- no stale native pointer is serialized;
- cache misses are profiled.

Only add polymorphic caches after real measurements show monomorphic misses remain significant.

## 31. Resolved field forms

Decoded field instructions should carry direct storage metadata:

- declaring `ClassId`;
- static or instance flag;
- field slot/static slot index;
- expected `ValueKind`;
- class-initialization requirement.

The execution path must no longer call `ClassStateRegistry::resolve_field()` after the operand is resolved.

## 32. Resolved class/type forms

For `new`, `anewarray`, `checkcast`, `instanceof` and `multianewarray`, cache:

- target `ClassId` or symbolic unresolved entry;
- array component kind;
- instantiability metadata;
- assignability fast data where possible.

Class initialization remains separate from class resolution. `new` must preserve the existing class initialization and erroneous-class behavior.

## 33. Phase 2 acceptance

- Differential execution fixtures match the old path.
- All exception-table tests pass using original BCIs.
- Legacy `jsr`/`ret`, switch and obfuscator fixtures pass.
- Repeated field access no longer performs field resolution after warmup.
- Repeated static/special calls no longer perform method resolution after warmup.
- Virtual call cache hit ratio is reported.
- No per-bytecode string allocation occurs in warmed decoded execution.
- Corpus startup and active gameplay do not regress.

---

# Phase 3 — VM-thread heap fast access

## 34. Objective

Remove repeated heap mutex acquisition from interpreter and synchronous native hot paths while retaining a safe public API for external/host access.

This phase is primarily about synchronization ownership, not changing object layout.

## 35. Concurrency contract

The intended contract is:

1. One `Machine` executes Java bytecode at a time under `Machine::execution_mutex_`.
2. Java heap mutation occurs only on the VM execution thread or while holding an explicit VM execution token.
3. Platform callbacks must not directly mutate Java heap objects from arbitrary threads.
4. Platform callbacks enqueue completion data into thread-safe native registries/queues and wake the scheduler.
5. Java-visible completion is applied when VM execution resumes.
6. GC runs only at a VM safepoint with all roots published.

This contract must be documented in headers and enforced with debug assertions.

## 36. Proposed files

Create:

```text
Core/include/phoneme/vm/HeapExecutionAccess.hpp
Core/src/vm/HeapExecutionAccess.cpp
Core/Tests/HeapExecutionAccessTests.cpp
```

Update:

```text
Core/include/phoneme/vm/Heap.hpp
Core/src/vm/Heap.cpp
Core/include/phoneme/vm/Machine.hpp
Core/src/vm/Machine.cpp
Core/src/vm/*Natives.cpp where hot array/object access occurs
```

## 37. API shape

Keep current public locked functions for tests and external callers.

Add an internal access object that can be created only by `Machine` while execution ownership is valid:

```cpp
class HeapExecutionAccess final {
public:
    Result<Value> field(ObjectRef reference, usize index) const;
    Status set_field(ObjectRef reference, usize index, Value value);
    Result<Value> element(ObjectRef reference, usize index) const;
    Status set_element(ObjectRef reference, usize index, Value value);
    Result<usize> array_length(ObjectRef reference) const;
    Result<ClassId> class_id(ObjectRef reference) const;
};
```

`HeapExecutionAccess` must call private unlocked heap primitives. It must not lock per operation.

Possible enforcement:

- `HeapExecutionAccess` constructor is private and `Machine` is a friend;
- construction requires a non-copyable `MachineExecutionToken`;
- debug builds record the owning native thread ID and assert on misuse;
- the access object cannot outlive the current execution scope.

Do not expose raw references or pointers that can survive allocation/GC unless they are protected by a documented pin/guard mechanism.

## 38. Combined heap operations

Avoid reproducing multiple calls for one bytecode. Add combined operations for interpreter use:

```cpp
Result<Value> load_array_element(
    ObjectRef array,
    usize index,
    ArrayKind expected_kind) const;

Status store_array_element(
    ObjectRef array,
    usize index,
    ArrayKind expected_kind,
    Value value);

Result<Value> load_instance_field(
    ObjectRef object,
    ClassId expected_assignable_class,
    usize slot,
    ValueKind expected_kind) const;
```

A combined operation performs reference generation validation, object kind validation, bounds/type check and access in one traversal.

Java exceptions should still be created by `Machine` at the instruction boundary. Heap primitives may return structured internal errors describing null, bounds, stale reference, wrong kind or type mismatch.

## 39. Public API migration

Do not convert every native module at once.

Order:

1. interpreter array and field bytecodes;
2. core String/stream native hot paths;
3. graphics/image pixel paths;
4. network byte buffers;
5. RMS byte buffers;
6. media data paths;
7. remaining native modules.

Track locked public heap calls in the profiler. The count during active Java execution should trend toward zero.

## 40. GC and root safety

The fast access layer must not cache a pointer across:

- allocation that may trigger GC;
- explicit GC request;
- nested Java invocation that may allocate;
- scheduler blocking/resume;
- platform callback boundary.

If a bulk operation needs direct storage access, use a scoped view whose lifetime explicitly forbids GC or pins the payload through a stable handle.

The first implementation should prefer short scoped views and no allocation inside the view.

## 41. Phase 3 acceptance

- Existing public heap tests still pass.
- Debug assertions reject fast access outside an execution scope.
- Interpreter field/array operations do not acquire `Heap::mutex_` per access.
- Locked heap operation count drops substantially in benchmarks.
- TSAN-equivalent reasoning is documented even if ThreadSanitizer cannot run on every target.
- ASan/UBSan pass.
- Network/media/platform callbacks do not directly use unlocked heap access from callback threads.

---

# Phase 4 — Typed Java arrays

## 42. Objective

Store arrays according to their Java element width and reference semantics.

This phase must reduce memory use, improve locality, accelerate bulk operations and reduce GC scanning.

## 43. Proposed files

Create:

```text
Core/include/phoneme/vm/ArrayKind.hpp
Core/include/phoneme/vm/ArrayStorage.hpp
Core/src/vm/ArrayStorage.cpp
Core/Tests/TypedArrayTests.cpp
Core/Tests/ArrayCopyTests.cpp
```

Update:

```text
Core/include/phoneme/vm/Heap.hpp
Core/src/vm/Heap.cpp
Core/src/vm/Machine.cpp
Core/src/vm/CoreNatives.cpp or owning System.arraycopy native module
Core/src/vm/GraphicsNatives.cpp
Core/src/vm/ImageNatives.cpp
Core/src/vm/ConnectionNatives.cpp
Core/src/vm/RmsNatives.cpp
Core/src/vm/MediaNatives.cpp
```

## 44. Array kinds

Define explicit kinds:

```cpp
enum class ArrayKind : u8 {
    boolean8,
    byte8,
    char16,
    short16,
    int32,
    long64,
    float32,
    double64,
    reference,
};
```

Do not infer array behavior by repeatedly comparing class-name strings such as `"[B"` in the hot path.

## 45. Storage representation

A correctness-first implementation may use a variant:

```cpp
using ArrayStorage = std::variant<
    std::vector<u8>,       // boolean
    std::vector<i8>,       // byte
    std::vector<char16_t>, // char
    std::vector<i16>,      // short
    std::vector<i32>,      // int
    std::vector<i64>,      // long
    std::vector<float>,    // float
    std::vector<double>,   // double
    std::vector<ObjectRef> // reference
>;
```

If duplicate `u8`/`i8` alternatives make `std::variant` access awkward, wrap them in distinct storage structs.

A later arena implementation may replace vectors with raw spans, but the semantic API should remain stable.

## 46. Java conversion semantics

At bytecode boundaries:

- `baload` sign-extends Java byte;
- `baload` on boolean returns 0 or 1;
- `bastore` into boolean normalizes according to existing JVM semantics used by the core;
- `caload` zero-extends `char`;
- `saload` sign-extends `short`;
- `iastore`, `lastore`, `fastore`, `dastore` preserve exact bit/value semantics;
- `aastore` performs assignability checks before mutation;
- null reference stores are allowed;
- all bounds checks remain exact.

Add explicit tests for minimum/maximum values and negative byte/short values.

## 47. Object representation

Split object payload types instead of carrying both fields and elements for every object.

Suggested design:

```cpp
struct InstancePayload final {
    std::vector<Value> fields;
};

struct PrimitiveArrayPayload final {
    ArrayKind kind;
    ArrayStorage storage;
};

struct ReferenceArrayPayload final {
    ClassId component_class;
    std::vector<ObjectRef> elements;
};

using HeapPayload = std::variant<
    InstancePayload,
    PrimitiveArrayPayload,
    ReferenceArrayPayload
>;
```

String payload may remain a separate optional/special payload during this phase, but avoid allocating unused vectors for every object.

## 48. Bulk byte APIs

Replace element-by-element `Value` conversion in byte-oriented native paths with bulk APIs:

```cpp
Result<std::vector<u8>> copy_byte_array(
    ObjectRef array,
    usize offset,
    usize length) const;

Status write_byte_array(
    ObjectRef array,
    usize offset,
    std::span<const u8> bytes);
```

For hot synchronous operations, provide scoped byte views only when the caller cannot allocate or trigger GC while the view is alive.

Never expose a raw array pointer to Swift/Objective-C for asynchronous retention. Copy into platform-owned storage at the boundary.

## 49. `System.arraycopy`

Implement specialized paths:

- primitive source/destination must have exactly matching array kind;
- reference arrays perform Java assignability checks;
- overlapping ranges use `memmove`/equivalent behavior;
- zero length still validates Java-required null/type/range conditions in the correct order;
- byte counts use checked arithmetic;
- no temporary `std::vector<Value>` is created for primitive copies.

## 50. GC scanning

After this phase:

- instance fields are scanned for reference-valued `Value` entries;
- reference arrays are scanned directly as `ObjectRef` values;
- primitive arrays are not scanned for references;
- String payload text is not scanned;
- weak references retain existing special handling.

Profiler should confirm zero primitive element scans.

## 51. Memory accounting

Update `Heap::estimate_object_bytes()` or its replacement to use actual payload sizes.

Tests must verify approximate accounting for:

- empty arrays;
- one-element arrays;
- large byte arrays;
- reference arrays;
- strings;
- cloned arrays and objects.

Do not double-count vector capacity and logical payload inconsistently. Pick a documented accounting policy and use it everywhere.

## 52. Migration strategy

Do not switch all code by global search/replace.

Recommended steps:

1. Introduce `ArrayKind` and typed allocation API.
2. Make new arrays use typed storage.
3. Keep compatibility wrappers returning/accepting `Value` for old callers.
4. Migrate interpreter array bytecodes.
5. Migrate `System.arraycopy`.
6. Migrate byte-oriented native modules.
7. Remove generic `elements` storage only when no caller depends on it.

## 53. Phase 4 acceptance

- All primitive array bytecodes pass edge-value tests.
- `aastore` compatibility tests pass.
- Overlapping `System.arraycopy` tests pass.
- A large `byte[]` consumes close to one byte per element plus bounded metadata rather than 16 bytes per element.
- Network/image/RMS byte copies no longer convert every byte through `Value`.
- Primitive arrays are not scanned by GC.
- Full host, sanitizer and corpus tests pass.
- Peak Java heap decreases on resource-heavy benchmarks.

---

# Phase 5 — VM-owned execution stack and frame arena

## 54. Objective

Remove native allocation of independent locals/operand vectors for each Java call.

Retain existing `Value` semantics initially. Compact slot representation is a separate optional optimization.

## 55. Proposed files

Create:

```text
Core/include/phoneme/vm/ExecutionStackArena.hpp
Core/src/vm/ExecutionStackArena.cpp
Core/Tests/ExecutionStackArenaTests.cpp
Core/Tests/DeepCallTests.cpp
```

Update:

```text
Core/include/phoneme/vm/ExecutionContext.hpp
Core/src/vm/ExecutionContext.cpp
Core/include/phoneme/vm/JavaThread.hpp
Core/src/vm/JavaThread.cpp
Core/include/phoneme/vm/SlotStorage.hpp
Core/src/vm/Machine.cpp
```

## 56. Stack model

Each Java execution context/thread owns one arena:

```text
frame 0 header
frame 0 local slots
frame 0 operand capacity
frame 1 header
frame 1 local slots
frame 1 operand capacity
...
```

Frames store offsets, not raw pointers, so arena growth cannot leave stale pointers.

Suggested metadata:

```cpp
struct FrameRecord final {
    MethodId method_id;
    u32 decoded_instruction_index;
    u32 current_bytecode_pc;
    u32 locals_offset;
    u32 locals_slots;
    u32 operand_offset;
    u32 operand_capacity;
    u32 operand_used_slots;
    std::optional<ObjectRef> synchronized_monitor;
    std::optional<Value> return_override;
};
```

The exact storage may use separate frame and slot vectors if that simplifies stable offsets. The critical requirement is reuse and no allocation per ordinary call after capacity warmup.

## 57. Category-2 values

Preserve current continuation-slot validation for locals.

Operand stack representation currently stores one `Value` object while tracking Java slot count. The arena implementation must preserve all existing stack manipulation semantics, including:

- `dup`, `dup_x1`, `dup_x2`;
- `dup2`, `dup2_x1`, `dup2_x2`;
- `pop`, `pop2`;
- category-2 local continuation slots;
- legacy return-address values.

Reuse existing tests and add deep mixed-category call tests.

## 58. Root scanning

The execution arena must support direct root scanning without creating temporary per-frame vectors.

Preferred API:

```cpp
void for_each_reference_root(
    const std::function<void(ObjectRef)>& visitor) const;
```

For the hot GC path, avoid `std::function` allocation by using a templated visitor or direct loop in the implementation.

Root scanning must include:

- locals;
- operand values;
- synchronized monitors;
- pending return override;
- pending exception state owned by the execution context.

## 59. Capacity and overflow

- Use checked arithmetic for slot growth.
- Enforce `kMaximumCallDepth` or a replacement documented limit.
- Surface Java `StackOverflowError` through existing behavior.
- Do not let native `std::bad_alloc` escape because production uses no C++ exceptions.
- Pre-reserve a reasonable amount but do not allocate maximum theoretical stack for every thread.
- Shrink only at lifecycle boundaries, not on every return.

## 60. Phase 5 acceptance

- No native allocation occurs for normal method entry after arena capacity is warmed.
- Deep recursion still throws the correct Java error.
- All stack/category-2 tests pass.
- GC sees all live references in frames.
- Monitor release on normal return and exception unwind remains correct.
- Instruction-budget and scheduler accounting remain correct.
- Method-heavy benchmark throughput improves or at minimum shows no regression.

---

# Phase 6 — Scheduler data-structure optimization

## 61. Objective

Make state transitions and runnable selection O(1) or amortized O(1), while preserving the current native-thread/cooperative architecture.

## 62. Proposed files

Update:

```text
Core/include/phoneme/vm/JavaThread.hpp
Core/src/vm/JavaThread.cpp
Core/include/phoneme/vm/Scheduler.hpp
Core/src/vm/Scheduler.cpp
Core/Tests/SchedulerTests.cpp
```

Optionally create:

```text
Core/include/phoneme/vm/IntrusiveThreadQueue.hpp
Core/Tests/IntrusiveThreadQueueTests.cpp
```

## 63. Queue membership

Current implementation removes a thread ID from multiple deques using erase/remove.

Replace this with explicit queue membership stored on the scheduler thread record:

```cpp
enum class SchedulerQueueKind : u8 {
    none,
    runnable,
    blocked,
    sleeping,
};
```

Use either:

- intrusive doubly linked IDs;
- stable `std::list` iterators stored in the thread record;
- another measured structure with O(1) removal.

Prefer an intrusive representation to avoid one native heap allocation per transition.

A transition must:

1. remove from the current queue once;
2. update state;
3. add to the target queue once;
4. notify only the condition needed by that transition.

## 64. Runnable cache

Maintain a cached next runnable ID when valid, similar in concept to phoneME's `_next_runnable_thread`.

Invalidate it when:

- the cached thread blocks, sleeps, terminates or is suspended;
- priority policy changes selection;
- shutdown excludes the thread;
- a newly runnable higher-priority policy requires reselection.

Do not let the cache alter fairness semantics without a documented scheduler policy change.

## 65. Interrupt wakeups

`Scheduler::interrupt()` currently may notify every scheduler condition to ensure a join/wait target wakes.

Track the blocking reason and specific wait target so interruption can wake:

- the interrupted thread's condition;
- the target condition used by join, when different;
- the global scheduler event condition only when needed.

Retain exact Java interrupt-flag semantics.

## 66. Timer/event sleep

When no Java thread is runnable, sleep until:

- the nearest timer deadline;
- an I/O/platform event;
- an interrupt;
- shutdown.

Do not poll at a fixed short interval when all work is blocked.

The current iOS-specific frame pacing and CPU backoff logic must be retained unless measured evidence justifies a replacement.

## 67. Phase 6 acceptance

- Scheduler tests cover every state transition.
- Queue membership invariants are asserted in debug builds.
- State transitions no longer scan all queues.
- Interrupt wakes exactly the required waiters.
- No lost wakeups or double completion.
- Background idle CPU does not increase.
- Online/network corpus titles remain stable across suspend/resume and close.

---

# Phase 7 — Per-suite prelink cache

## 68. Objective

Persist expensive class parsing, verification and decode results so later launches avoid repeating work.

This phase is optional until Phases 1–5 are stable. It must not become a correctness dependency: the runtime must always be able to rebuild the cache from the JAR.

## 69. Proposed files

Create:

```text
Core/include/phoneme/runtime/SuiteCodeCache.hpp
Core/src/runtime/SuiteCodeCache.cpp
Core/include/phoneme/vm/MetadataSerializer.hpp
Core/src/vm/MetadataSerializer.cpp
Core/Tests/SuiteCodeCacheTests.cpp
Core/Tests/MetadataSerializationTests.cpp
```

Update:

```text
Core/src/runtime/SuiteInstaller.cpp
Core/src/runtime/Runtime.cpp
Core/include/phoneme/vm/ClassRepository.hpp
Core/src/vm/ClassRepository.cpp
```

## 70. Cache contents

Persist only pointer-free, versioned data:

- source JAR hash;
- JAD/manifest identity needed for invalidation;
- core cache format version;
- architecture/endianness marker if representation depends on it;
- built-in API registry generation/hash;
- parsed class metadata;
- verifier result and required stack-map data;
- method/field indexes;
- parsed descriptors;
- decoded instructions;
- symbolic resolved operands that are safe to persist;
- resource central-directory/index metadata when beneficial.

Do not persist:

- native pointers;
- `shared_ptr` representation;
- live Java objects;
- static field values after application execution;
- RMS state;
- network/media handles;
- inline caches containing runtime IDs from a previous metadata generation.

## 71. Cache identity

A cache is valid only if all of the following match:

```text
SHA-256(JAR bytes)
cache format major/minor
core build compatibility version
built-in class registry fingerprint
verification policy version
endianness/data-layout marker
```

A mismatch, truncated file, checksum failure or unsupported version must delete/ignore the cache and rebuild it without failing the MIDlet launch.

## 72. Atomic write

Use the same crash-safe principles required for RMS:

1. write a temporary file in the same directory;
2. flush data;
3. validate header/checksum;
4. atomically replace the target;
5. never leave a partially written file as valid cache.

## 73. Security and sandboxing

- Cache path must remain suite-isolated.
- A suite must never load another suite's decoded metadata.
- Cache content must be treated as untrusted despite being locally generated.
- All offsets, lengths and counts require checked parsing.
- The JAR remains the source of truth.

## 74. Phase 7 acceptance

- Cold launch builds a valid cache.
- Warm launch avoids class parse/verify/decode work for cached classes.
- Corruption falls back safely.
- JAR replacement invalidates cache.
- Core cache-version change invalidates cache.
- Suite isolation tests pass.
- Startup wall time improves on large JARs without gameplay regressions.

---

# Phase 8 — Arena allocation and generational GC

## 75. Objective

Improve allocation speed, locality and GC pause behavior after typed arrays and stable metadata are complete.

This is the highest-risk phase and must not start before heap tests, root tests, sanitizer runs and real corpus baselines are strong.

## 76. Do not port the legacy heap directly

Do not import:

- `OopDesc` object layout;
- legacy near/far class pointers;
- task boundary objects;
- compiled-method area;
- phoneME mark bitmap code verbatim;
- pointer-compression assumptions;
- old finalizer infrastructure not required by CLDC behavior.

Preserve `ObjectRef` as a generation-checked stable handle.

## 77. Target handle/payload model

```text
ObjectRef(slot, generation)
        |
        v
HandleSlot
  - generation
  - occupied
  - age/generation flags
  - payload location
  - ClassId
        |
        v
Arena payload
  - instance fields
  - primitive array bytes
  - reference array handles
  - string text
```

Moving payload updates the handle slot, not every Java reference.

## 78. Initial arena allocator

Start with:

- bump allocation for small instance payloads;
- separate large-object allocation for large arrays;
- reusable free spans only after collection;
- alignment based on contained element type;
- checked byte sizes;
- no allocation in the mark loop after GC begins.

Keep handle-table allocation separate and stable.

## 79. Nursery design

After arena allocation is stable, add:

- young nursery for new small objects;
- age/promotion metadata in handle slots;
- minor collection scanning young roots and remembered old-to-young references;
- full collection fallback;
- large arrays allocated directly outside the nursery when measured thresholds justify it.

Do not guess thresholds permanently. Make them configurable for benchmarks and select defaults from corpus results.

## 80. Write barrier

Every reference store into an old object/reference array must record an old-to-young edge.

Barrier locations include:

- instance `putfield` of reference values;
- reference `aastore`;
- static field stores if static roots are handled through a separate root set;
- native helpers that mutate Java references;
- clone/copy operations.

The barrier must not run for primitive stores.

## 81. GC work buffers

Preallocate/reuse:

- mark stack;
- remembered set storage;
- root scratch storage;
- relocation/compaction metadata where used.

Avoid allocating a new `std::vector<ObjectRef>` proportional to all roots for every collection. Prefer visitor-based root enumeration directly into the mark queue.

## 82. Weak references

Retain current weak-reference semantics:

- weak referent does not keep the target alive;
- clear weak referent after marking determines it is unreachable;
- stale generation handles are treated as cleared;
- weak-reference processing order is deterministic enough for tests.

## 83. GC pause instrumentation

Record separately:

- root scan time;
- mark time;
- weak-reference processing time;
- sweep/compact time;
- handle updates;
- reclaimed bytes;
- promoted bytes;
- remembered-set size.

## 84. Phase 8 acceptance

- GC stress tests pass under ASan/UBSan.
- No stale payload pointers survive a collection.
- All native roots and scheduler roots remain valid.
- Minor GC reclaims short-lived allocations.
- Full GC remains correct.
- Maximum GC pause does not regress on representative games.
- Allocation throughput improves measurably.
- Memory remains bounded during long-running online and offline soak tests.

---

# Phase 9 — Optional dispatch and superinstruction work

## 85. Start only after earlier phases

Do not optimize interpreter dispatch before measuring the decoded interpreter. After metadata lookup, locks, arrays and frames are fixed, dispatch may become significant.

## 86. Candidate techniques

In priority order:

1. compact decoded instruction layout;
2. hot/cold opcode handler separation;
3. table-driven handler functions;
4. compiler-supported computed goto for host/device builds where portable and verified;
5. superinstructions for common safe sequences;
6. trivial accessor method fast path;
7. small-method inlining in decoded metadata only after strong evidence.

## 87. Superinstruction requirements

A superinstruction must:

- preserve original BCI mapping for every component instruction;
- preserve exception point and null-check ordering;
- preserve instruction-budget accounting policy;
- preserve scheduler maintenance polling often enough;
- preserve debugger/profiler visibility if those features rely on BCI;
- have differential tests against the unfused sequence.

Safe initial candidates may include:

- `aload_0` + resolved reference/int `getfield`;
- local load + small constant + integer operation;
- trivial getter method entry;
- direct final virtual call.

Do not fuse operations that can throw at multiple distinguishable BCIs until the exception mapping design supports it explicitly.

## 88. Phase 9 acceptance

- Dispatch benchmark improves beyond noise.
- Full correctness and corpus tests pass.
- No compiler-specific undefined behavior.
- iPhoneOS and host builds use the same Java semantics.
- A portable switch fallback remains available if computed goto is introduced.

---

## 89. Cross-cutting correctness invariants

Every phase must preserve these invariants.

### 89.1 Reference integrity

- Null reference remains `ObjectRef{}`.
- Slot zero is never a valid live object.
- Generation mismatch is always rejected.
- Reused slots increment generation and never produce generation zero.
- No raw payload pointer escapes a scope that can allocate or collect.

### 89.2 Class and method lifetime

- `ClassFile` and immutable runtime metadata live at least as long as any pointer/view into them.
- Cache invalidation cannot leave decoded methods pointing into freed classes.
- Adding/changing classpath archives invalidates dependent metadata as one generation.

### 89.3 Java exception ordering

Optimizations must preserve the order in which Java-visible checks occur. Examples:

- null check versus bounds check;
- class initialization versus static field access;
- receiver null versus method resolution;
- array store type check before mutation;
- monitor state checks;
- linkage exception timing.

Tests must assert exception class at minimum, and message where the project relies on stable messages.

### 89.4 Class initialization

Resolved metadata must not imply initialized class state.

Keep separate states for:

```text
loaded
linked/resolved
initializing
initialized
erroneous
```

Fast static access and `new` must call the existing initialization state machine when required.

### 89.5 GC safepoints

GC may run only where:

- all active frame roots are discoverable;
- native temporary roots are published through `NativeRootScope` or equivalent;
- no untracked raw heap pointer is live;
- monitor, scheduler, timer, media, network and UI roots are enumerable.

### 89.6 Thread blocking

A Java thread must not hold invalid transient C++ views across:

- monitor wait;
- sleep;
- join;
- I/O block;
- platform callback suspension;
- app background suspension.

### 89.7 Suite isolation

Metadata, static fields, heap objects, RMS, network handles and persisted caches remain isolated by suite/runtime ownership.

### 89.8 Shutdown ordering

Optimization must not regress forced destroy. Preserve the established cleanup sequence:

1. hide Canvas/UI;
2. close blocking I/O;
3. call `destroyApp(true)` with a finite budget;
4. shut down scheduler, media and timers;
5. flush/close RMS as required;
6. free VM-owned state.

No cache or profiler thread may outlive the runtime.

---

## 90. Testing strategy

## 90.1 Required test levels

Every phase must run:

### Focused unit tests

Tests for the new data structure or metadata behavior.

### VM fixture tests

Java fixtures exercising exact bytecodes and exception behavior.

### Differential tests

Where an old and new execution path coexist, run the same fixture through both and compare observable results.

### Host full suite

```sh
bash Core/Tools/test-all-host.sh
```

### Sanitizers

```sh
PHONEME_SANITIZE=1 bash Core/Tools/test-host.sh
```

Run additional focused sanitizer scripts when the changed module has one.

### iPhoneOS archive build and verification

```sh
bash Core/Tools/build-iphoneos.sh
bash Core/Tools/verify-iphoneos.sh
```

### Full regression owner entrypoint

```sh
bash Core/Tools/test-full-regression.sh
```

### Compatibility corpus

Run the existing JAR corpus and compare milestone classifications, not only process exit codes.

### Real-device soak

For memory, scheduler, network and background-sensitive phases, test multiple games concurrently/sequentially on a real arm64 iPhone.

## 90.2 New fixture requirements

Add Java fixtures covering:

- repeated instance/static field access;
- monomorphic and polymorphic virtual calls;
- interface calls;
- failed resolution cached repeatedly;
- class initialization failure;
- every primitive array kind;
- array min/max values;
- overlapping array copies;
- deep calls with category-2 values;
- GC with references only in locals/operand stack;
- weak references;
- monitor unwind through optimized frames;
- scheduler block/interrupt/timeout transitions;
- suite cache corruption and invalidation.

## 90.3 Performance regression policy

For each phase:

1. Record baseline and candidate on the same machine/configuration.
2. Use multiple runs and report median; include variance when large.
3. Reject performance claims based on one run.
4. Correctness always takes priority over a small speedup.
5. A change with no measurable benefit and substantial complexity should not be kept.
6. Do not hide regressions in startup, memory or GC while reporting only interpreter throughput.

A practical merge threshold for a major complex optimization is either:

- at least a meaningful double-digit improvement in its targeted metric on representative workloads; or
- a large memory reduction with no meaningful CPU regression; or
- removal of a demonstrated pathological pause/CPU problem.

Exact thresholds should be recorded with the benchmark because different phases target different metrics.

---

## 91. File ownership and conflict reduction

`Core/src/vm/Machine.cpp` is large and frequently edited. Minimize parallel conflicts by extracting new concepts into dedicated files before modifying the interpreter loop.

Recommended ownership split:

```text
Agent/task A: PerformanceCounters.* + benchmark scripts
Agent/task B: MetadataId.* + RuntimeMetadata.* + class indexes
Agent/task C: DecodedMethod.* + decode tests
Agent/task D: HeapExecutionAccess.* + heap tests
Agent/task E: ArrayKind.* + ArrayStorage.* + typed array tests
Agent/task F: ExecutionStackArena.* + frame tests
Agent/task G: Scheduler queue structure
Integration owner: Machine.cpp migration and full regression
```

Rules:

- only one integration owner edits the main interpreter loop at a time;
- module agents expose stable interfaces and tests first;
- integration commits remain small enough to bisect;
- never combine unrelated graphics/network/UI fixes with interpreter optimization commits;
- preserve uncommitted parallel work and report conflicts instead of overwriting them.

---

## 92. Suggested concrete commit plan

### Commit 1 — profiler infrastructure

- add compile-time-disabled counters;
- add snapshot/reset tests;
- no semantic runtime changes.

### Commit 2 — benchmark harness and baseline manifest

- add selected corpus manifest;
- add machine-readable output;
- capture baseline outside source tree.

### Commit 3 — strong metadata IDs

- add ID types and runtime metadata generation;
- preserve existing string lookup as slow path.

### Commit 4 — method/field indexes and descriptor cache

- remove linear method lookup after class publication;
- cache descriptors;
- bind native IDs.

### Commit 5 — decoded representation

- decode verified methods;
- add BCI mapping tests;
- do not switch default execution yet.

### Commit 6 — decoded interpreter parity

- execute simple opcode groups through decoded path;
- differential tests;
- expand incrementally by opcode family.

### Commit 7 — resolved fields/statics/classes

- direct field/static slots;
- preserve class initialization;
- profile warm-path misses.

### Commit 8 — resolved invocation and monomorphic call cache

- direct static/special/native targets;
- virtual/interface call-site cache;
- dispatch/error tests.

### Commit 9 — heap execution access

- add execution ownership token;
- migrate interpreter field/array operations;
- retain public locked API.

### Commit 10 — typed arrays foundation

- add storage types and allocation;
- compatibility wrappers.

### Commit 11 — typed array bytecodes and arraycopy

- migrate interpreter and bulk copy;
- memory/GC tests.

### Commit 12 — native byte-buffer migrations

- graphics/image;
- network;
- RMS;
- media;
- validate async copy ownership.

### Commit 13 — execution stack arena

- frame offsets and roots;
- migrate interpreter;
- category-2/deep-call tests.

### Commit 14 — scheduler O(1) transitions

- intrusive membership;
- precise interrupt wakeups;
- idle CPU checks.

### Later commits

- suite cache;
- arena allocator;
- nursery/minor GC;
- measured dispatch/superinstructions.

---

## 93. Prohibited shortcuts

Do not:

- copy legacy phoneME source into `Core/src`;
- compile Vendor sources as a temporary fallback;
- disable verifier checks to gain speed;
- skip class initialization checks after resolving a static operand;
- store `ClassFile*`, method pointers or native pointers in serialized cache;
- store platform object pointers in Java fields;
- use unchecked casts or pointer arithmetic in heap storage;
- remove generation checks from `ObjectRef` access;
- expose mutable heap pointers to asynchronous callbacks;
- make all heap methods unlocked globally;
- convert heap mutex to recursive mutex and claim the hot-path lock issue is solved;
- add an intrinsic solely for one JAR without a generic fallback and regression fixture;
- introduce computed goto before decoded execution is stable and measured;
- introduce a JIT as part of this handoff;
- report a phase complete with only unit tests and no corpus run;
- report a performance gain without baseline data.

---

## 94. Definition of done by optimization area

### Metadata/decoded execution is done when

- warmed field/static/special/native operations use resolved IDs/offsets;
- virtual call sites have measured inline caches;
- original BCI behavior is preserved;
- no hot-path string construction remains for resolved operations;
- full compatibility corpus is unchanged or improved.

### Heap fast access is done when

- interpreter heap operations do not acquire a mutex per operation;
- external callbacks remain safe through queued completion;
- debug ownership checks exist;
- sanitizers and background tests pass.

### Typed arrays are done when

- each Java primitive kind uses its natural width;
- primitive arrays are not GC-scanned;
- `System.arraycopy` is specialized and correct;
- byte-oriented native paths use bulk operations;
- memory reduction is measured.

### Frame arena is done when

- repeated Java calls do not allocate independent vectors;
- category-2 and root semantics pass;
- monitor/exception unwind remains correct;
- call-heavy benchmark improves or does not regress.

### Scheduler optimization is done when

- state transitions do not linearly scan queues;
- wakeups are precise;
- idle/background CPU is not worse;
- online games survive suspend/resume/close.

### Generational GC is done when

- minor and full collection are both correct;
- write barriers cover every reference store;
- payload movement cannot stale Java references;
- GC stress and real-device soak remain bounded;
- pause/allocation improvements are measured.

---

## 95. Final validation matrix

Before declaring the full optimization program complete, record results for:

| Area | Required evidence |
|---|---|
| Host correctness | `test-all-host.sh` passes |
| Sanitizers | ASan/UBSan pass |
| iPhoneOS archive | build and verify scripts pass |
| App integration | Debug and Release app build without signing |
| Corpus | milestone classifications compared with baseline |
| Startup | median time to first UI/frame |
| Active performance | bytecodes/s, CPU, frame interval |
| Memory | Java heap, process memory, large byte-array behavior |
| GC | count, max pause, reclaimed/promoted bytes |
| Scheduler | state transitions, idle CPU, wakeup correctness |
| Background | hide/resume without queued UI flood or black screen |
| Network | online game reconnect/close/suspend behavior |
| RMS | save/settings survive exit and relaunch |
| Shutdown | forced-destroy sequence completes without hang |
| Suite isolation | no cross-suite object/cache/RMS leakage |

Attach exact device model, iOS version, build configuration and JAR hashes to performance reports.

---

## 96. Immediate next action

Continue Phase 2 only. Do not begin typed arrays, heap arenas or scheduler rewrites yet.

Concrete next task:

1. inspect current `git status` and preserve unrelated changes;
2. add a per-`RuntimeMethod` operand-resolution side table indexed by decoded operand index;
3. migrate resolved field and direct static/special call targets from Machine-local maps into that side table without changing execution semantics;
4. implement explicit `unresolved -> resolving -> resolved/failed` states and cache stable Java linkage failures at the original BCI;
5. extend decoded-vs-legacy differential fixtures with heap/static-field effects and stable throwable class/message checks;
6. run decoded OFF, decoded ON, profiling ON, differential and ASan/UBSan suites;
7. compare the resulting profile with `phase2-all-operands-final` before migrating class/type operands.

Keep `PHONEME_ENABLE_DECODED_EXECUTION` disabled by default until all resolved operand categories and the differential corpus pass.

---

## 97. Handoff reporting template

Every implementation handoff/update should use this format:

```text
Implemented:
- exact files and symbols

Behavior preserved:
- Java semantics and compatibility conditions checked

Tests run:
- exact commands and pass/fail counts

Performance before/after:
- machine/configuration
- benchmark/JAR hashes
- median and variance

Known limitations:
- unresolved risks or unconverted hot paths

Next step:
- one bounded follow-up phase
```

A phase is not complete when code compiles. It is complete only when its acceptance criteria and validation evidence are recorded.
