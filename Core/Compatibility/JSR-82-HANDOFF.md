# JSR-82 Bluetooth Core handoff

Updated: 2026-08-03

## Corpus evidence

The local compatibility corpus contains multiple games with `javax.bluetooth` references. The first implementation target is:

- `jar_test/Pes09.jar`
- SHA-256: `befe750941c140e233f76eb167f8602ddaef736d02eb6f36a914427a838b89a5`
- MIDlet: `PES 2009` 1.0.0, Konami, CLDC-1.0/MIDP-2.0

Additional confirmed JARs include:

- `jar_test/4x4XR.jar` — `0716bc8e6116bd5265c71a496230af5278531cfacef1e76c6922edc8217e3dcd`
- `jar_test/Juiced3D.jar` — `30ac1cb71bc6ebc5bc4f850e4250493e903d0e16940cc4f1c166fa0d79c6940a`

A scan stopped after the first 40 matching JARs, so JSR-82 is not a speculative API in this corpus.

## PES 2009 API usage

Static dependency analysis and bytecode inspection show use of:

- `BluetoothStateException`
- `LocalDevice.getLocalDevice`, discoverable mode, discovery agent and device class
- `DiscoveryAgent.startInquiry`, `searchServices`, cancellation and callbacks
- `DiscoveryListener`
- `RemoteDevice` address/friendly name
- `UUID(long)` and `UUID(String, boolean)`
- `ServiceRecord.getConnectionURL`
- `L2CAPConnectionNotifier.acceptAndOpen`
- `L2CAPConnection.send`, `receive`, `ready`, and `close`
- `Connector.open("btl2cap://...")`

## Implemented in the C++ core

The current module is isolated in:

- `Core/src/vm/BluetoothBuiltinClasses.cpp`
- `Core/src/vm/BluetoothNatives.cpp`
- `Core/src/vm/BluetoothNatives.hpp`

Implemented pure-VM semantics:

- Built-in JSR-82 class/interface surface required by the corpus.
- JSR-82 public constants through class initialization.
- `UUID` normalization, equality, hash and validation.
- `LocalDevice` per-VM singleton and validated discoverable modes.
- Deterministic no-device discovery completion so a game does not wait forever when no host Bluetooth adapter exists.
- Monotonic service-search transaction IDs and no-record completion callback.
- `DeviceClass` masks.
- `RemoteDevice` identity/address behavior.
- `DataElement` type/range validation, boolean/integer/object values, sequence storage, enumeration and mutations.
- Explicit exceptions for invalid values instead of fake successful results.

Regression coverage:

- `Core/Tests/fixtures/BluetoothOps.java`
- `Core/Tests/BluetoothVmTests.cpp`
- `Core/Tools/test-bluetooth-host.sh`

## Deliberately not claimed complete

The following still require a transport/platform submodule and must not be reported as working:

1. Real inquiry and service discovery.
2. Service record population/update.
3. `btl2cap://` parsing and `Connector.open` dispatch.
4. L2CAP client/listener transport, MTU, cancellation and close semantics.
5. Per-suite Bluetooth permission gates.
6. iOS capability policy and user-facing unsupported-device behavior.

Until a transport adapter is installed, Core exposes deterministic zero-device discovery behavior. It must not synthesize remote devices, service records or successful L2CAP connections.

## Proposed adapter boundary

Keep iOS frameworks outside the VM:

```text
Core/include/phoneme/bluetooth/BluetoothAdapter.hpp
Core/include/phoneme/bluetooth/BluetoothRegistry.hpp
Core/src/bluetooth/BluetoothRegistry.cpp
Core/src/vm/BluetoothConnectionNatives.cpp
phoneME/Services/BluetoothPlatformAdapter.swift
```

The C++ adapter should use opaque generation-checked handles, asynchronous completion, cancellation tokens, per-MIDlet ownership and scheduler wakeups. No Swift/Objective-C object pointer may be stored in Java fields.

## Security model

Before inquiry, service search, discoverable mode or connection open, the operation must call `PermissionPolicy::require` using the suite declarations for the corresponding Bluetooth client/server permission. Permission prompting must happen outside the global Runtime lock. A denied or undeclared operation must surface `java/lang/SecurityException`.

## Acceptance matrix for the transport phase

- No-adapter path completes inquiry without deadlock and opens no fake connection.
- Client/server discovery and connection between two supported hosts.
- Multiple concurrent MIDlets cannot see or close each other's Bluetooth handles.
- Cancel inquiry/search/connect wakes blocked Java threads exactly once.
- Close during send/receive is race-safe.
- MTU and byte range validation.
- Background/suspend cleanup.
- Host normal plus ASan/UBSan; real-device iPhoneOS validation for the selected adapter technology.
