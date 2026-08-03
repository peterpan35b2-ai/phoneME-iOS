# Handoff: Full J2ME Core Port — C++23, iPhoneOS 16+

Ngày cập nhật: 2026-08-02  
Trạng thái tài liệu: chuẩn triển khai bắt buộc  
Phạm vi: `Core/` độc lập hoàn toàn, không phụ thuộc source hoặc runtime archive của phoneME vendor

---

## 1. Mục tiêu

Xây dựng một J2ME runtime mới bằng C++23 có khả năng chạy ổn định các MIDlet/game J2ME online và offline trên iPhoneOS 16 trở lên.

Core phải tự chứa toàn bộ logic cần thiết cho:

- JVM/CLDC virtual machine.
- CLDC class library cần thiết.
- MIDP lifecycle và application management.
- RMS, filesystem, networking và security policy.
- LCDUI, Canvas, GameCanvas, input và graphics command stream.
- Media API và audio lifecycle.
- Bộ compatibility layer dành cho các game J2ME cũ hoặc không hoàn toàn đúng chuẩn.

Core không được yêu cầu `Vendor/phoneME`, `classes.zip`, generated source của phoneME, KNI, PCSL, MIDP C runtime cũ hoặc bất kỳ binary runtime vendor nào để build hay chạy.

phoneME chỉ được dùng như tài liệu tham khảo hành vi trong quá trình nghiên cứu. Không copy nguyên module, không link binary và không tạo fallback về core cũ.

---

## 2. Các ràng buộc không được thay đổi

### 2.1 Nền tảng

- Chỉ hỗ trợ `iphoneos`.
- Kiến trúc `arm64`.
- Deployment target tối thiểu: iOS 16.0.
- Không xây simulator runtime trong Core production.
- Không JIT, không cấp vùng nhớ executable động.
- VM production dùng interpreter và có thể bổ sung threaded interpreter nếu vẫn tuân thủ chính sách iOS.

### 2.2 Ngôn ngữ và ABI

- Toàn bộ VM/runtime core viết bằng C++23.
- Public integration surface dùng C ABI ổn định.
- Swift/UIKit/Objective-C chỉ nằm ở lớp platform/app bridge ngoài VM.
- Không để Swift object, Objective-C object hoặc native pointer lọt vào Java slot, heap object hoặc serialized state.
- Java `long` và `double` luôn là giá trị category-2 theo đặc tả Java, không phụ thuộc kích thước pointer native.
- Không cast pointer sang `int32_t`, `long` hoặc bất kỳ kiểu có thể hẹp hơn `uintptr_t`.

### 2.3 Dependency

Dependency production được phép:

- C++ standard library/libc++.
- zlib hoặc implementation ZIP nội bộ tương đương.
- API POSIX có trên iPhoneOS khi được bọc qua platform layer.
- Apple frameworks thông qua adapter C/Objective-C++ bên ngoài lớp VM thuần C++.

Không được phép:

- `Vendor/phoneME`.
- `phoneME/Resources/PhoneMERuntime/classes.zip`.
- KNI, PCSL, CVM, MIDP C runtime hoặc generated source của phoneME.
- JNI runtime.
- Dynamic library tải từ ngoài app bundle.
- Runtime code generation hoặc executable memory.

---

## 3. Baseline hiện tại

Core hiện có cấu trúc độc lập:

```text
Core/
├── include/
│   ├── JarArchive.h
│   ├── PhoneMECore.h
│   └── phoneme/
├── src/
│   ├── api/
│   ├── archive/
│   ├── classfile/
│   ├── platform/
│   ├── runtime/
│   └── vm/
├── Tests/
├── Tools/
├── CMakeLists.txt
├── README.md
└── FULL_PORT_HANDOFF.md
```

Đã có:

- ZIP/JAR reader dựa trên mapped file.
- Class-file parser cơ bản.
- Descriptor parser và slot storage.
- Heap/object handle nền tảng.
- Class repository và class layout.
- Interpreter với một phần bytecode.
- Native method registry kiểu hóa.
- Suite install và MIDlet lifecycle scaffolding.
- Framebuffer và queue sự kiện cơ bản.
- Built-in boot class registry bằng C++, không cần `classes.zip`.
- C API để app iOS tạo runtime, install JAR và điều khiển lifecycle.
- Host test và iPhoneOS arm64 build/verification.

Chưa hoàn thiện:

- Full bytecode set.
- Verifier, StackMap và CLDC preverification compatibility.
- Java exception semantics đầy đủ.
- Threads, scheduler và monitors.
- CLDC/MIDP class library đầy đủ.
- RMS.
- Networking đầy đủ.
- LCDUI/Canvas/GameCanvas đầy đủ.
- Media API đầy đủ.
- Compatibility với corpus game thật ở mức production.

Không được mô tả baseline hiện tại là “full J2ME”.

### 3.1 Tiến độ triển khai đã xác nhận ngày 2026-08-02

Đã hoàn tất và có regression test:

- Java exception table, implicit exception creation, catch matching và unwind qua nhiều frame.
- `athrow`, `finally`, uncaught throwable boundary và lifecycle failure isolation.
- Stack/category-2 opcodes, conversions, comparisons, dense/sparse switch,
  `jsr`/`ret`, casts, array checks và `multianewarray`.
- Virtual/special/static/interface dispatch, constructor resolution, abstract,
  native và linkage error paths.
- Class initialization erroneous state: lần đầu dùng
  `ExceptionInInitializerError`, các lần sau `NoClassDefFoundError`, không chạy
  lại `<clinit>`.
- CLDC `StackMap`, Java `StackMapTable`, structural verifier và CFG type-state
  verifier cho locals, operand stack, branch merge, handler, return và
  uninitialized constructor state.
- Canonical class mirror cho class literal và static synchronized method.
- Reentrant monitor table, `monitorenter`/`monitorexit`, instance/static
  synchronized method và release khi exception unwind.
- Mark/sweep root publication cho active frames, static fields, interned strings,
  class mirrors và held monitors; allocation retry sau safepoint collection.
- Host corpus, ASan/UBSan, iPhoneOS arm64 build/verification và Release app link.

Chưa hoàn tất, không được đánh dấu full port:

- Suspendable execution context và scheduler Java thread thật.
- Contended monitor handoff, `wait`/`notify`, `sleep`, `yield` và thread cleanup.
- Emergency OOM reserve và root publication cho mọi native/platform handle.
- Toàn bộ CLDC/MIDP, RMS, networking/TLS, LCDUI/Canvas/GameCanvas, media và
  compatibility corpus trên thiết bị thật.

---

## 4. Định nghĩa hoàn thành

Full port chỉ được xem là hoàn thành khi đồng thời đạt tất cả điều kiện sau:

1. Xóa hoặc di chuyển toàn bộ `Vendor/phoneME` ra ngoài repository mà Core và app vẫn build được.
2. App bundle không chứa `classes.zip`, phoneME runtime archive hoặc legacy core binary.
3. Core chạy được toàn bộ test corpus đã duyệt mà không crash native, deadlock hoặc corrupt heap.
4. MIDlet lifecycle hoạt động đúng khi mở, pause, background, resume, hide, chuyển app và destroy.
5. Nhiều MIDlet có thể tồn tại đồng thời mà không chia sẻ sai state, RMS, socket, class statics hoặc framebuffer.
6. Game online giữ kết nối đúng theo policy của app, tự xử lý reconnect và không làm chết runtime khác.
7. LCDUI và Canvas có hành vi tương thích đủ để các game trong corpus sử dụng được toàn bộ menu, input và rendering.
8. Media phát, pause, resume, seek và background đúng theo capability của iOS.
9. Không còn lỗi 32-bit/64-bit, undefined behavior hoặc data race được phát hiện bởi bộ kiểm tra hỗ trợ.
10. CPU, RAM và battery usage đạt budget đã định nghĩa trong tài liệu này.
11. Release build chạy được trên thiết bị thật tối thiểu iPhone 14/iOS 16.0 và các phiên bản iOS mới hơn trong matrix.
12. Tất cả API chưa hỗ trợ phải trả lỗi Java hợp lệ; không abort process và không để runtime ở trạng thái treo.

---

## 5. Kiến trúc đích

### 5.1 Phân lớp

```text
C API / Host Integration
        │
Runtime Manager / Multi-MIDlet Isolation
        │
MIDP Services
├── Lifecycle / AMS
├── LCDUI / Canvas / Input
├── RMS
├── Networking
├── Media
└── Push / Permissions / Properties
        │
CLDC Class Library and Native Bindings
        │
JVM
├── Loader / Linker / Verifier
├── Interpreter
├── Exceptions
├── Threads / Monitors
├── Heap / GC
└── Native Method Registry
        │
Platform Abstraction
├── Filesystem
├── Clock / Timer
├── Socket / TLS
├── Audio
├── Graphics Surface
└── Logging / Diagnostics
```

### 5.2 Quy tắc dependency

- `vm/` không include UIKit, Foundation, AVFoundation hoặc Swift-generated header.
- `classfile/` không phụ thuộc runtime hoặc platform UI.
- `runtime/` được phép phụ thuộc `vm/`, nhưng VM không phụ thuộc ngược vào runtime UI.
- MIDP service giao tiếp với iOS qua interface hoặc C callback table.
- Mỗi adapter phải có fake implementation để chạy host test.
- Không dùng singleton global cho state thuộc từng runtime hoặc từng MIDlet.

### 5.3 Isolation

Mỗi MIDlet instance phải có:

- Java heap riêng hoặc heap arena được phân vùng an toàn.
- Class initialization state riêng.
- Static fields riêng.
- Thread set riêng.
- Event queue riêng.
- Framebuffer/display state riêng.
- RMS namespace riêng.
- Connection registry riêng.
- Media player registry riêng.
- Lifecycle state machine riêng.

Shared cache chỉ được dùng cho dữ liệu immutable như parsed archive metadata hoặc decoded immutable asset, và phải có ownership rõ ràng.

---

## 6. Phạm vi JVM

### 6.1 Class-file loader

Phải hỗ trợ class-file phù hợp với J2ME/CLDC và compatibility thực tế:

- Constant pool đầy đủ cho class versions cần hỗ trợ.
- Fields, methods, interfaces và attributes.
- `Code`, exception table, line numbers và local variable metadata khi cần debug.
- CLDC `StackMap` attribute.
- Java SE `StackMapTable` ở mức compatibility cần thiết cho các JAR được build bằng toolchain mới hơn.
- Synthetic/bridge flags khi xuất hiện trong game đã được normalize.
- Modified UTF-8 đúng semantics JVM.
- Kiểm tra bounds và integer overflow cho mọi offset/length.

Parser không được đọc ngoài buffer hoặc tin tưởng kích thước từ JAR.

### 6.2 Loading, linking và initialization

Cần triển khai:

- Bootstrap class registry bằng C++.
- Application classpath từ MIDlet JAR.
- Parent-first cho namespace hệ thống.
- Cấm JAR ghi đè `java/*`, `javax/microedition/*` và namespace internal.
- Superclass/interface resolution.
- Field và method resolution.
- Virtual/interface dispatch.
- Class initialization theo đúng thứ tự và chỉ một lần trên mỗi runtime.
- Circular initialization handling.
- Constant static field preparation.
- Array class synthesis.

### 6.3 Bytecode interpreter

Phải triển khai toàn bộ opcode cần cho Java ME, bao gồm:

- Constants và local load/store.
- Stack manipulation.
- Integer, long, float và double arithmetic.
- Conversion và comparison.
- Branches, `tableswitch`, `lookupswitch`.
- Field access.
- Static, special, virtual và interface invocation.
- Object/array creation.
- Primitive và reference arrays.
- Type checks, casts và `instanceof`.
- Exceptions và `athrow`.
- Monitors.
- Null, bounds, divide-by-zero và negative-size checks.
- Correct category-2 slot behavior.

Mỗi opcode phải có unit test thành công và test lỗi tương ứng.

### 6.4 Verifier

Verifier phải:

- Kiểm tra control-flow graph.
- Kiểm tra stack depth và local types.
- Validate branch targets.
- Validate exception handlers.
- Hỗ trợ CLDC StackMap.
- Chấp nhận các compatibility case đã được định nghĩa rõ, không tắt verifier toàn cục.
- Trả `VerifyError`, `ClassFormatError` hoặc lỗi Java phù hợp.

Không được sửa bytecode ngầm trong lúc execute. Mọi normalization/preverification phải là bước rõ ràng, deterministic và có test.

### 6.5 Exceptions

Phải có:

- Java throwable object đầy đủ tối thiểu cho CLDC/MIDP.
- Frame unwinding qua exception table.
- Catch type resolution.
- Native-to-Java exception conversion.
- `finally` semantics thông qua bytecode chuẩn.
- Uncaught exception boundary ở từng MIDlet/thread.
- Không dùng C++ exception cho Java control flow.

Core hiện build với `-fno-exceptions`; giữ nguyên trừ khi có quyết định kiến trúc được review.

### 6.6 Threads và monitors

Cần triển khai:

- Java `Thread` lifecycle.
- Scheduler cooperative hoặc hybrid phù hợp iOS.
- Runnable dispatch.
- Sleep, yield, interrupt.
- Per-object monitor.
- Reentrant lock.
- `wait`, `notify`, `notifyAll`.
- Thread termination khi MIDlet destroy.
- Safepoint cho GC và lifecycle suspension.

Không tạo một native OS thread cho mọi Java thread nếu làm tăng CPU/battery. Ưu tiên scheduler có giới hạn worker và fairness đo được.

### 6.7 Heap và GC

Yêu cầu:

- Object/array layout 64-bit-safe.
- Stable object handles; không expose address trực tiếp.
- Root scanning từ frames, statics, native handles và service registries.
- Mark/sweep hoặc generational collector phù hợp memory footprint.
- Stop-the-world có timeout và diagnostics.
- Không collect khi native binding chưa publish roots.
- Weak/soft references chỉ triển khai khi API yêu cầu.
- Out-of-memory phải ném `OutOfMemoryError`, không abort app.

GC metrics tối thiểu:

- Heap committed/used.
- Object count.
- Collection count.
- Pause time p50/p95/max.
- Allocation rate.

---

## 7. CLDC class library

Boot classes phải được mô tả và đăng ký từ Core C++, không phụ thuộc prebuilt vendor archive.

Phạm vi tối thiểu:

### 7.1 `java.lang`

- Object, Class, String, StringBuffer.
- Throwable hierarchy.
- Boolean, Byte, Short, Integer, Long, Character.
- Float/Double nếu profile/game yêu cầu.
- Math, System, Runtime.
- Thread, Runnable.
- Class loading helpers và reflection tối thiểu theo CLDC.

### 7.2 `java.io`

- InputStream/OutputStream.
- ByteArray streams.
- DataInput/DataOutput streams.
- Reader/Writer subset nếu profile yêu cầu.
- UTF encoding đúng Java ME.

### 7.3 `java.util`

- Vector, Stack, Hashtable.
- Enumeration.
- Calendar, Date, TimeZone.
- Random.
- Timer/TimerTask nếu thuộc target profile.

### 7.4 Build strategy

Không đưa Java class archive vendor vào app. Một trong hai phương án được chấp nhận:

1. Implement class behavior trực tiếp dưới dạng built-in class metadata và native methods bằng C++.
2. Duy trì source Java sạch do project sở hữu, compile deterministic trong build Core rồi nhúng generated byte array/object archive vào static library.

Nếu dùng phương án 2:

- Source Java phải nằm trong `Core/`.
- Build phải tái tạo được artifact từ source.
- Không được copy từ phoneME vendor.
- Artifact nhúng phải được verify bằng hash/provenance.
- Core copy riêng vẫn build mà không cần repository ngoài.

---

## 8. MIDP và AMS

### 8.1 MIDlet lifecycle

State machine bắt buộc:

```text
Installed → Constructed → Active ↔ Paused → Destroyed
                         ↘ Error
```

Yêu cầu:

- Constructor, `startApp`, `pauseApp`, `destroyApp` đúng semantics.
- `notifyPaused`, `resumeRequest`, `notifyDestroyed`.
- Không gọi lifecycle đồng thời trên cùng MIDlet.
- Lifecycle callback không chạy dưới global runtime lock.
- Timeout và diagnostics cho callback treo.
- Destroy phải thu hồi thread, socket, media, display và RMS handle.
- Lỗi của một MIDlet không được làm hỏng MIDlet khác.

### 8.2 Suite metadata

Hỗ trợ:

- MANIFEST.MF parsing.
- JAD override rules.
- Multiple MIDlets trong một suite.
- MIDlet name/icon/class mapping.
- Permissions và properties.
- Vendor/version/install identity.
- Deterministic suite ID và storage namespace.

### 8.3 Multi-app

Phải test tối thiểu:

- Ba game chạy đồng thời.
- Chuyển foreground liên tục.
- Một game crash Java trong khi hai game còn lại tiếp tục.
- Một game giữ socket, một game phát media, một game ở Canvas.
- Pause/resume ngẫu nhiên trong stress test.
- Không vượt CPU budget hoặc deadlock sau thời gian dài.

---

## 9. LCDUI, Canvas và graphics

### 9.1 LCDUI model

Core phải quản lý model độc lập cho:

- Display và Displayable.
- Form, List, TextBox, Alert.
- Item hierarchy.
- Command và CommandListener.
- Ticker nếu cần.
- Focus, selection và traversal.
- Screen transitions.

Bridge iOS chỉ render state do Core phát ra và gửi interaction về Core. UIKit không được tự thay đổi Java-visible state ngoài event protocol.

### 9.2 Command mapping

Mapping phải deterministic:

- `BACK`, `CANCEL`, `EXIT` ưu tiên navigation/back position.
- `OK`, `ITEM`, `SCREEN` ưu tiên primary action.
- Left/right softkey semantics phải tương thích game.
- Không tự chuyển `List` sang dropdown nếu làm thay đổi behavior.
- Command order phải theo priority và type rules của MIDP.

### 9.3 Canvas/GameCanvas

Cần hỗ trợ:

- `paint`, repaint coalescing và serviceRepaints.
- Fullscreen mode.
- Key press/release/repeat.
- Pointer press/drag/release.
- Game actions.
- GameCanvas key state.
- Double buffering.
- Clip, translate, color, stroke.
- Images, regions, transforms và sprites cần thiết.
- Fonts và text metrics tương thích.

### 9.4 Graphics pipeline

- Render target mặc định RGBA/BGRA được định nghĩa rõ.
- Không scale asset hoặc framebuffer nhiều lớp không cần thiết.
- Không thực hiện UIKit work trên VM thread.
- Frame publish dùng generation/token để tránh hiển thị frame của app cũ.
- Khi app background, giảm hoặc dừng render tick nhưng không phá VM/network policy.
- Capture chỉ lấy vùng game/app theo yêu cầu UI.

---

## 10. RMS và filesystem

### 10.1 RMS

Triển khai đầy đủ:

- Open/create/delete record store.
- Add/set/delete/get record.
- Enumeration, comparator và filter.
- Version/last modified/size available.
- Listener nếu profile yêu cầu.
- Atomic metadata update.
- Crash-safe commit.
- Per-suite isolation.
- File locking hoặc serialized access.

Không được mất dữ liệu khi:

- App bị kill.
- Thiết bị hết pin.
- MIDlet crash.
- Hai thread truy cập cùng record store.

### 10.2 Filesystem

- Chỉ expose sandbox được cấp phép.
- Normalize path và chặn traversal.
- Mapping memory card rõ ràng.
- Không dùng shared storage giữa game nếu chưa được policy cho phép.
- Filename encoding phải ổn định với Unicode/Vietnamese.

---

## 11. Networking

Phạm vi:

- `Connector` URL parsing.
- TCP socket.
- UDP datagram.
- HTTP/HTTPS.
- DNS.
- Timeouts.
- Input/output stream semantics.
- Connection close và half-close phù hợp.
- TLS validation qua platform adapter.
- Proxy/header/redirect behavior cần cho game corpus.

Yêu cầu ổn định:

- Không tự đóng socket chỉ vì MIDlet bị hide nếu policy cho phép chạy nền.
- Background policy phải tập trung tại Runtime Manager, không rải logic trong từng connection.
- Socket callback không giữ raw pointer đến object có thể bị GC.
- Mỗi async operation dùng handle/generation an toàn.
- Cancel phải idempotent.
- Resume sau network transition không được reuse descriptor đã invalid.

Test:

- Local echo TCP/UDP.
- HTTP status/body/chunked/redirect.
- TLS success/failure.
- DNS failure.
- Timeout/cancel.
- Network loss/reconnect.
- Background/foreground loop.
- Nhiều MIDlet kết nối đồng thời.

---

## 12. Media

Phạm vi MMAPI tối thiểu theo corpus:

- Manager/createPlayer.
- DataSource từ resource, file và URL.
- Realize/prefetch/start/stop/close.
- Loop count.
- Volume control.
- Tone/MIDI nếu game sử dụng.
- Audio stream formats phổ biến trong J2ME game.
- Player events.

Kiến trúc:

- Java player object giữ opaque media handle.
- Actual AVFoundation object nằm trong iOS media adapter.
- Callback về Core qua C ABI và generation token.
- Không callback vào object đã close/destroy.
- Audio session được quản lý tập trung giữa nhiều MIDlet.
- Background audio chỉ hoạt động khi app capability và user setting cho phép.

---

## 13. Platform abstraction

Tạo interface rõ ràng cho:

- Monotonic clock và wall clock.
- Sleep/timer scheduling.
- Random bytes.
- Filesystem operations.
- Socket/TLS operations.
- Audio/media operations.
- Clipboard/vibration nếu cần.
- Locale/timezone.
- Logging và crash breadcrumbs.
- Graphics surface publish.

Mọi interface phải có:

- iPhoneOS implementation.
- Host fake/test implementation.
- Ownership/lifetime contract.
- Threading contract.
- Cancellation contract.
- Error mapping sang Java exception.

---

## 14. Error handling

### 14.1 Nguyên tắc

- Không `abort`, `assert` hoặc `std::terminate` với lỗi do JAR/user input gây ra.
- Assertions chỉ dành cho invariant nội bộ trong debug; release phải trả lỗi kiểm soát được.
- Mọi parser dùng checked arithmetic.
- Mọi public C API validate null, range và state.
- Không để lỗi native khiến runtime mutex bị giữ vĩnh viễn.

### 14.2 Error domains

Duy trì phân biệt:

- Invalid host argument.
- Invalid runtime state.
- Malformed JAR/class.
- Verification/linkage error.
- Java exception.
- Platform I/O error.
- Resource exhaustion.
- Unsupported API/profile.

C API trả status code ổn định và cung cấp structured last-error information, không chỉ một số âm chung chung.

---

## 15. Thread safety và lock policy

- Ghi rõ lock order trong source.
- Không gọi Java code, UIKit, network callback hoặc media callback khi đang giữ global lock.
- Dùng per-runtime/per-service locks thay vì một mutex toàn cục.
- Event queue phải bounded và có backpressure/drop policy rõ ràng.
- Framebuffer publish không block VM lâu.
- Async callback phải kiểm tra runtime generation và app generation.
- Destroy/cancel có thể gọi nhiều lần mà không crash.

Lock-order đề xuất:

```text
Runtime manager
  → Application state
    → VM scheduler
      → Heap/GC
        → Service-specific registry
```

Không lấy lock theo chiều ngược lại.

---

## 16. Performance budget

Budget ban đầu trên iPhone 14/iOS 16, Release build:

### Idle/background

- Paused MIDlet không media/network activity: CPU trung bình gần 0%, không polling loop.
- Hidden MIDlet có socket nhưng không traffic: không busy wait.
- Không render frame khi không cần.

### Active game

- Render target tối đa 60 FPS nếu game yêu cầu.
- Không ép game 30 FPS bằng sleep cố định.
- Scheduler phải tôn trọng timing của game nhưng có fairness giữa nhiều MIDlet.
- Không decode lại asset ở mỗi frame.
- Không tạo bitmap trung gian không cần thiết.

### Memory

- Per-runtime heap limit cấu hình được.
- Memory warning từ iOS phải dẫn đến cache trim/GC hợp lệ.
- Không giữ frame/image cũ sau khi app generation thay đổi.
- Không chia sẻ mutable resource giữa suite.

Các con số p50/p95 cụ thể phải được thiết lập sau khi có benchmark corpus và lưu trong benchmark report, không đoán bằng cảm giác.

---

## 17. Test strategy

### 17.1 Unit tests

Bắt buộc cho:

- Byte reader và checked arithmetic.
- ZIP/JAR parser.
- Class parser.
- Descriptor parser.
- MUTF-8.
- Every opcode.
- Method/field resolution.
- Class initialization.
- Exception unwind.
- Heap allocation/GC.
- Monitor/thread state.
- RMS transactions.
- URL/Connector parsing.
- LCDUI command ordering.

### 17.2 Integration tests

- Install/start/pause/resume/destroy MIDlet.
- MIDlet inheritance từ built-in `MIDlet` không có `classes.zip`.
- Multiple MIDlets.
- Canvas input/render.
- RMS persistence sau restart.
- Socket/media lifecycle.
- Background/foreground transitions.

### 17.3 Negative tests

- Truncated ZIP.
- ZIP bomb limits.
- Invalid constant pool.
- Invalid branch target.
- Invalid StackMap.
- Recursive class hierarchy.
- Missing native method.
- OOM.
- Socket cancellation race.
- Destroy trong lúc callback.
- Rapid app switching.

### 17.4 Fuzzing

Fuzz target tối thiểu:

- ZIP central directory/local headers.
- Class-file parser.
- Descriptor parser.
- MUTF-8 decoder.
- Manifest/JAD parser.
- Image decoders thuộc Core nếu có.

Fuzzer có thể chạy host-side; production Core vẫn chỉ target iPhoneOS.

### 17.5 Stress tests

- 3–5 MIDlet trong ít nhất 30 phút.
- 1.000 vòng switch foreground.
- 1.000 vòng pause/resume.
- Repeated install/uninstall.
- Network loss loop.
- Memory warning loop.
- Media start/stop loop.
- RMS concurrent writes.

Không chấp nhận “không crash trong vài phút” làm bằng chứng ổn định.

---

## 18. Compatibility corpus

Duy trì corpus có license/phạm vi sử dụng hợp lệ, gồm:

- MIDlet LCDUI đơn giản.
- Canvas game offline.
- Game dùng sprites/game layer.
- Game online TCP.
- Game HTTP/HTTPS.
- Game RMS nặng.
- Game media/audio.
- JAR có class version/StackMap khác nhau.
- JAR từ Nokia/Symbian-era toolchains.
- Multiple-MIDlet suite.

Mỗi JAR có manifest metadata:

```text
name
sha256
main class
profile/configuration
required APIs
expected boot result
expected first playable state
known quirks
network requirements
```

Không patch riêng theo tên game trong VM nếu lỗi có thể giải quyết bằng semantics chuẩn. Compatibility quirk bắt buộc có rule tổng quát, comment lý do và regression test.

---

## 19. Observability

Core cần structured diagnostics có thể bật theo category:

- class loading/linking/verifier
- interpreter
- exceptions
- threads/monitors
- GC
- lifecycle
- RMS
- networking
- LCDUI/graphics
- media

Mỗi log liên quan app phải chứa:

- runtime ID
- suite ID
- app ID
- generation
- Java thread ID khi có

Không log password, session token, private message hoặc full network payload mặc định.

Cần ring buffer breadcrumbs để thu thập các sự kiện cuối trước crash mà không spam console.

---

## 20. Build và verification contract

Các lệnh chuẩn:

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

`test-full-regression.sh` là entrypoint của integration owner: ngoài host matrix và
sanitizer, script phải rebuild `Core/libphoneMECore.a` đúng archive mà Xcode
force-load, verify symbol/provenance, rồi build app Debug và Release.

Public C ABI phải compile được dưới C11 và có version additive qua
`PHONEME_C_API_VERSION`/`phoneme_c_api_version()`. Thay đổi không tương thích
phải tăng major version; thêm API tương thích chỉ tăng minor/patch.

Xcode Release validation:

```sh
xcodebuild \
  -project phoneME.xcodeproj \
  -scheme phoneME \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO \
  build
```

Verification phải thất bại nếu:

- Core source reference `Vendor/phoneME` hoặc external phoneME runtime.
- Archive có architecture ngoài arm64.
- Minimum iOS nhỏ hơn 16.0.
- Có source implementation C/Objective-C trong `Core/src`.
- Có pointer-to-narrow-integer cast.
- Archive thiếu symbol `phoneme_c_api_version`.
- App bundle chứa `classes.zip` hoặc `PhoneMERuntime`.
- Link xuất hiện legacy symbols như `KNI_`, `CVM_`, `midp_`, `pcsl_`, `JVM_`, `gxj_`, `lfp_`.

Thêm standalone-copy test vào CI:

1. Copy riêng `Core/` vào thư mục tạm.
2. Xóa output build.
3. Chạy host test.
4. Build iPhoneOS archive.
5. Verify archive.

Bước này là bằng chứng bắt buộc rằng Core không phụ thuộc repository ngoài.

---

## 21. Coding rules

- C++23, `-fno-exceptions`, `-fno-rtti` trừ khi có review thay đổi.
- RAII cho resource native.
- `std::expected`/`Result` cho lỗi recoverable.
- Không raw owning pointer.
- Không unbounded recursion khi xử lý dữ liệu JAR.
- Không unchecked narrowing conversion.
- Không global mutable VM state.
- Không merged `.cpp` hoặc unity build che giấu dependency.
- Một `.cpp` tạo một object trong archive production.
- Public headers phải tối thiểu và không leak platform framework.
- Comment giải thích invariant và compatibility decision, không lặp lại code.

Naming:

- Namespace C++ hiện tại có thể giữ `phoneme` để tránh refactor rủi ro tức thời.
- Tên namespace không được hiểu là dependency vào vendor.
- Có thể đổi tên sau bằng một migration riêng, không trộn với functional port.

---

## 22. Thứ tự triển khai bắt buộc

### Phase 0 — Baseline độc lập

Trạng thái: đã có nền tảng.

- Standalone build/test.
- Built-in boot registry.
- Không classes.zip.
- arm64/iPhoneOS 16 build.

Exit criteria:

- `Core/` copy riêng vẫn test/build/verify thành công.

### Phase 1 — JVM correctness

Ưu tiên cao nhất:

1. Exception table parsing.
2. Java exception creation và frame unwind.
3. Hoàn thiện stack/conversion/switch opcodes.
4. `checkcast`, `instanceof`, array checks.
5. Full method dispatch.
6. Class initialization correctness.
7. Verifier và StackMap support.

Exit criteria:

- Opcode suite hoàn chỉnh.
- Invalid bytecode trả Java linkage/verification error.
- Không crash native với malformed class corpus.

### Phase 2 — Threads, monitors và GC hardening

- Java scheduler.
- Monitors/wait/notify.
- Safepoints.
- Root-complete GC.
- Runtime shutdown correctness.

Exit criteria:

- Thread/monitor stress suite qua.
- Destroy không leak thread hoặc deadlock.

### Phase 3 — CLDC library

- Hoàn thiện `java.lang`, `java.io`, `java.util` cần thiết.
- Native binding inventory.
- Java exception hierarchy.

Exit criteria:

- CLDC conformance subset qua.
- Basic real MIDlet không cần compatibility patch riêng.

### Phase 4 — MIDP lifecycle và RMS

- AMS state machine.
- Suite metadata.
- RMS crash safety/isolation.

Exit criteria:

- Multi-MIDlet lifecycle stress qua.
- RMS persistence/recovery qua.

### Phase 5 — LCDUI và Canvas

- Full Displayable model.
- Commands/items/forms/lists/alerts.
- Canvas/GameCanvas graphics/input.
- Native iOS bridge protocol ổn định.

Exit criteria:

- LCDUI corpus dùng được hoàn chỉnh.
- Canvas games đạt first playable state.

### Phase 6 — Networking

- Socket/datagram/HTTP/HTTPS/DNS.
- Background connection policy.
- Async cancellation safety.

Exit criteria:

- Online corpus login/play/reconnect ổn định.
- Không socket leak hoặc cross-app callback.

### Phase 7 — Media

- MMAPI subset theo corpus.
- Audio session/background behavior.

Exit criteria:

- Media corpus phát đúng và không leak player.

### Phase 8 — Compatibility hardening

- Toolchain/class compatibility.
- Device quirks tổng quát.
- Performance tuning.
- Long-run stress.

Exit criteria:

- Corpus pass rate đạt ngưỡng release đã đặt.
- Không còn P0/P1 defect.

### Phase 9 — Release qualification

- Device matrix.
- Battery/CPU/RAM benchmarks.
- Crash-free soak.
- Packaging/license/security audit.

Exit criteria:

- Đạt toàn bộ Definition of Done.

---

## 23. Priority ngay sau handoff

Task tiếp theo nên bắt đầu từ exception semantics, không chuyển sang thêm API bề mặt quá sớm.

Thứ tự cụ thể:

1. Mở rộng `CodeAttribute` để lưu exception table.
2. Parse và validate từng handler range/target/catch type.
3. Thêm throwable state vào interpreter execution result.
4. Thực hiện unwind qua frame stack.
5. Resolve catch type và kiểm tra assignability.
6. Chuyển null access, divide-by-zero, bounds và negative-size thành Java exception.
7. Thêm regression tests cho nested calls, finally patterns và uncaught boundary.
8. Sau khi exception ổn định mới hoàn thiện verifier và threads.

Lý do: networking, LCDUI, RMS và media đều cần exception semantics đúng để không biến lỗi recoverable thành crash native hoặc runtime treo.

---

## 24. Các shortcut bị cấm

- Mang lại `classes.zip` để “tạm chạy được”.
- Link lại legacy phoneME binary.
- Copy toàn bộ vendor source vào `Core/` rồi đổi tên.
- Tắt verifier toàn cục.
- Catch mọi lỗi rồi tiếp tục execute bytecode hỏng.
- Hardcode theo SHA/tên game trong interpreter.
- Một global VM cho mọi MIDlet mà không isolation.
- Polling loop liên tục để giả lập scheduler/background.
- Giữ UIKit object trong Java heap.
- Dùng raw pointer làm Java object reference.
- Bỏ qua error và trả thành công giả.
- Claim full compatibility khi chưa có corpus/test evidence.

---

## 25. Checklist cho mỗi pull request

- [ ] Chỉ sửa module cần thiết, không kéo vendor tree.
- [ ] Có unit/regression test cho hành vi mới.
- [ ] Có negative test nếu xử lý dữ liệu không tin cậy.
- [ ] Không thêm global mutable state.
- [ ] Không thêm pointer narrowing hoặc UB.
- [ ] Host tests qua.
- [ ] iPhoneOS Core build qua.
- [ ] Core verification qua.
- [ ] Xcode Release build qua nếu thay public ABI/integration.
- [ ] Không xuất hiện `classes.zip`/PhoneMERuntime trong app bundle.
- [ ] Cập nhật status/gap trong tài liệu nếu scope thay đổi.
- [ ] Không mô tả module là hoàn chỉnh nếu acceptance criteria chưa đạt.

---

## 26. Handoff summary

Kiến trúc đã chuyển sang Core C++ độc lập và có bằng chứng standalone build. Công việc còn lại là một full runtime port thực sự, không phải cleanup nhỏ.

Nguyên tắc dẫn đường:

1. Correctness của VM trước breadth của API.
2. Isolation và lifecycle trước tối ưu đa nhiệm.
3. Platform adapters rõ ràng, không để UIKit/Swift xâm nhập VM.
4. Không fallback vendor dưới bất kỳ hình thức nào.
5. Mọi compatibility behavior phải có test.
6. Chỉ tuyên bố ổn định khi có stress/device/corpus evidence.

File này là source of truth cho phạm vi full port. Khi thay đổi kiến trúc hoặc tiêu chí nghiệm thu, cập nhật file này trong cùng pull request.
