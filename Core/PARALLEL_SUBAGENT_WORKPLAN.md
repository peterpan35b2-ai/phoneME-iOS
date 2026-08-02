# Kế hoạch chia việc cho nhiều subagent — phoneME C++ Core

Ngày cập nhật: 2026-08-02  
Phạm vi: `/Users/duypham/Developer/phoneME-iOS`  
Tài liệu nền: `Core/FULL_PORT_HANDOFF.md`  
Mục đích: chia phần còn lại của full J2ME port thành các mục độc lập để có thể giao cho nhiều subagent làm song song.

---

## 0. Cách sử dụng tài liệu này

Có thể giao nguyên văn cho một agent theo mẫu:

```text
Implement mục 09 trong Core/PARALLEL_SUBAGENT_WORKPLAN.md.
Trước khi sửa code, đọc block TRẠNG THÁI của mục 09.
Chỉ tiếp tục nếu trạng thái là TODO và Agent là UNASSIGNED.
Claim mục bằng cách điền Agent, Worktree/Branch, thời gian rồi đổi sang CLAIMED;
commit riêng thay đổi claim trước khi sửa source.
Sau đó đổi sang IN_PROGRESS và chỉ làm đúng phạm vi/file ownership của mục.
Không sửa các file trung tâm đã được dành riêng cho mục 20.
Chạy đầy đủ test được yêu cầu và báo cáo file đã đổi, test đã chạy, giới hạn còn lại.
Khi xong đổi sang READY_FOR_INTEGRATION, không tự đánh dấu DONE.
```

Mỗi agent chỉ sở hữu **một mục đánh số** trừ khi được chỉ định rõ thêm. Không tự mở rộng sang mục khác chỉ vì thấy API liên quan.

### 0.1 Cơ chế claim và trạng thái chống conflict

Block `TRẠNG THÁI` ngay dưới tiêu đề từng mục là **nguồn sự thật duy nhất** về ownership đang hoạt động của mục đó.

Các trạng thái hợp lệ:

| Trạng thái | Ý nghĩa | Ai được chuyển |
|---|---|---|
| `TODO` | Chưa có agent nhận | User hoặc integration owner |
| `CLAIMED` | Agent đã giữ chỗ, chưa sửa implementation | Agent nhận mục |
| `IN_PROGRESS` | Đang sửa code/test trong ownership | Agent đang giữ mục |
| `BLOCKED` | Đang dừng vì dependency/lỗi ngoài phạm vi | Agent đang giữ mục |
| `READY_FOR_INTEGRATION` | Implementation và standalone test đã xong, chờ mục 20 | Agent đang giữ mục |
| `INTEGRATING` | Mục 20 đang merge/resolve registry/build | Integration owner |
| `DONE` | Đã merge và full regression pass | Chỉ integration owner |
| `CANCELLED` | Mục bị hủy hoặc trả lại | User hoặc integration owner |

Quy trình bắt buộc:

1. Agent phải đọc bản mới nhất của file này trước khi làm.
2. Chỉ được claim khi `Trạng thái: TODO` và `Agent: UNASSIGNED`.
3. Khi claim, điền đầy đủ `Agent`, `Worktree/Branch`, `Bắt đầu`, `Cập nhật cuối`; đổi trạng thái sang `CLAIMED`.
4. Commit riêng thay đổi claim trước khi sửa source. Commit claim không được kèm code.
5. Sau khi bắt đầu sửa implementation, đổi sang `IN_PROGRESS`.
6. Cập nhật `Cập nhật cuối` sau mỗi checkpoint đáng kể hoặc trước khi dừng phiên làm việc.
7. Nếu bị chặn, đổi sang `BLOCKED` và ghi rõ `Blocker`; không tự mở rộng sang ownership của mục khác để gỡ chặn.
8. Khi standalone test đã pass và handoff đầy đủ, đổi sang `READY_FOR_INTEGRATION`, điền commit/branch vào `Handoff`.
9. Chỉ mục 20 được đổi `READY_FOR_INTEGRATION` → `INTEGRATING` → `DONE`.
10. Thấy mục đang `CLAIMED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_INTEGRATION` hoặc `INTEGRATING` thì agent khác **không được sửa bất kỳ file ownership nào của mục đó**.
11. Không agent nào được tự cướp claim vì thấy `Cập nhật cuối` cũ. Chỉ user hoặc integration owner được reset về `TODO`/`UNASSIGNED`.
12. Agent chỉ được sửa block trạng thái của mục mình sở hữu; không cập nhật hộ mục khác.
13. Khi trả task mà chưa hoàn thành, phải ghi lý do rồi để user/integration owner chuyển sang `CANCELLED` hoặc reset về `TODO`.
14. Nếu một agent được giao nhiều mục, phải claim riêng từng mục; không dùng claim của mục này để sửa ownership mục khác.

Mẫu block trạng thái:

```text
**TRẠNG THÁI**
- Trạng thái: TODO
- Agent: UNASSIGNED
- Worktree/Branch: -
- Bắt đầu: -
- Cập nhật cuối: -
- Blocker: -
- Handoff: -
```

### 0.2 Quy tắc bắt buộc cho tất cả agent

1. Core production chỉ dùng C++23, target `iphoneos`, `arm64`, iOS 16.0 trở lên.
2. Không copy hoặc link lại `Vendor/phoneME`, KNI, PCSL, CVM, `classes.zip` hoặc legacy phoneME runtime.
3. Không JIT, không executable memory, không JNI runtime.
4. Không lưu native pointer trong Java slot, heap object, serialized state hoặc giá trị `int32_t`.
5. Không dùng Objective-C/Swift trong `Core/src/vm`, `Core/src/classfile` hoặc logic VM thuần.
6. Không làm fallback giả để test pass. API chưa hỗ trợ phải trả Java exception hoặc lỗi Core đúng loại.
7. Không sửa format toàn repository và không chạy formatter toàn bộ project.
8. Không xóa hoặc ghi đè thay đổi đang tồn tại của agent khác.
9. Mỗi thay đổi phải có regression test; không chỉ thêm declaration hoặc skeleton.
10. Mỗi agent phải kết thúc với báo cáo:
   - File đã tạo/sửa.
   - Semantics đã triển khai.
   - Test đã chạy và kết quả.
   - Giới hạn chưa giải quyết.
   - File trung tâm cần mục 20 tích hợp.

### 0.3 Các file trung tâm không được sửa khi làm mục 01–19

Các file sau được dành cho **mục 20 — Integration owner** để tránh merge conflict:

```text
Core/CMakeLists.txt
Core/src/vm/BuiltinClasses.cpp
Core/Tests/CoreTests.cpp
Core/include/PhoneMECore.h
Core/src/api/CAPI.cpp
phoneME.xcodeproj/project.pbxproj
phoneME/Support/phoneME-Bridging-Header.h
```

Ngoại lệ:

- Mục 16 được phép chuẩn bị patch C ABI/Swift trong file riêng, nhưng thay đổi cuối vào các file trên vẫn do mục 20 thực hiện.
- Mọi agent được phép sửa **duy nhất block `TRẠNG THÁI` của mục mình đang claim** trong `Core/PARALLEL_SUBAGENT_WORKPLAN.md`. Không được sửa mô tả, ownership, dependency hoặc block trạng thái của mục khác.

Nếu module mới cần đăng ký built-in class hoặc native method:

- Tạo file module riêng với hàm `register_<module>_classes(...)` hoặc `register_<module>_natives(...)`.
- Ghi rõ dòng cần thêm vào file trung tâm trong báo cáo handoff.
- Không tự sửa `BuiltinClasses.cpp`.

Nếu module mới cần thêm source vào build:

- Tạo source/header/test đầy đủ.
- Ghi danh sách source cần thêm vào `Core/CMakeLists.txt` trong handoff.
- Có thể tạo script test standalone compile trực tiếp module đó.

### 0.4 Quy tắc test song song

Các script hiện tại có thể `rm -rf` thư mục build mặc định. Không chạy nhiều agent chung một test root.

Dùng test root riêng:

```sh
PHONEME_TEST_ROOT="$PWD/Core/build/agent-XX-host" \
  bash Core/Tools/test-host.sh

PHONEME_GRAPHICS_TEST_ROOT="$PWD/Core/build/agent-XX-graphics" \
  bash Core/Tools/test-graphics-host.sh

PHONEME_GRAPHICS_VM_TEST_ROOT="$PWD/Core/build/agent-XX-graphics-vm" \
  bash Core/Tools/test-graphics-vm-host.sh
```

Ưu tiên tạo test độc lập:

```text
Core/Tests/<ModuleName>Tests.cpp
Core/Tests/fixtures/<ModuleName>Ops.java
Core/Tools/test-<module>-host.sh
```

Không thêm trực tiếp vào `Core/Tests/CoreTests.cpp`; mục 20 sẽ ghép test cuối.

### 0.5 Mức độ song song

Ký hiệu:

- **ĐỘC QUYỀN**: chỉ một agent được sửa nhóm file đó.
- **SONG SONG**: có thể chạy cùng các mục khác nếu tuân thủ ownership.
- **PHỤ THUỘC**: nên chờ API của mục được nêu ổn định trước khi merge cuối.

### 0.6 Chuỗi phụ thuộc chính

```text
01 Java Scheduler/Threads
 ├──> 02 Runtime lifecycle và execution model
 ├──> 03 GC/OOM/native roots
 ├──> 09 Network async integration
 └──> 12 Canvas event scheduling

05 Installer/JAD
 └──> 06 Security manifest/trust
       ├──> 08 Filesystem permission gate
       ├──> 09 Network permission gate
       └──> 15 Media permission gate

11 Graphics accuracy
 └──> 14 MIDP Game API

09 Network + 10 Push Core
 └──> 16 iOS host integration

01–16
 └──> 17 Real-game compatibility corpus
     └──> 19 Performance, soak và device validation

Tất cả mục
 └──> 20 Integration owner
```

---

# 01. Java scheduler, Thread và monitor blocking

**TRẠNG THÁI**
- Trạng thái: `CLAIMED`
- Agent: `GPT-5.6 Thinking`
- Worktree/Branch: `/Users/duypham/Developer/phoneME-iOS @ main`
- Bắt đầu: `2026-08-02 18:50 +07:00`
- Cập nhật cuối: `2026-08-02 18:50 +07:00`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN, P0  
**Không giao cùng file cho agent khác.**

## Mục tiêu

Biến VM từ mô hình một Java invocation đồng bộ thành VM có execution context tạm dừng/tiếp tục và scheduler thật, đủ để game loop Java hoạt động.

## Baseline hiện tại

- `java/lang/Thread` mới có constructor rỗng và `yield()` no-op.
- Monitor hỗ trợ reentrant trên main Java thread giả lập.
- Khi monitor contention, VM trả `unsupported_feature`.
- Chưa có `Object.wait/notify/notifyAll`.
- Chưa có sleep, join, interruption hoặc thread cleanup.

## File ownership

Agent này được sở hữu độc quyền:

```text
Core/include/phoneme/vm/Machine.hpp
Core/include/phoneme/vm/Interpreter.hpp
Core/include/phoneme/vm/MonitorTable.hpp
Core/src/vm/Machine.cpp
Core/src/vm/Interpreter.cpp
Core/src/vm/MonitorTable.cpp
Core/src/vm/LangBuiltinClasses.cpp          # chỉ phần Thread/Object methods
Core/src/vm/CoreNatives.cpp                 # chỉ phần Thread/Object/monitor natives
```

File mới đề xuất:

```text
Core/include/phoneme/vm/JavaThread.hpp
Core/include/phoneme/vm/Scheduler.hpp
Core/include/phoneme/vm/ExecutionContext.hpp
Core/src/vm/JavaThread.cpp
Core/src/vm/Scheduler.cpp
Core/src/vm/ExecutionContext.cpp
Core/Tests/SchedulerTests.cpp
Core/Tests/fixtures/ThreadOps.java
Core/Tools/test-scheduler-host.sh
```

## Phạm vi phải triển khai

1. Java thread identity riêng, không dùng hằng `kMainJavaThreadId` cho mọi execution.
2. Execution frame stack có thể suspend/resume mà không mất locals, operand stack, PC, held monitors và pending exception.
3. Scheduler có runnable queue, blocked queue, sleeping queue và terminated state.
4. `Thread` tối thiểu:
   - `Thread()`.
   - `Thread(Runnable)`.
   - `start()`.
   - `run()`.
   - `currentThread()`.
   - `yield()`.
   - `sleep(long)`.
   - `join()` và `join(long)`.
   - `isAlive()`.
   - `interrupt()` và trạng thái interrupted tối thiểu đủ cho sleep/wait/join.
   - priority API có validation; scheduler có thể dùng cùng quantum ban đầu nhưng không được trả sai state.
5. `Object.wait()`, `wait(long)`, `notify()`, `notifyAll()`.
6. Contended `monitorenter` và synchronized method phải block thread, không fail VM.
7. Monitor phải được release đúng khi exception unwind hoặc thread terminate.
8. Scheduler safepoint cho GC và native completion.
9. Instruction budget chuyển thành quantum/yield point hợp lý.
10. Uncaught throwable của một Java thread không được làm corrupt thread khác hoặc VM khác.

## Không thuộc phạm vi

- iOS background policy.
- Socket implementation cụ thể.
- UI event queue.
- Media playback.
- Full priority scheduling tinh vi.

## Test bắt buộc

- Hai thread tăng counter trong synchronized block.
- Contended monitor handoff.
- Reentrant synchronized method.
- wait/notify và wait timeout.
- notifyAll với nhiều waiter.
- sleep không chặn toàn host runtime.
- join và join timeout.
- interruption của sleeping/waiting thread.
- exception trong thread và monitor cleanup.
- GC trong lúc có nhiều suspended thread.
- stack roots của mọi thread còn sống sau GC.
- deterministic scheduler mode cho host test.

## Điều kiện hoàn thành

- Không còn message `monitor contention requires Java scheduler` trên đường chạy hợp lệ.
- `Thread.start()` chạy `Runnable.run()` trên execution context riêng.
- Host test chứng minh một thread sleep/block nhưng thread khác và runtime vẫn tiến triển.
- ASan/UBSan pass.
- Không busy-spin khi không có runnable thread.

## Handoff cần gửi mục 20

- Source/header mới cần thêm vào build.
- Built-in methods mới cần registry trung tâm nhận diện.
- API scheduler mà Runtime/Network/Canvas phải gọi.

---

# 02. Runtime lifecycle, lock boundary và multi-MIDlet execution

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN, P0  
**Phụ thuộc:** API scheduler của mục 01.

## Mục tiêu

Loại bỏ việc giữ `Runtime::mutex_` trong khi thực thi Java callback hoặc blocking native; hoàn thiện lifecycle nhiều MIDlet và failure isolation.

## Baseline hiện tại

- `startApp`, `pauseApp`, `destroyApp`, Canvas callback và LCDUI callback có thể chạy khi Runtime mutex đang giữ.
- `Runtime::stop()` xóa VM mà không gọi `destroyApp(true)`.
- `Runtime::suspend()` chỉ hide Canvas và suspend media, không có policy lifecycle chi tiết.
- Input hiện vẫn có thể gửi vào app state `paused`.

## File ownership

```text
Core/include/phoneme/runtime/Runtime.hpp
Core/src/runtime/Runtime.cpp
Core/include/phoneme/runtime/ConcurrentQueue.hpp
```

File mới đề xuất:

```text
Core/include/phoneme/runtime/AppRuntime.hpp
Core/include/phoneme/runtime/LifecyclePolicy.hpp
Core/src/runtime/AppRuntime.cpp
Core/src/runtime/LifecyclePolicy.cpp
Core/Tests/RuntimeConcurrencyTests.cpp
Core/Tests/fixtures/LifecycleConcurrencyOps.java
Core/Tools/test-runtime-concurrency-host.sh
```

## Phạm vi phải triển khai

1. Không giữ global Runtime mutex khi gọi Java bytecode, native callback có thể block hoặc permission prompt.
2. Dùng stable app handle/generation để validate sau khi callback quay lại.
3. Per-app execution serialization; không cho hai host thread đồng thời mutate cùng Machine ngoài scheduler contract.
4. Lifecycle state machine rõ:
   - installed.
   - starting.
   - active.
   - paused.
   - background-running nếu policy cho phép.
   - destroying.
   - destroyed.
   - error.
5. `stop()` phải thực hiện destroy best-effort cho từng MIDlet và đóng resource theo thứ tự.
6. Phân biệt:
   - chuyển foreground giữa MIDlet.
   - user pause.
   - app host vào background nhưng được giữ chạy.
   - iOS suspension thật.
   - explicit exit từ MIDlet.
7. Input chỉ đi tới foreground app đang nhận input hợp lệ.
8. UI event, framebuffer và input queue phải giữ isolation theo app ID/generation.
9. Một MIDlet treo hoặc throw không làm khóa MIDlet khác.
10. Resource cleanup phải đóng network/media/file handles trước khi giải phóng Machine.

## Test bắt buộc

- Start hai MIDlet đồng thời từ hai host thread.
- Một MIDlet callback lâu không chặn query trạng thái MIDlet khác.
- Switch foreground liên tục và không gửi nhầm input/frame.
- `notifyDestroyed()` trong `startApp`, callback UI và worker thread.
- `notifyPaused()` và `resumeRequest()` đúng state.
- `stop()` gọi destroy callback đúng một lần.
- Failure trong `pauseApp`/`destroyApp` được isolate.
- Destroy khi đang có pending Canvas/network/media callback.
- 1000 vòng start/switch/pause/resume/destroy không deadlock.

## Điều kiện hoàn thành

- Không có Java invocation dưới global Runtime mutex.
- State transition được kiểm tra bằng bảng hoặc hàm tập trung, không rải rác condition khó kiểm soát.
- App A không thể emit event vào generation mới của app B dùng lại cùng AppId.
- Test ThreadSanitizer nếu toolchain hỗ trợ; tối thiểu ASan/UBSan và stress pass.

---

# 03. Heap, GC safepoint, OOM và native handle roots

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN, P0/P1  
**Phụ thuộc:** execution context của mục 01.

## Mục tiêu

Làm GC an toàn khi có nhiều Java thread, native calls, platform handle và allocation failure thực tế.

## File ownership

```text
Core/include/phoneme/vm/Heap.hpp
Core/src/vm/Heap.cpp
Core/include/phoneme/vm/ClassLayout.hpp
Core/src/vm/ClassLayout.cpp
```

Được phối hợp có kiểm soát với mục 01 trong phần root publication của execution context.

File mới đề xuất:

```text
Core/include/phoneme/vm/RootSet.hpp
Core/include/phoneme/vm/NativeRootScope.hpp
Core/src/vm/RootSet.cpp
Core/src/vm/NativeRootScope.cpp
Core/Tests/GcStressTests.cpp
Core/Tests/fixtures/GcThreadOps.java
Core/Tools/test-gc-stress-host.sh
```

## Phạm vi phải triển khai

1. Publish roots của tất cả runnable/blocked/sleeping Java threads.
2. RAII native root scopes cho ObjectRef tạm trong native method có thể allocate.
3. Root ownership cho:
   - GraphicsStore.
   - Canvas runtime.
   - RMS listener/enumeration state.
   - LCDUI component registry.
   - media listener/player Java objects.
   - pending network callbacks nếu có Java references.
4. Emergency reserve để tạo `OutOfMemoryError` khi heap đã cạn.
5. Heap budget theo bytes ước tính đáng tin hơn chỉ đếm số object.
6. Overflow-safe allocation cho array/string/object.
7. GC safepoint trước/sau native có thể allocate hoặc block.
8. Không collect object đang được platform callback tham chiếu.
9. Clear/prune native maps khi ObjectRef generation không còn live.
10. Diagnostic heap stats per MIDlet.

## Test bắt buộc

- Allocation pressure từ nhiều Java thread.
- OOM vẫn ném được Java throwable.
- ObjectRef stale generation bị từ chối.
- Native callback giữ object qua một GC cycle.
- Graphics/LCDUI/RMS listener không bị collect sớm.
- Sau destroy MIDlet, native resources được prune.
- Large array overflow không wrap.
- Repeated OOM không crash native.

## Điều kiện hoàn thành

- Không có raw ObjectRef tạm sống qua allocation mà không nằm trong root scope.
- GC có thể chạy khi thread khác đang blocked mà không corrupt frame.
- Heap stats phản ánh gần đúng RAM của object payload và native side tables.
- Sanitizer pass trên stress corpus.

---

# 04. Hoàn thiện CLDC `java.lang`, `java.io`, `java.util`

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Không sửa phần Thread/Object wait/notify của mục 01.**

## Mục tiêu

Bổ sung class/method CLDC thường dùng còn thiếu và chuẩn hóa semantics hiện có theo CLDC 1.0/1.1.

## File ownership

```text
Core/src/vm/LangBuiltinClasses.cpp      # trừ Thread và Object wait/notify
Core/src/vm/IOBuiltinClasses.cpp
Core/src/vm/UtilBuiltinClasses.cpp
Core/src/vm/IONatives.cpp
Core/src/vm/UtilNatives.cpp
Core/src/vm/ConsoleNatives.cpp
Core/src/vm/StringEncodingNatives.cpp
Core/src/vm/WrapperNatives.cpp
Core/src/vm/TimeNatives.cpp
```

File mới tùy nhu cầu:

```text
Core/src/vm/ReaderWriterBuiltinClasses.cpp
Core/src/vm/ReaderWriterNatives.cpp
Core/src/vm/TimerBuiltinClasses.cpp
Core/src/vm/TimerNatives.cpp
Core/Tests/CldcLibraryTests.cpp
Core/Tests/fixtures/CldcLibraryOps.java
Core/Tools/test-cldc-library-host.sh
```

## Phạm vi phải triển khai

1. Audit API signatures với CLDC 1.0 và 1.1.
2. Bổ sung tối thiểu:
   - `java.io.Reader`.
   - `java.io.Writer`.
   - `InputStreamReader`.
   - `OutputStreamWriter`.
   - `IOException` constructor/message semantics.
   - `EOFException`, `UTFDataFormatException` nếu thiếu.
   - `java.util.Timer`.
   - `java.util.TimerTask`.
3. Hoàn thiện Throwable message/cause/toString bề mặt cần cho game, không cần full Java SE stack trace nếu CLDC không yêu cầu.
4. Chuẩn hóa String/StringBuffer/StringBuilder edge cases.
5. `PrintStream` phải hỗ trợ custom OutputStream sau khi scheduler callback contract có sẵn.
6. Date/Calendar/TimeZone fields thông dụng và timezone offset đúng.
7. Vector/Hashtable/Stack enumeration và synchronization semantics.
8. DataInput/DataOutput modified UTF edge cases.
9. Encoding aliases thường thấy trong J2ME: UTF-8, UTF-16BE, ISO-8859-1, US-ASCII nếu platform contract cho phép.
10. Unsupported Java SE API phải fail có chủ đích, không silently trả sai.

## Test bắt buộc

- Differential fixture chạy trên Java reference hoặc phoneME reference khi có thể.
- Reader/writer với tiếng Việt và surrogate pairs.
- DataInput/DataOutput UTF boundary và malformed input.
- Timer one-shot, fixed delay, cancel và purge cơ bản.
- Hashtable null rejection và rehash.
- Vector enumeration khi mutate theo CLDC behavior được chọn.
- Throwable message/toString.
- Calendar fields quanh midnight/timezone/DST.

## Điều kiện hoàn thành

- Danh sách API coverage được cập nhật bởi handoff cho mục 20.
- Không dùng Java SE behavior tùy tiện nếu khác CLDC.
- Toàn bộ test standalone pass với sanitizer.

---

# 05. JAD, installer và persistent Suite Store

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1

## Mục tiêu

Biến `SuiteStore` in-memory thành installer MIDP có JAD, upgrade/uninstall, managed storage và suite identity ổn định.

## File ownership

```text
Core/include/phoneme/runtime/SuiteStore.hpp
Core/src/runtime/SuiteStore.cpp
Core/include/phoneme/archive/ZipArchive.hpp
Core/src/archive/ZipArchive.cpp              # chỉ khi cần validation installer
```

File mới đề xuất:

```text
Core/include/phoneme/runtime/JadParser.hpp
Core/include/phoneme/runtime/SuiteInstaller.hpp
Core/include/phoneme/runtime/SuiteDatabase.hpp
Core/src/runtime/JadParser.cpp
Core/src/runtime/SuiteInstaller.cpp
Core/src/runtime/SuiteDatabase.cpp
Core/Tests/SuiteInstallerTests.cpp
Core/Tests/fixtures/*.jad
Core/Tools/test-suite-installer-host.sh
```

## Phạm vi phải triển khai

1. Parse JAD với continuation, duplicate property policy, UTF handling và size limits.
2. Merge JAD/manifest theo MIDP rules được ghi rõ.
3. Validate:
   - `MIDlet-Name`.
   - `MIDlet-Vendor`.
   - `MIDlet-Version`.
   - `MIDlet-Jar-URL`.
   - `MIDlet-Jar-Size`.
   - `MicroEdition-Profile`.
   - `MicroEdition-Configuration`.
   - `MIDlet-n` entries.
4. Copy JAR/JAD vào managed suite directory; không phụ thuộc đường dẫn import ban đầu.
5. Persistent suite database atomic và corruption-detecting.
6. Stable suite ID dựa trên suite identity, không chỉ CRC tạm trong một Runtime.
7. Install transaction, rollback khi crash/error.
8. Upgrade/downgrade/version comparison.
9. Uninstall transaction, có policy giữ/xóa RMS/files/permissions.
10. Expose manifest/JAD properties cho MIDlet.
11. Thu thập declared permissions cho mục 06.
12. Giới hạn kích thước archive, số entry, path traversal và zip bomb protection.

## Test bắt buộc

- Install từ JAR-only.
- Install JAD + JAR.
- JAD size mismatch.
- Missing MIDlet class.
- Duplicate/continued manifest fields.
- Upgrade giữ suite ID và RMS.
- Downgrade rejection.
- Crash simulation giữa copy/db commit.
- Corrupt database detection/recovery.
- Uninstall và reinstall.
- JAR path traversal/zip bomb rejection.

## Điều kiện hoàn thành

- Runtime restart vẫn list/load suite đã install.
- Xóa file import gốc không làm suite đã install mất khả năng chạy.
- Upgrade atomic và không mất RMS ngoài policy.
- Không sử dụng CRC32 đơn thuần làm security identity.

---

# 06. Security domain, manifest permissions và resource gating

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Phụ thuộc:** declared permission output từ mục 05.

## Mục tiêu

Kết nối `PermissionPolicy` vào tài nguyên thật và hoàn thiện trust/permission behavior theo suite.

## File ownership

```text
Core/include/phoneme/security/PermissionPolicy.hpp
Core/src/security/PermissionPolicy.cpp
Core/src/vm/SecurityBuiltinClasses.cpp
Core/src/vm/SecurityNatives.cpp
Core/src/vm/SecurityNatives.hpp
```

File mới đề xuất:

```text
Core/include/phoneme/security/PermissionCatalog.hpp
Core/include/phoneme/security/SuiteSecurityDomain.hpp
Core/src/security/PermissionCatalog.cpp
Core/src/security/SuiteSecurityDomain.cpp
Core/Tests/SecurityIntegrationTests.cpp
Core/Tests/fixtures/SecurityResourceOps.java
Core/Tools/test-security-integration-host.sh
```

## Phạm vi phải triển khai

1. Nhận `MIDlet-Permissions` và `MIDlet-Permissions-Opt` từ installer.
2. Canonical permission catalog cho network, file, media, push và platform request.
3. Phân biệt mandatory/optional declared permission.
4. Trust domain và default policy rõ ràng; không hardcode mọi suite trusted.
5. Prompt callback phải chạy ngoài Runtime global lock.
6. Session/one-shot/blanket persistence như hiện tại nhưng có migration/version/checksum.
7. API `require()` ổn định để subsystem owner gọi trước resource access.
8. Resource string không được chứa secret không cần thiết trong log.
9. Prompt coalescing nếu nhiều thread đồng thời hỏi cùng permission.
10. Revocation làm các operation mới bị từ chối; policy cho operation đang chạy phải rõ.
11. `MIDlet.checkPermission()` trả đúng allowed/denied/unknown.
12. Audit mọi đường vào resource, kể cả constructor/redirect/reconnect.

## Phân chia integration với agent khác

- Mục 08 tự gọi security gate trong FileNatives/FileSystem.
- Mục 09 tự gọi security gate trong Connector/Network.
- Mục 10 tự gọi security gate cho PushRegistry.
- Mục 15 tự gọi security gate trong Media.
- Mục 16 nối prompt UI Swift/C ABI.

## Test bắt buộc

- Undeclared mandatory permission bị reject install hoặc start theo policy.
- Optional permission denied nhưng MIDlet vẫn chạy.
- One-shot prompt mỗi lần.
- Session prompt một lần mỗi session.
- Blanket persist qua restart.
- Prompt đồng thời được coalesce.
- Reentrant host prompt không deadlock Runtime.
- Per-suite isolation.
- Permission file corruption.

## Điều kiện hoàn thành

- Security policy không còn chỉ là API kiểm tra thụ động.
- Mỗi subsystem có test chứng minh resource thật bị chặn khi denied.
- Không có default bypass ngoài trust policy được khai báo rõ.

---

# 07. RMS semantics, crash recovery và stress verification

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1

## Mục tiêu

Hoàn thiện và xác nhận RMS hiện có, đặc biệt các semantics đã viết nhưng chưa được gọi trong test chính.

## File ownership

```text
Core/include/phoneme/runtime/RecordStoreRegistry.hpp
Core/src/runtime/RecordStoreRegistry.cpp
Core/src/vm/RmsBuiltinClasses.cpp
Core/src/vm/RmsNatives.cpp
Core/src/vm/RmsNatives.hpp
Core/Tests/fixtures/RmsOps.java
```

File mới đề xuất:

```text
Core/Tests/RmsAdvancedTests.cpp
Core/Tests/RmsCrashHarness.cpp
Core/Tools/test-rms-host.sh
```

## Phạm vi phải triển khai

1. Thực sự chạy `RmsOps.advancedSemantics()` trong test standalone.
2. Xác nhận listener trên nhiều handles cùng store.
3. Live enumeration và `keepUpdated` cursor semantics khi add/set/delete.
4. Comparator constants `PRECEDES`, `EQUIVALENT`, `FOLLOWS` đúng chiều.
5. Filter/comparator exception propagation.
6. Concurrent open handles trên nhiều Java thread.
7. Delete store while any handle open.
8. Quota ở mức toàn suite với nhiều stores.
9. Rollback in-memory state nếu persist fail.
10. Atomic add/set/delete khi process bị kill ở từng checkpoint.
11. `.tmp`/`.bak` recovery selection không chọn generation corrupt.
12. Record ID không tái sử dụng sau delete/restart.
13. Migration v1 → v2 và future-version rejection.
14. Suite isolation và hash collision handling.
15. Large store performance và bounded memory.

## Test bắt buộc

- Gọi toàn bộ advanced fixture và assert bitmask đầy đủ.
- Fault injection cho write/fsync/rename/directory fsync.
- Kill child process giữa transaction và reopen.
- Corrupt canonical, tmp và backup theo mọi tổ hợp.
- Multi-thread add/set/delete stress.
- Quota boundary chính xác.
- Listener tự remove/add trong callback.
- Enumeration destroy/store close behavior.

## Điều kiện hoàn thành

- Advanced semantics không còn là fixture không được gọi.
- Persist failure không để registry memory khác disk mà không báo lỗi/rollback.
- Recovery test chứng minh không mất generation đã fsync thành công.
- Sanitizer pass.

---

# 08. Filesystem sandbox, resource loading và JSR-75 FileConnection

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Phụ thuộc mềm:** security gate API mục 06.

## Mục tiêu

Làm filesystem chống symlink/path escape, hoàn thiện FileConnection và bảo đảm I/O không khóa sai Runtime.

## File ownership

```text
Core/include/phoneme/filesystem/FileSystem.hpp
Core/include/phoneme/filesystem/ResourceLoader.hpp
Core/src/filesystem/FileSystem.cpp
Core/src/filesystem/ResourceLoader.cpp
Core/src/vm/FileBuiltinClasses.cpp
Core/src/vm/FileNatives.cpp
Core/src/vm/FileNatives.hpp
Core/Tests/fixtures/FileOps.java
```

File mới đề xuất:

```text
Core/include/phoneme/filesystem/SandboxResolver.hpp
Core/src/filesystem/SandboxResolver.cpp
Core/Tests/FileSystemSecurityTests.cpp
Core/Tools/test-filesystem-host.sh
```

## Phạm vi phải triển khai

1. Root-relative resolution dùng directory FD/openat khi khả dụng.
2. Chặn symlink escape và TOCTOU giữa validation/open.
3. Chặn absolute path, `..`, encoded traversal và NUL.
4. Per-suite persistent root và per-app temporary root.
5. Resource lookup đúng cho:
   - `Class.getResourceAsStream("name")`.
   - absolute resource `/name`.
   - package-relative resource.
6. Full FileConnection surface ưu tiên:
   - exists.
   - isDirectory.
   - fileSize/directorySize.
   - canRead/canWrite/setReadable/setWritable theo capability khả dụng.
   - create/mkdir/delete/rename/truncate.
   - list với filter và includeHidden.
   - openInputStream/openOutputStream(offset).
   - availableSize/totalSize/usedSize.
7. Handle ownership theo Machine/app.
8. Close idempotency và stream-close propagation.
9. Background-safe I/O contract; blocking file operation không giữ Runtime global lock.
10. Security gate read/write trước khi mở hoặc mutate.
11. Atomic helper cho save file quan trọng.

## Test bắt buộc

- Symlink bên trong sandbox trỏ ra ngoài phải bị chặn.
- Rename race/path replacement stress.
- Resource package-relative và Unicode names.
- Multi-handle read/write/truncate.
- Delete/rename khi stream mở theo policy đã định nghĩa.
- Permission denied read/write.
- Large file và offset overflow.
- Temporary cleanup khi app destroy.

## Điều kiện hoàn thành

- Không thể đọc/ghi ngoài suite root qua path hoặc symlink.
- FileConnection errors map đúng `IOException`, `SecurityException`, `IllegalArgumentException`.
- Test stress không leak FD.

---

# 09. Network/GCF asynchronous completion, socket/TLS và cancellation

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG nhưng ownership lớn, P0/P1  
**Phụ thuộc:** scheduler mục 01 và security API mục 06.

## Mục tiêu

Biến networking từ API có tên async nhưng thực thi/block đồng bộ thành operation có thể suspend riêng Java thread, cancel và tiếp tục mà không khóa toàn runtime.

## File ownership

```text
Core/include/phoneme/network/AsyncNetworkAdapter.hpp
Core/include/phoneme/network/ConnectionRegistry.hpp
Core/include/phoneme/network/Url.hpp
Core/src/network/ConnectionRegistry.cpp
Core/src/network/PosixNetworkAdapter.cpp
Core/src/network/Url.cpp
Core/src/vm/ConnectionBuiltinClasses.cpp
Core/src/vm/ConnectionNatives.cpp
Core/src/vm/ConnectionNatives.hpp
Core/Tests/FakeNetworkAdapter.hpp
Core/Tests/fixtures/NetworkOps.java
```

File mới đề xuất:

```text
Core/include/phoneme/network/NetworkOperation.hpp
Core/include/phoneme/network/TlsAdapter.hpp
Core/src/network/NetworkOperation.cpp
Core/src/network/AppleTlsAdapter.cpp             # C++ wrapper qua platform C API
Core/Tests/NetworkAsyncTests.cpp
Core/Tests/NetworkLoopbackTests.cpp
Core/Tools/test-network-host.sh
```

## Phạm vi phải triển khai

1. Operation state machine: pending/completed/cancelled/failed.
2. Java thread suspend khi connect/read/write/accept/HTTP wait; scheduler wake khi complete.
3. Cancellation thật khi close/destroy/timeout/interruption.
4. Timeout cho connect/read/write/accept/HTTP.
5. Partial read/write đúng stream semantics.
6. Socket half-close và EOF.
7. UDP boundaries, source address/port và maximum datagram size.
8. ServerSocket accept không block toàn VM.
9. HTTP:
   - request body streaming hoặc bounded buffering.
   - redirects có limit và method rules.
   - chunked/content-length/connection-close body.
   - repeated headers.
   - response available trước body read theo contract.
10. HTTPS/TLS:
    - certificate chain metadata.
    - hostname validation.
    - TLS errors map đúng IOException/SecurityException.
    - cancellation không để NSURLSession callback truy cập freed state.
11. Connection ownership per app và close-all khi destroy.
12. Reconnect policy không tự ý replay non-idempotent request.
13. Security permission gate theo scheme/host/port.
14. Không giữ Runtime mutex trong bất kỳ network wait nào.

## Test bắt buộc

- Fake adapter deterministic async completion.
- Loopback TCP client/server với partial I/O.
- Connect/read/accept timeout.
- Close trong lúc blocked read.
- Thread interruption của network wait.
- UDP roundtrip và oversized datagram.
- HTTP chunked, redirect loop, truncated body, 1xx/204/304.
- HTTPS trusted/untrusted/hostname mismatch trên test server phù hợp.
- Destroy MIDlet khi callback đang pending.
- Hai MIDlet dùng network đồng thời không lẫn handles.

## Điều kiện hoàn thành

- Một Java thread blocked socket không chặn Canvas/input/thread khác.
- `cancel()` có test chứng minh platform operation dừng và callback late bị bỏ an toàn.
- Không buffer body không giới hạn.
- Real-device HTTPS test được chuẩn bị cho mục 19.

---

# 10. Push Registry end-to-end Core

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Phụ thuộc:** mục 09 cho listener connection; mục 06 cho permission.

## Mục tiêu

Nối Push Registry persistence hiện có với network availability, alarm scheduler và yêu cầu launch/resume MIDlet.

## File ownership

```text
Core/include/phoneme/push/PushRegistry.hpp
Core/src/push/PushRegistry.cpp
Core/src/vm/PushBuiltinClasses.cpp
Core/src/vm/PushNatives.cpp
Core/src/vm/PushNatives.hpp
Core/Tests/PushRegistryTests.cpp
Core/Tests/fixtures/PushOps.java
```

File mới đề xuất:

```text
Core/include/phoneme/push/PushDispatcher.hpp
Core/include/phoneme/push/PushFilter.hpp
Core/src/push/PushDispatcher.cpp
Core/src/push/PushFilter.cpp
Core/Tests/PushDispatcherTests.cpp
Core/Tools/test-push-dispatch-host.sh
```

## Phạm vi phải triển khai

1. Parse/validate connection schemes được hỗ trợ.
2. Filter matching theo scheme, peer address và port.
3. Register connection phải phối hợp với network listener ownership.
4. Incoming connection/datagram tạo launch request đúng một lần.
5. Alarm scheduler dùng monotonic/wall-clock policy rõ ràng và xử lý clock change.
6. Launch request persistence, retry và acknowledgement.
7. Coalesce duplicate nhưng không mất distinct event cần thiết.
8. Request expiry/backoff để tránh launch loop vô hạn.
9. Permission gate cho register connection/alarm.
10. API Runtime để:
    - poll eligible request.
    - mark launching.
    - acknowledge success/failure.
    - requeue có backoff.
11. Suite uninstall cleanup.
12. Recovery sau process restart.

## Không thuộc phạm vi

- Swift BGTask/UserNotifications implementation; thuộc mục 16.
- Full iOS always-on server guarantee; cần policy host.

## Test bắt buộc

- Incoming loopback connection tạo request.
- Filter accept/reject.
- Duplicate coalescing.
- Alarm replace/cancel/due.
- Clock jump.
- Launch fail và retry.
- Crash giữa mark-launching/ack.
- Cross-suite ownership.
- Permission denied.

## Điều kiện hoàn thành

- Push không còn chỉ là registry/hàng đợi do host tự notify thủ công.
- Core có dispatcher contract rõ để iOS host thực thi trong capability được cấp.

---

# 11. Graphics/Image/Font pixel accuracy và performance

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1

## Mục tiêu

Hardening implementation Graphics hiện có để đạt hành vi MIDP ổn định, pixel-correct và đủ nhanh cho game 2D.

## File ownership

```text
Core/include/phoneme/graphics/
Core/src/graphics/
Core/src/vm/ImageNatives.cpp
Core/src/vm/ImageNatives.hpp
Core/src/vm/GraphicsNatives.cpp
Core/src/vm/GraphicsNatives.hpp
Core/src/vm/GraphicsNativeSupport.hpp
Core/Tests/GraphicsModuleTests.cpp
Core/Tests/GraphicsVmTests.cpp
Core/Tests/fixtures/GraphicsOps.java
```

## Phạm vi phải triển khai

1. Golden tests cho mọi anchor combination hợp lệ/không hợp lệ.
2. Pixel rules cho:
   - line endpoints.
   - dotted stroke.
   - rect/roundrect.
   - arc/fillArc angles.
   - triangle fill edge rule.
   - clip/translate overflow.
3. `drawRegion` overlap/source==target behavior.
4. Alpha blend và `drawRGB(processAlpha=false)` chính xác.
5. `copyArea` overlap.
6. PNG decoder:
   - grayscale.
   - palette.
   - tRNS.
   - common bit depths.
   - interlace nếu cần cho corpus.
   - strict CRC/size/decompression limits.
7. Font metrics consistency giữa measure và rasterize.
8. Unicode fallback và missing glyph behavior.
9. Dùng dirty region để tránh full framebuffer copy khi phù hợp.
10. Không runtime scale làm mờ pixel art; scaling policy nằm host UI hoặc asset/game logic, không tự động trong Graphics.
11. Bounded temporary allocations trong drawRegion/text/PNG.
12. Benchmark sprite-heavy workload 320x240 ở 60 FPS target.

## Test bắt buộc

- Golden RGBA fixtures cho primitive và transforms.
- Fuzz PNG malformed corpus.
- Large dimension overflow.
- Self-overlap copy/draw.
- Unicode Vietnamese/Japanese.
- 10.000 draw calls/frame benchmark.
- ASan/UBSan.

## Điều kiện hoàn thành

- Golden output ổn định giữa host và iPhoneOS.
- Không có unbounded allocation theo dữ liệu PNG độc hại.
- Frame render không tạo Data/full-image copy dư ở Core.

---

# 12. Canvas/GameCanvas/Input và event scheduling

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Phụ thuộc:** scheduler mục 01; phối hợp với Graphics mục 11.

## Mục tiêu

Đưa Canvas callbacks vào event model đúng, không chạy trực tiếp tùy tiện từ host input thread và hoàn thiện repaint/key/pointer semantics.

## File ownership

```text
Core/include/phoneme/runtime/CanvasRuntime.hpp
Core/src/runtime/CanvasRuntime.cpp
Core/include/phoneme/vm/CanvasBridge.hpp
Core/src/vm/CanvasNatives.cpp
Core/src/vm/CanvasNatives.hpp
Core/src/vm/GameCanvasNatives.cpp
Core/src/vm/GameCanvasNatives.hpp
Core/Tests/fixtures/CanvasOps.java
```

File mới đề xuất:

```text
Core/include/phoneme/runtime/LcduiEventDispatcher.hpp
Core/src/runtime/LcduiEventDispatcher.cpp
Core/Tests/CanvasEventTests.cpp
Core/Tools/test-canvas-events-host.sh
```

## Phạm vi phải triển khai

1. Một LCDUI/Canvas event dispatch context per MIDlet hoặc policy tương thích rõ.
2. `paint`, key, pointer, show/hide, sizeChanged chạy theo thứ tự deterministic.
3. Repaint coalescing nhưng không mất repaint phát sinh trong paint.
4. `serviceRepaints()` block/schedule đúng mà không deadlock event thread.
5. Key repeat timer thật thay vì phụ thuộc host gửi press lặp.
6. `getKeyStates()` edge semantics, release và focus loss cleanup.
7. Suppress key events của GameCanvas đúng.
8. Pointer capability dựa trên host config; coordinates clipped/mapped đúng orientation.
9. `showNotify`/`hideNotify` đúng một lần mỗi visibility transition.
10. Fullscreen làm cập nhật dimensions/sizeChanged theo policy UI.
11. GameCanvas buffer ownership và flush region.
12. Exception trong callback làm app error có kiểm soát, không poison dispatcher.
13. Input của paused/background app bị chặn hoặc queue theo policy rõ.

## Test bắt buộc

- Repaint gọi trong paint.
- serviceRepaints từ event thread và worker thread.
- Key repeat timing deterministic clock.
- Focus loss release tất cả key states.
- Rapid show/hide/resize ordering.
- Pointer drag outside bounds.
- GameCanvas suppressKeyEvents true/false.
- Destroy trong callback.

## Điều kiện hoàn thành

- Host input API chỉ enqueue event; không trực tiếp chạy Java callback trên input queue thread.
- Không deadlock khi callback gọi lại repaint/setCurrent/flushGraphics.
- Event ordering được ghi thành test, không phụ thuộc unordered_map iteration.

---

# 13. LCDUI extended widgets và callback hai chiều

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1

## Mục tiêu

Bổ sung các class LCDUI còn thiếu và hoàn thiện reverse action từ native UI sang Java listener/state.

## File ownership

```text
Core/src/vm/LcduiBuiltinClasses.cpp
Core/src/vm/LcduiNatives.cpp
Core/src/vm/LcduiNatives.hpp
Core/src/vm/ChoiceNatives.cpp
Core/src/vm/ChoiceNatives.hpp
Core/include/phoneme/vm/LcduiBridge.hpp
Core/Tests/fixtures/LcduiApp.java
```

File mới đề xuất để giảm kích thước file hiện tại:

```text
Core/src/vm/AlertNatives.cpp
Core/src/vm/TextBoxNatives.cpp
Core/src/vm/DateFieldNatives.cpp
Core/src/vm/ImageItemNatives.cpp
Core/src/vm/CustomItemNatives.cpp
Core/src/vm/LcduiItemCallbacks.cpp
Core/Tests/LcduiExtendedTests.cpp
Core/Tests/fixtures/LcduiExtendedOps.java
Core/Tools/test-lcdui-extended-host.sh
```

## Phạm vi phải triển khai

1. `Alert` và `AlertType`, timeout, next Displayable, dismiss action; không tự phát beep nếu app policy không yêu cầu.
2. `TextBox` đầy đủ text/max size/constraints/caret.
3. `DateField` DATE/TIME/DATE_TIME và timezone conversion.
4. `ImageItem` layout/appearance/alt text/command.
5. `Spacer`.
6. `Ticker` và attach/detach/update.
7. `ItemStateListener` callback khi native user thay đổi TextField/Gauge/Choice/DateField.
8. `ItemCommandListener` khi activate item/default command.
9. `CustomItem` tối thiểu paint/input/traverse/size lifecycle qua Canvas-like bridge.
10. Focus/activate actions 101/102 phải có semantics thật, không chỉ echo event.
11. Date action kind 106 phải update Java object và listener.
12. Scroll position action 107 phải lưu/restore per Displayable hoặc bỏ API nếu không thuộc Core contract; không để silent no-op không tài liệu.
13. Command mapping ưu tiên/back/ok/screen/item theo MIDP, không biến List thành dropdown sai.
14. Display.setCurrent(Alert, next) nếu API được corpus dùng.

## Test bắt buộc

- Mỗi widget create/update/show/hide.
- Native edit kích hoạt ItemStateListener đúng một lần.
- Item default command kích hoạt ItemCommandListener.
- Alert timeout và manual dismiss.
- Text constraints/password/numeric validation.
- DateField roundtrip timezone.
- CustomItem paint và pointer/key callback.
- Form mutation trong listener.

## Điều kiện hoàn thành

- Không còn class LCDUI chính trong coverage list bị missing, ngoại trừ class được ghi rõ deferred.
- Reverse bridge thay đổi Java state trước khi gọi listener.
- Swift host có event model đủ thông tin để render; phần Swift thực thi thuộc mục 16.

---

# 14. MIDP Game API: Layer, Sprite, TiledLayer, LayerManager

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1  
**Phụ thuộc:** Graphics/Image ổn định từ mục 11.

## Mục tiêu

Triển khai `javax.microedition.lcdui.game` ngoài GameCanvas để game tile/sprite phổ biến chạy được.

## File ownership

Tạo module riêng, không nhét vào `LcduiNatives.cpp`:

```text
Core/include/phoneme/game/LayerRuntime.hpp
Core/include/phoneme/game/SpriteRuntime.hpp
Core/include/phoneme/game/TiledLayerRuntime.hpp
Core/src/game/LayerRuntime.cpp
Core/src/game/SpriteRuntime.cpp
Core/src/game/TiledLayerRuntime.cpp
Core/src/vm/GameBuiltinClasses.cpp
Core/src/vm/GameLayerNatives.cpp
Core/src/vm/GameLayerNatives.hpp
Core/Tests/GameApiTests.cpp
Core/Tests/fixtures/GameApiOps.java
Core/Tools/test-game-api-host.sh
```

## Phạm vi phải triển khai

1. `Layer` position, visibility, width/height.
2. `Sprite`:
   - frame sequence.
   - raw frame/current frame.
   - transforms.
   - reference pixel.
   - collision rectangle.
   - pixel-level collision.
   - collision với Sprite/TiledLayer/Image.
3. `TiledLayer`:
   - static tile set.
   - animated tiles.
   - cell set/get/fill.
   - paint.
4. `LayerManager`:
   - append/insert/remove/getSize/getLayerAt.
   - view window.
   - back-to-front paint order.
5. Transform/collision phải overflow-safe.
6. Không copy toàn image mỗi frame nếu có thể dùng immutable image reference.
7. Java object references phải được GC-root hoặc side-table prune đúng.

## Test bắt buộc

- Tất cả transforms và reference pixel.
- Frame sequences invalid/valid.
- Collision rectangle và pixel collision alpha threshold.
- Animated tile mapping.
- LayerManager ordering/view clipping.
- Move large/negative coordinates.
- GC sau khi layer/sprite bị bỏ tham chiếu.
- Golden render tests.

## Điều kiện hoàn thành

- API signatures khớp MIDP 2.0.
- Không phụ thuộc UIKit/Swift.
- Sprite-heavy benchmark không tạo allocation theo mỗi draw call.

---

# 15. MMAPI media hardening và optional capture/video surface

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1

## Mục tiêu

Hoàn thiện Player lifecycle/event semantics trên adapter async và bổ sung control thường gặp theo capability iOS.

## File ownership

```text
Core/include/phoneme/media/
Core/src/media/
Core/src/vm/MediaBuiltinClasses.cpp
Core/src/vm/MediaNatives.cpp
Core/src/vm/MediaNatives.hpp
Core/Tests/fixtures/MediaOps.java
phoneME/Core/PhoneMEMediaBridge.m
```

Không sửa Swift host ngoài API proposal; mục 16 tích hợp Swift.

File mới đề xuất:

```text
Core/include/phoneme/media/MediaEventQueue.hpp
Core/src/media/MediaEventQueue.cpp
Core/Tests/MediaAsyncTests.cpp
Core/Tools/test-media-host.sh
```

## Phạm vi phải triển khai

1. Player state machine đúng UNREALIZED/REALIZED/PREFETCHED/STARTED/CLOSED.
2. Illegal transition ném đúng exception.
3. Async PlayerListener events:
   - STARTED.
   - STOPPED.
   - END_OF_MEDIA.
   - DURATION_UPDATED.
   - VOLUME_CHANGED.
   - ERROR.
   - CLOSED nếu cần theo implementation.
4. Loop count chính xác, kể cả `-1` infinite.
5. Seek/media time/duration edge cases.
6. Remote URL buffering, failure, reconnect policy và cancellation.
7. Audio session interruption/route change lifecycle.
8. Background suspend/resume phân biệt app pause và host background playback policy.
9. Security permission gate cho capture/record và remote media nếu policy yêu cầu.
10. `RecordControl` nếu product cần capture audio.
11. `VideoControl` tối thiểu capability-query hoặc implementation thực nếu game corpus cần; không khai báo giả.
12. ToneControl sequence parser đầy đủ hơn playTone đơn lẻ.
13. Resource cleanup và late AVFoundation callback safety.
14. Không block Runtime mutex khi prefetch/start remote stream.

## Test bắt buộc

- Fake async adapter event ordering.
- Invalid lifecycle transitions.
- Loop count 1/2/infinite.
- Seek trước/sau start.
- End/error race.
- Close trong lúc loading.
- Remote URL local test server.
- iOS bridge unit/manual test matrix cho common formats.
- Audio interruption simulation nơi khả dụng.

## Điều kiện hoàn thành

- Host fake tests và iPhoneOS bridge compile pass.
- PlayerListener không được gọi trực tiếp từ arbitrary AVFoundation callback thread vào Machine; phải enqueue qua runtime/scheduler contract.
- Unsupported controls trả null/MediaException đúng, không object giả không hoạt động.

---

# 16. C ABI và iOS Swift/Objective-C host integration

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN ở lớp app, P1  
**Phụ thuộc:** API ổn định từ mục 02, 06, 09, 10, 12, 13, 15.

## Mục tiêu

Nối các subsystem Core mới vào app iOS mà không đưa logic VM vào Swift và không tạo thêm polling/buffer copy dư.

## File ownership

Agent này được sửa các file host sau, nhưng thay đổi header/CAPI trung tâm phải đưa thành patch cho mục 20:

```text
phoneME/Runtime/PhoneMECAPI.swift
phoneME/Runtime/EmbeddedPhoneMEEngine.swift
phoneME/Runtime/LCDUIState.swift
phoneME/Views/NativeLCDUIView.swift hoặc vị trí thực tế tương ứng
phoneME/Core/PhoneMEHTTPSBridge.m
phoneME/Core/PhoneMEMediaBridge.m
phoneME/Core/PhoneMEPlatformBridge.m
```

File mới đề xuất:

```text
phoneME/Runtime/PhoneMEPermissionCoordinator.swift
phoneME/Runtime/PhoneMEPushCoordinator.swift
phoneME/Runtime/PhoneMEBackgroundPolicy.swift
phoneME/Runtime/PhoneMEEventPump.swift
```

## Phạm vi phải triển khai

1. Permission prompt C callback → Swift UI, không deadlock runtime queue.
2. Suite trust/permission settings persistence và truyền vào Core.
3. Push:
   - poll pending requests khi host có background window.
   - launch/resume đúng suite/MIDlet.
   - acknowledge success/failure.
   - schedule local/background mechanism trong capability hợp lệ của iOS.
4. Event pump thống nhất cho framebuffer/LCDUI/media/push; tránh nhiều timer polling cạnh tranh.
5. Dynamic polling cadence; idle không poll 60 FPS.
6. Không tạo `Data`/`CGImage` mới khi frame generation không đổi.
7. Dirty frame/region nếu Core cung cấp.
8. App background policy rõ:
   - keep network running khi capability được cấp.
   - media background.
   - suspend render/UI polling.
   - resume an toàn.
9. Thread confinement của C runtime ref.
10. Late callback sau engine destroy phải bị bỏ an toàn.
11. Native LCDUI widgets cho các event mới của mục 13 theo iOS 16 style.
12. Không dùng private API hoặc giả định iOS cho chạy nền vô hạn.

## Test bắt buộc

- Engine create/destroy lặp nhiều lần.
- Background/foreground rapid transitions.
- Permission prompt allow/deny/session/blanket.
- Frame generation unchanged không allocate/deliver lại.
- Nhiều MIDlet switching.
- Pending HTTPS/media callback sau destroy.
- Push request launch/ack simulated.
- Build iPhoneOS 16 target thành công.

## Điều kiện hoàn thành

- Swift không chứa J2ME semantics; chỉ host/presentation/policy.
- Không có blocking dispatch semaphore trên main thread.
- Không re-enter cùng runtime từ callback khi Core chưa cho phép.
- Xcode Debug và Release iphoneos build pass sau mục 20 tích hợp.

---

# 17. Real-game compatibility corpus và differential testing

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P1/P2  
**Phụ thuộc:** có thể bắt đầu thu thập corpus ngay, nhưng kết luận cuối sau các module chính.

## Mục tiêu

Biến các nhận định “có API” thành bằng chứng chạy được game/app thật, đồng thời phát hiện class/method/semantics còn thiếu theo dữ liệu.

## File ownership

Không sửa implementation subsystem nếu chưa được giao thêm. Sở hữu test tooling/docs:

```text
Core/Compatibility/
Core/Tests/Compatibility/
docs/J2ME_API_COVERAGE.md
```

File mới đề xuất:

```text
Core/Compatibility/CORPUS.md
Core/Compatibility/expected-results.json
Core/Compatibility/run-corpus.sh
Core/Compatibility/analyze-failures.py hoặc tool phù hợp ngoài production
Core/Compatibility/fixtures/
Core/Tests/Compatibility/CompatibilityHarness.cpp
```

## Phạm vi phải triển khai

1. Corpus đại diện:
   - Canvas offline game.
   - GameCanvas threaded game.
   - Sprite/TiledLayer game.
   - LCDUI app.
   - RMS-heavy game.
   - HTTP/HTTPS game.
   - socket/UDP online game.
   - media app/game.
   - obfuscated/preverified old JAR.
2. Chỉ dùng JAR có quyền sử dụng hợp lệ trong repo/test environment; không commit nội dung không có quyền phân phối.
3. Tự động thu thập:
   - install result.
   - missing class/method/native.
   - verifier error.
   - uncaught exception.
   - startup time.
   - frame produced.
   - network/media actions.
   - exit state.
4. API coverage sinh từ class references trong corpus.
5. Differential behavior với phoneME/reference emulator khi hợp pháp và có thể.
6. Golden screenshots/frame hashes cho scene ổn định.
7. Failure taxonomy và ưu tiên theo số game bị ảnh hưởng.
8. Reproduction fixture nhỏ cho mỗi bug; không chỉ giữ JAR lớn.

## Test/đầu ra bắt buộc

- Báo cáo pass/fail per corpus item.
- Top missing APIs theo tần suất.
- Top verifier/runtime failures.
- Regression command chạy lại subset.
- Không đánh dấu pass chỉ vì app không crash; phải có observable milestone.

## Điều kiện hoàn thành

- Có dashboard/report reproducible.
- Mỗi compatibility bug quan trọng có minimized fixture hoặc log đủ tái hiện.
- Không sửa game JAR để che lỗi Core trừ compatibility patch được ghi rõ và optional.

---

# 18. Test infrastructure, isolated build roots, sanitizer và CI

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN ở test tooling, P0/P1

## Mục tiêu

Loại bỏ race giữa test scripts, chuẩn hóa standalone module tests và tạo pipeline xác nhận host + iPhoneOS.

## File ownership

```text
Core/Tools/
Core/Tests/.gitignore
```

Không sửa `Core/Tests/CoreTests.cpp`; mục 20 ghép suite.

File mới đề xuất:

```text
Core/Tools/test-all-host.sh
Core/Tools/test-module.sh
Core/Tools/test-sanitizers.sh
Core/Tools/test-concurrent.sh
Core/Tools/build-matrix.sh
Core/Tools/lib/common-test-root.sh
```

## Phạm vi phải triển khai

1. Mọi script nhận build root override riêng.
2. Mặc định tạo unique temp/build directory theo PID hoặc task ID, không `rm -rf` shared directory.
3. Có cleanup trap an toàn; không xóa path rỗng/root.
4. Chạy parallel module tests mà không race fixture classes/JAR.
5. ASan/UBSan preset.
6. Optional TSan preset cho code host hỗ trợ.
7. Fuzz target cho class parser/verifier/PNG/URL/JAD nếu toolchain cho phép.
8. iPhoneOS archive verification:
   - arm64.
   - deployment target.
   - no simulator slice.
   - no forbidden vendor symbols.
   - required objects present.
9. Xcode Debug và Release no-sign build command reproducible.
10. Test timeout/hang detection.
11. JUnit/TAP/JSON output tối thiểu để CI đọc.
12. Không phụ thuộc absolute developer path.

## Test bắt buộc

- Chạy hai `test-host.sh` đồng thời với root khác nhau.
- Chạy full module matrix parallel.
- Intentional failing test trả exit code đúng.
- Timeout process cleanup.
- Path safety test cho cleanup helper.

## Điều kiện hoàn thành

- Không còn trường hợp agent này xóa build output của agent khác.
- Một command chạy toàn bộ host/sanitizer/module tests.
- Build logs chỉ rõ module thất bại.

---

# 19. Performance, multi-MIDlet soak, RAM/CPU/battery và device validation

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG sau khi core ổn định, P2  
**Phụ thuộc:** mục 01, 02, 09, 11, 12, 15, 16.

## Mục tiêu

Đo và sửa bottleneck thực tế thay vì tối ưu cảm tính; xác nhận nhiều app/game chạy lâu trên iPhoneOS.

## File ownership

Chủ yếu benchmark/instrumentation; không sửa subsystem nếu chưa có assignment bổ sung:

```text
Core/Benchmarks/
docs/PERFORMANCE_BUDGET.md
phoneME/Diagnostics/
```

File mới đề xuất:

```text
Core/Benchmarks/VmBenchmark.cpp
Core/Benchmarks/GraphicsBenchmark.cpp
Core/Benchmarks/NetworkBenchmark.cpp
Core/Benchmarks/MultiMidletSoak.cpp
phoneME/Diagnostics/RuntimeMetrics.swift
```

## Phạm vi phải triển khai

1. Định nghĩa budget:
   - idle CPU.
   - active Canvas 30/60 FPS CPU.
   - RAM per MIDlet.
   - framebuffer/native image memory.
   - network idle wakeups.
   - battery/background wakeups.
2. Benchmark VM instruction throughput.
3. Graphics draw-call/framebuffer copy benchmark.
4. Three-or-more MIDlet soak.
5. Repeated install/start/destroy leak test.
6. Network reconnect/socket idle soak.
7. Media long playback and interruption.
8. Background/foreground 1000 cycles.
9. Memory warning response.
10. Instruments workflow cho Time Profiler, Allocations, Leaks, Energy Log, Hangs.
11. Device matrix tối thiểu iPhone 14/iOS 16 và một iOS mới hơn nếu có.
12. Không giảm correctness để đạt benchmark; mọi optimization cần regression test.

## Đầu ra bắt buộc

- Baseline và after metrics.
- Top hotspots có stack evidence.
- Leak/wakeup report.
- Reproduction command/profile.
- Danh sách optimization đề xuất theo ROI.

## Điều kiện hoàn thành

- Idle runtime không poll/render liên tục khi không có thay đổi.
- Ba MIDlet không làm CPU runaway hoặc deadlock trong soak target.
- Memory trở về gần baseline sau destroy all.
- Device build chạy ổn định theo thời lượng đã ghi trong budget.

---

# 20. Integration owner: registry, CMake, C API, Xcode project và full regression

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** ĐỘC QUYỀN, thực hiện sau/định kỳ khi nhận handoff từ các mục khác, P0  
**Đây là agent duy nhất được sửa các file trung tâm đã khóa ở mục 0.3.**

## Mục tiêu

Ghép các module độc lập, giải quyết registration/build conflicts và xác nhận toàn repository vẫn build/test được mà không làm thay đổi semantics của module.

## File ownership độc quyền

```text
Core/CMakeLists.txt
Core/src/vm/BuiltinClasses.cpp
Core/Tests/CoreTests.cpp
Core/include/PhoneMECore.h
Core/src/api/CAPI.cpp
phoneME.xcodeproj/project.pbxproj
phoneME/Support/phoneME-Bridging-Header.h
Core/README.md
Core/FULL_PORT_HANDOFF.md
Core/PARALLEL_SUBAGENT_WORKPLAN.md        # nội dung kế hoạch; agent khác chỉ sửa status block của mục mình
```

## Phạm vi phải triển khai

1. Nhận handoff của từng mục và thêm source vào CMake/Xcode.
2. Đăng ký built-in class factory và native registry đúng package ownership.
3. Không dồn implementation mới vào file trung tâm.
4. Ghép standalone tests vào full suite khi phù hợp.
5. Giải quyết descriptor/layout conflict bằng test registry trước khi sửa implementation.
6. Giữ ABI backward-compatible hoặc version C ABI rõ.
7. Nối C API mới cho scheduler/security/push/dirty frame nếu đã ổn định.
8. Verify public header C-compatible.
9. Cập nhật docs/API coverage theo implementation đã merge thật.
10. Chạy toàn bộ test/build matrix.
11. Kiểm tra forbidden dependency/vendor symbols.
12. Review ownership/isolation và remove temporary integration hacks.

## Full regression bắt buộc

```sh
bash Core/Tools/test-builtin-registry.sh
bash Core/Tools/test-host.sh
PHONEME_SANITIZE=1 bash Core/Tools/test-host.sh
bash Core/Tools/test-graphics-host.sh
bash Core/Tools/test-graphics-vm-host.sh
bash Core/Tools/test-push-host.sh
bash Core/Tools/build-iphoneos.sh
bash Core/Tools/verify-iphoneos.sh
```

Thêm tất cả standalone test scripts mới từ mục 01–19.

Sau đó build app:

```sh
xcodebuild \
  -project phoneME.xcodeproj \
  -scheme phoneME \
  -configuration Debug \
  -sdk iphoneos \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO \
  build
```

Và tương tự cho Release.

## Điều kiện hoàn thành

- Không có source module bị bỏ khỏi archive do quên CMake/Xcode integration.
- Full host test, sanitizer, iPhoneOS verify, Debug và Release app build pass.
- Không có duplicate native registration hoặc built-in layout mismatch.
- Git diff không chứa generated build products.
- Tài liệu tiến độ chỉ đánh dấu phần đã có test thực sự.

---

# 21. Optional APIs theo corpus: Wireless Messaging, Bluetooth, Location, SIP, PIM

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P2/P3  
**Chỉ bắt đầu khi mục 17 chứng minh corpus cần.**

## Mục tiêu

Bổ sung JSR theo nhu cầu thực tế, không tự mở rộng vô hạn trước khi MIDP/CLDC lõi ổn định.

## Quy tắc

Mỗi JSR phải tách thành submodule và test riêng. Không gộp tất cả vào một agent nếu scope lớn.

Các candidate:

1. JSR-120/205 Wireless Messaging.
2. JSR-82 Bluetooth.
3. JSR-179 Location.
4. JSR-75 PIM phần còn lại.
5. JSR-180 SIP.
6. Nokia/Samsung/Sony Ericsson vendor APIs theo compatibility adapters.

## Điều kiện để mở task con

- Có ít nhất một JAR corpus hợp lệ phụ thuộc API đó.
- Có danh sách class/method thực tế được dùng.
- Có platform capability/policy iOS rõ.
- Không làm giả API trả success nếu không thể thực hiện.

## Handoff đề xuất

Tạo tài liệu riêng:

```text
Core/Compatibility/JSR-<number>-HANDOFF.md
```

với ownership, security permission, platform adapter và test matrix cụ thể.

---

# 22. Optional compatibility layer cho game J2ME cũ/không chuẩn

**TRẠNG THÁI**
- Trạng thái: `TODO`
- Agent: `UNASSIGNED`
- Worktree/Branch: `-`
- Bắt đầu: `-`
- Cập nhật cuối: `-`
- Blocker: `-`
- Handoff: `-`

**Loại:** SONG SONG, P2  
**Phụ thuộc:** mục 17 cung cấp failure evidence.

## Mục tiêu

Hỗ trợ game cũ có classfile/preverification/vendor quirks mà không làm yếu verifier hoặc semantics mặc định cho mọi suite.

## File ownership đề xuất

```text
Core/include/phoneme/compat/
Core/src/compat/
Core/Compatibility/patches/
```

## Phạm vi

1. Per-suite compatibility profile, mặc định strict.
2. Known-safe verifier relaxations có điều kiện và log rõ.
3. Key code/device property aliases.
4. Canvas dimension/fullscreen quirks.
5. HTTP header/redirect quirks.
6. RMS migration/import adapters nếu cần.
7. Vendor property/class shims tối thiểu theo corpus.
8. Không patch bytecode/JAR âm thầm; mọi transform phải deterministic, checksumed và có audit log.

## Test bắt buộc

- Strict mode vẫn reject malformed input.
- Compatibility profile chỉ áp dụng đúng suite/signature/version đã match.
- Mỗi quirk có minimized fixture.
- Không làm game khác thay đổi behavior.

## Điều kiện hoàn thành

- Compatibility workaround không nằm rải rác trong VM core.
- Có registry/profile tập trung và có thể tắt.
- Không dùng compatibility layer để che memory corruption hoặc parser bug thật.

---

# Bảng phân công nhanh

| Mục | Tên ngắn | Ưu tiên | Có thể bắt đầu ngay | Phụ thuộc chính |
|---:|---|:---:|:---:|---|
| 01 | Java Scheduler/Threads | P0 | Có | Không |
| 02 | Runtime lifecycle/locking | P0 | Chuẩn bị được | 01 |
| 03 | GC/OOM/native roots | P0/P1 | Chuẩn bị được | 01 |
| 04 | CLDC library | P1 | Có | 01 cho Timer/PrintStream async |
| 05 | Installer/JAD | P1 | Có | Không |
| 06 | Security integration | P1 | Có | 05 để lấy declared permissions |
| 07 | RMS hardening | P1 | Có | 01 cho concurrency thật |
| 08 | Filesystem/JSR-75 | P1 | Có | 06 gate API |
| 09 | Network async/TLS | P0/P1 | Có thể refactor adapter | 01, 06 |
| 10 | Push end-to-end | P1 | Có thể làm registry/dispatcher | 09, 06 |
| 11 | Graphics accuracy/perf | P1 | Có | Không |
| 12 | Canvas event scheduling | P1 | Có thể chuẩn bị | 01, 11 |
| 13 | LCDUI extended | P1 | Có | 12 cho dispatcher cuối |
| 14 | Game API | P1 | Có | 11 |
| 15 | Media hardening | P1 | Có | 01, 06 cho async/gate cuối |
| 16 | iOS host integration | P1 | Chuẩn bị được | API từ 02/06/09/10/13/15 |
| 17 | Compatibility corpus | P1/P2 | Có | Module hoàn thiện dần |
| 18 | Test infrastructure | P0/P1 | Có | Không |
| 19 | Performance/device soak | P2 | Chuẩn bị benchmark | 01/02/16 |
| 20 | Integration owner | P0 | Chạy định kỳ | Handoff mọi mục |
| 21 | Optional JSRs | P2/P3 | Chưa | 17 chứng minh nhu cầu |
| 22 | Compatibility layer | P2 | Chưa | 17 chứng minh quirk |

---

# Nhóm có thể chạy song song ngay

Một đợt phân công ít conflict có thể là:

```text
Agent A  -> mục 01
Agent B  -> mục 05
Agent C  -> mục 07
Agent D  -> mục 08
Agent E  -> mục 11
Agent F  -> mục 13
Agent G  -> mục 14
Agent H  -> mục 15
Agent I  -> mục 17
Agent J  -> mục 18
Agent K  -> mục 20 (chỉ integration định kỳ)
```

Sau khi mục 01 có scheduler API ổn định:

```text
Agent L  -> mục 02
Agent M  -> mục 03
Agent N  -> mục 09
Agent O  -> mục 12
```

Sau khi installer/security/network API ổn định:

```text
Agent P  -> mục 06
Agent Q  -> mục 10
Agent R  -> mục 16
```

Mục 19 chạy sau khi các đường execution chính đã merge. Mục 21 và 22 chỉ mở dựa trên dữ liệu từ mục 17.

---

# Tiêu chuẩn review chung trước khi merge một mục

Một mục chỉ được chuyển sang integration khi đáp ứng toàn bộ:

1. Không có skeleton/no-op trên API được tuyên bố hoàn thành.
2. Có test positive, negative và lifecycle/cleanup.
3. Không giữ global Runtime lock qua blocking operation.
4. Không leak file descriptor, socket, media handle hoặc Java root.
5. Error map thành Java exception/Core error đúng loại.
6. Integer conversion và size arithmetic overflow-safe.
7. Per-suite/per-MIDlet isolation được test.
8. Sanitizer pass nếu module chạy được trên host.
9. iPhoneOS compile pass nếu module có platform adapter.
10. Có handoff rõ cho mục 20, không tự sửa file trung tâm.

---

# Definition of Done của toàn kế hoạch

Không mục riêng nào được gọi là “full port”. Full port chỉ hoàn thành khi:

1. Mục 01–20 đã merge và full regression pass.
2. Game/app corpus mục 17 đạt target đã thống nhất.
3. Không còn blocker Thread/scheduler, missing MIDP core class hoặc global runtime blocking.
4. Nhiều MIDlet online/offline chạy đồng thời không lẫn state hoặc làm CPU runaway.
5. RMS/files/security/network/media/push có isolation và crash-safe behavior.
6. LCDUI/Canvas/Game API đủ cho corpus mục tiêu.
7. Debug và Release iPhoneOS arm64 build, chạy và soak trên thiết bị thật.
8. Optional JSR/compatibility chỉ được thêm theo nhu cầu corpus, không làm yếu core chuẩn.
