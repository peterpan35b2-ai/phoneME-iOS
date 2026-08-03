# Continuous subagent review notes — phoneME C++ Core

Ngày tạo: 2026-08-02 18:59 +07:00  
Workplan: [`Core/PARALLEL_SUBAGENT_WORKPLAN.md`](PARALLEL_SUBAGENT_WORKPLAN.md)  
Mục đích: lưu review độc lập cho từng mục công việc để subagent đọc được findings mới nhất mà không phải sửa nội dung workplan.

---

## 0. Cách sử dụng file review

File này là nơi reviewer ghi nhận liên tục:

- Findings phát hiện trong code hiện tại.
- Rủi ro kiến trúc và dependency với mục khác.
- Yêu cầu sửa trước khi integration.
- Kết quả review theo commit/checkpoint.
- Quyết định approve, request changes hoặc block integration.

Mỗi mục trong workplan có link trực tiếp đến section tương ứng trong file này.

### 0.1 Quy tắc dành cho subagent

1. Trước khi claim hoặc bắt đầu lại một mục, đọc toàn bộ section review của mục đó.
2. Đọc cả phần `Global findings` vì có thể áp dụng cho nhiều module.
3. Không xóa, viết lại hoặc đánh dấu resolved findings của reviewer.
4. Khi đã xử lý một finding, ghi mã finding trong báo cáo handoff và commit liên quan.
5. Reviewer là người đổi finding từ `OPEN` sang `RESOLVED`, `ACCEPTED` hoặc `SUPERSEDED`.
6. Nếu không đồng ý với finding, subagent ghi giải trình trong handoff; không tự sửa review note.
7. Mục có finding `BLOCKER` còn `OPEN` không được chuyển sang `READY_FOR_INTEGRATION`.
8. Mỗi checkpoint review phải chỉ rõ commit SHA hoặc worktree/branch đã review.
9. Review `APPROVED` chỉ áp dụng cho commit được ghi trong entry, không tự động áp dụng cho commit mới hơn.
10. Mục 20 phải kiểm tra lại toàn bộ findings chưa đóng trước khi đánh dấu `DONE`.

### 0.2 Trạng thái review

- `NOT_REVIEWED`: chưa có checkpoint implementation để review.
- `REVIEWING`: đang review một checkpoint cụ thể.
- `CHANGES_REQUESTED`: có finding phải sửa.
- `APPROVED_FOR_INTEGRATION`: checkpoint đã đạt yêu cầu standalone.
- `INTEGRATION_REVIEW`: đang review sau khi ghép file trung tâm.
- `INTEGRATED_VERIFIED`: đã qua full regression trên commit tích hợp.

### 0.3 Mức độ finding

- `BLOCKER`: không được integration trước khi sửa.
- `HIGH`: có nguy cơ crash, deadlock, corruption, sai isolation hoặc sai API lớn.
- `MEDIUM`: semantics chưa đủ, test thiếu hoặc có rủi ro compatibility đáng kể.
- `LOW`: maintainability, diagnostics, naming hoặc tối ưu không chặn merge ngay.
- `NOTE`: thông tin cần lưu ý, chưa phải defect.

### 0.4 Mẫu entry review mới

```text
### Review checkpoint YYYY-MM-DD HH:mm +07:00

- Review status: CHANGES_REQUESTED
- Reviewer: <name/model>
- Reviewed commit: <sha>
- Worktree/Branch: <path-or-branch>
- Test evidence: <commands/results>

#### Rxx-YYYYMMDD-NN — <title>
- Severity: BLOCKER|HIGH|MEDIUM|LOW|NOTE
- State: OPEN|RESOLVED|ACCEPTED|SUPERSEDED
- Files: <paths>
- Evidence: <specific code/test evidence>
- Required action: <what must change>
- Resolution evidence: <filled by reviewer later>
```

---

## 1. Global findings

### RG-20260802-01 — Không dùng chung test root giữa subagent

- Severity: `HIGH`
- State: `OPEN`
- Applies to: tất cả mục có host test
- Evidence: các script test hiện dùng `rm -rf` build root mặc định; chạy song song từng gây compile/link failure giả do agent này xóa output của agent khác.
- Required action: mọi agent dùng test root riêng theo mã mục; mục 18 chuẩn hóa default isolation.

### RG-20260802-02 — File trung tâm chỉ do mục 20 sửa

- Severity: `HIGH`
- State: `OPEN`
- Applies to: mục 01–19, 21–22
- Files: `Core/CMakeLists.txt`, `Core/src/vm/BuiltinClasses.cpp`, `Core/Tests/CoreTests.cpp`, `Core/include/PhoneMECore.h`, `Core/src/api/CAPI.cpp`, Xcode project và bridging header.
- Required action: module agent chỉ tạo registrar/source/test riêng và ghi integration instructions trong handoff.

### RG-20260802-03 — Scheduler là dependency thật, không được mô phỏng bằng blocking host call

- Severity: `BLOCKER`
- State: `OPEN`
- Applies to: 01, 02, 03, 04, 09, 10, 12, 15, 16
- Evidence: VM hiện không có suspendable Java execution context; blocking socket/HTTPS và callback Java có thể giữ Runtime mutex hoặc host thread.
- Required action: không thêm sleep/poll loop, semaphore chờ hoặc detached callback trực tiếp vào Java object để né scheduler contract.

### RG-20260802-04 — Security policy hiện chưa gate tài nguyên thật

- Severity: `HIGH`
- State: `OPEN`
- Applies to: 05, 06, 08, 09, 10, 15, 16
- Evidence: policy engine tồn tại nhưng network/filesystem/media chưa gọi `PermissionPolicy::require()`; Runtime chưa enforce declared permissions.
- Required action: thiết kế một gate contract thống nhất và test deny/allow tại điểm mở tài nguyên.

### RG-20260802-05 — Host test pass không chứng minh iOS adapter/device semantics

- Severity: `MEDIUM`
- State: `OPEN`
- Applies to: 09, 11, 15, 16, 19
- Evidence: Network sử dụng fake adapter; Media có host fallback; Graphics host test không thay thế device rendering/performance.
- Required action: giữ riêng kết luận host correctness và device/platform verification; không đánh dấu production complete chỉ vì host test pass.

### RG-20260802-06 — Không ghi native pointer vào Java state hoặc serialized state

- Severity: `BLOCKER`
- State: `OPEN`
- Applies to: tất cả module có native handles
- Required action: dùng integer handle registry có generation/ownership; thêm stale-handle và cleanup tests.

---

<a id="review-01"></a>
# Review 01 — Java scheduler, Thread và monitor blocking

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 01](PARALLEL_SUBAGENT_WORKPLAN.md#01-java-scheduler-thread-và-monitor-blocking)

## Initial audit findings

### R01-20260802-01 — Thread surface hiện chưa đủ để chạy game loop

- Severity: `BLOCKER`
- State: `RESOLVED`
- Required action: triển khai Java thread lifecycle và scheduler thật.
- Resolution evidence: commit `3c4fd4a` triển khai identity/state/start/sleep/join/yield/currentThread/interruption/priority và worker execution context; fixture pass.

### R01-20260802-02 — Monitor contention hiện trả unsupported

- Severity: `BLOCKER`
- State: `RESOLVED`
- Required action: block execution context, enqueue waiter, preserve monitor/stack state.
- Resolution evidence: MonitorTable có FIFO entry queue, reentrant depth, wait set, notify/notifyAll, reacquire depth và cancellation; contended synchronized fixture pass.

### R01-20260802-03 — Cần test cleanup khi thread kết thúc bất thường

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: test uncaught exception, monitor release, join wakeup, interrupted sleep/wait và suspended roots.
- Resolution evidence: `ThreadOps` và `SchedulerTests` bao phủ các đường trên; normal và ASan/UBSan pass.

#### R01-20260802-04 — Shutdown có thể treo với Java loop không block

- Severity: `BLOCKER`
- State: `RESOLVED`
- Evidence: checkpoint `3c4fd4a` chỉ kiểm tra stop token trước `invoke_instance`; worker trong vòng lặp bytecode vô hạn làm `Scheduler::shutdown()` join vô hạn.
- Required action: cooperative cancellation tại interpreter safepoint và regression test busy worker.
- Resolution evidence: interpreter kiểm tra `current_stop_requested()` mỗi dispatch; fixture `BusyTask` chạy vòng lặp vô hạn và test yêu cầu shutdown dưới 2 giây. Normal + ASan/UBSan pass.

## Review log

### Review checkpoint 2026-08-02 21:33 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: `3c4fd4a` cộng shutdown-safepoint follow-up trong shared checkout.
- Test evidence: scheduler normal và ASan/UBSan PASS; host/sanitizer module matrices PASS.
- Remaining process action: commit follow-up scheduler source/fixture/test và cập nhật Handoff SHA.

---

<a id="review-02"></a>
# Review 02 — Runtime lifecycle, lock boundary và multi-MIDlet execution

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 02](PARALLEL_SUBAGENT_WORKPLAN.md#02-runtime-lifecycle-lock-boundary-và-multi-midlet-execution)

## Initial audit findings

### R02-20260802-01 — Java callback đang có thể chạy dưới Runtime mutex

- Severity: `BLOCKER`
- State: `RESOLVED`
- Evidence: checkpoint cũ gọi constructor/startApp/pause/resume/destroy, Canvas pump, input và LCDUI reverse callback trong `Runtime::mutex_`.
- Required action: prepare/execute/commit, không giữ state mutex qua Java hoặc blocking native.
- Resolution evidence: Runtime snapshot `shared_ptr<ApplicationVM>` + lifecycle token dưới lock, chạy VM/Canvas/Media/File setup ngoài lock và commit lại nếu token còn hợp lệ. `ApplicationVM::operation_mutex` serialize per-app execution. Fixture permission prompt gọi ngược `Runtime::is_running/app_state` trong start/pause/resume/destroy và pass, chứng minh không deadlock.

### R02-20260802-02 — `stop()` chưa thực hiện destroy lifecycle

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: graceful/forced stop, gọi `destroyApp(true)` khi có thể và không giữ Runtime lock.
- Resolution evidence: `stop()` move app snapshots và clear host state dưới lock, sau đó hide/pump/invoke `destroyApp(true)` best-effort ngoài lock. Scheduler stop safepoint ngăn busy Java loop treo Machine teardown.

### R02-20260802-03 — Paused app không nên mặc định nhận input

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: test input routing theo active/paused/background/foreground và stale generation.
- Resolution evidence: key/pointer enqueue và dispatch chỉ nhận app `active + foreground + matching generation`; Canvas regression xác nhận paused app không nhận pointer và resumed app nhận lại. Normal + ASan/UBSan PASS.

#### R02-20260802-04 — Runtime lifecycle checkpoint chưa có commit sạch

- Severity: `HIGH`
- State: `OPEN`
- Files: `Core/include/phoneme/runtime/Runtime.hpp`, `Core/src/runtime/Runtime.cpp`, lifecycle/Canvas fixtures/tests.
- Evidence: shared checkout đang được nhiều agent cập nhật; source hiện pass nhưng không có một SHA tái tạo độc lập.
- Required action: commit item 02 ownership sau khi resolve concurrent edits, chạy host + ASan/UBSan + Canvas module từ worktree sạch và cập nhật Handoff.

## Review log

### Review checkpoint 2026-08-02 22:05 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Test evidence: full host normal + ASan/UBSan PASS; Canvas runtime normal + ASan/UBSan PASS; synchronous lifecycle re-entry fixture PASS.

---

<a id="review-03"></a>
# Review 03 — Heap, GC safepoint, OOM và native handle roots

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 03](PARALLEL_SUBAGENT_WORKPLAN.md#03-heap-gc-safepoint-oom-và-native-handle-roots)

## Initial audit findings

### R03-20260802-01 — Root model phải mở rộng cho suspended threads

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: publish locals, operand stacks, pending exception, held monitors và native continuation roots cho mọi Java execution context.

### R03-20260802-02 — OOM path cần emergency reserve

- Severity: `HIGH`
- State: `OPEN`
- Required action: bảo đảm tạo/throw `OutOfMemoryError` không cần allocation thông thường; test allocation failure ở object, array, string và native conversion.

### R03-20260802-03 — Heap budget hiện chưa phản ánh byte usage chính xác

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: có accounting bytes/objects rõ, overflow-safe, per-MIDlet limit và diagnostics.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-04"></a>
# Review 04 — CLDC `java.lang`, `java.io`, `java.util`

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 04](PARALLEL_SUBAGENT_WORKPLAN.md#04-hoàn-thiện-cldc-javalang-javaio-javautil)

## Initial audit findings

### R04-20260802-01 — API inventory phải đối chiếu CLDC, không dựa vào Java SE ngẫu nhiên

- Severity: `HIGH`
- State: `OPEN`
- Required action: lập signature matrix CLDC 1.0/1.1 và phoneME behavior; tránh thêm API Java SE làm sai hierarchy/layout.

### R04-20260802-02 — Throwable semantics còn mỏng

- Severity: `HIGH`
- State: `OPEN`
- Required action: message/cause/stack trace boundary tối thiểu phù hợp J2ME; constructor overloads và `toString` đúng.

### R04-20260802-03 — Timer/TimerTask phụ thuộc scheduler

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: không triển khai bằng host detached thread gọi Java trực tiếp; dùng scheduler/timer queue của mục 01.

### R04-20260802-04 — PrintStream custom OutputStream chưa hoàn chỉnh

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: hỗ trợ dispatch Java override sau khi execution continuation ổn định; test recursion/error state.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-05"></a>
# Review 05 — JAD, installer và persistent Suite Store

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 05](PARALLEL_SUBAGENT_WORKPLAN.md#05-jad-installer-và-persistent-suite-store)

## Initial audit findings

### R05-20260802-01 — SuiteStore hiện chỉ tồn tại trong memory

- Severity: `BLOCKER`
- State: `RESOLVED`
- Evidence: suite ID/archive metadata không survive Runtime restart; JAR vẫn tham chiếu đường dẫn nguồn.
- Required action: transactional managed install, persistent metadata, stable suite identity, rollback và uninstall.
- Resolution evidence: commits `7cf49d5` và `b25c246` thêm managed suite storage, `SuiteDatabase`, stable identity, upgrade/uninstall và reload sau restart; standalone installer test pass.

### R05-20260802-02 — Chưa có JAD merge/validation

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: parse continuation/encoding, JAD precedence, `MIDlet-Jar-Size`, URL, name/vendor/version identity và mismatch rejection.
- Resolution evidence: `JadParser` và `SuiteInstaller::inspect` đã parse/merge JAD + manifest, kiểm tra identity mismatch, JAR size, MIDlet entries và class-file declaration; positive/negative tests pass.

### R05-20260802-03 — Permission/signing metadata phải là output có cấu trúc

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: cung cấp declared required/optional permissions và trust evidence cho mục 06; không để Security tự parse lại raw manifest.
- Resolution evidence: `SuiteDescriptor`/`Suite` xuất required, optional, merged permissions và `ArchiveTrustEvidence`; commit `b25c246` thêm signature metadata digest có cấu trúc.

### R05-20260802-04 — CRC32 không đủ làm identity bảo mật

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: dùng cryptographic content digest cho managed archive/version matching; CRC có thể giữ cho ZIP integrity nhưng không làm identity duy nhất.
- Resolution evidence: suite identity và archive matching dùng SHA-256; CRC32 chỉ còn phục vụ archive/database integrity phụ trợ.

## Review log

### Review checkpoint 2026-08-02 19:29 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed commits: `7cf49d5`, `b25c246`
- Worktree/Branch: `/Users/duypham/Developer/phoneME-iOS @ main`
- Test evidence:
  - `bash Core/Tools/test-suite-installer-host.sh` — pass.
  - `PHONEME_SANITIZE=1 bash Core/Tools/test-suite-installer-host.sh` — pass.
- Scope note: review standalone implementation; CMake/Runtime integration vẫn thuộc mục 20.

#### R05-20260802-05 — Suite file activation chưa được directory-fsync trước database commit

- Severity: `BLOCKER`
- State: `RESOLVED`
- Files: `Core/src/runtime/SuiteStore.cpp`
- Evidence: checkpoint cũ chưa fsync directory entries trước database commit.
- Required action: sync staged files + stage directory, activate rename, fsync `suites/`, sau đó mới commit DB; thêm fault/crash tests.
- Resolution evidence: commit `6548b5f` fsync stage/file/parent và activation directory trước database commit; fault matrix xác nhận rollback/restart tại các checkpoint.

#### R05-20260802-06 — Database commit có thể báo lỗi sau khi primary đã bị thay thế

- Severity: `HIGH`
- State: `RESOLVED`
- Files: `Core/src/runtime/SuiteDatabase.cpp`, `Core/src/runtime/SuiteStore.cpp`
- Evidence: checkpoint cũ có thể rollback suite files nhưng để DB primary ở generation mới.
- Required action: commit API phải phân biệt hoặc tự restore primary/backup và fsync trước khi trả lỗi.
- Resolution evidence: `6548b5f` thêm structured commit outcome và backup restore; SuiteStore rollback đồng bộ DB và suite directory.

#### R05-20260802-07 — Profile/configuration validation chỉ kiểm tra prefix

- Severity: `MEDIUM`
- State: `RESOLVED`
- Files: `Core/src/runtime/SuiteInstaller.cpp`
- Required action: parse capability token và reject runtime version không hỗ trợ.
- Resolution evidence: capability matrix hỗ trợ `MIDP-1.0/2.0`, `CLDC-1.0/1.1`; unsupported-only declarations bị từ chối, mixed supported token được test.

### Review checkpoint 2026-08-02 20:48 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed source commit: `6548b5fefb111e5085db5871ba79e223006178c2`
- Test evidence: isolated worktree normal và ASan/UBSan đều PASS.
- Remaining process action: commit fallback của `Core/Tools/test-suite-installer-host.sh` để lệnh handoff tự chạy trước khi mục 18 được tích hợp.

---

<a id="review-06"></a>
# Review 06 — Security domain, manifest permissions và resource gating

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 06](PARALLEL_SUBAGENT_WORKPLAN.md#06-security-domain-manifest-permissions-và-resource-gating)

## Initial audit findings

### R06-20260802-01 — Policy engine chưa nối vào resource open path

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: gate network, filesystem, media và push tại đúng operation; deny phải thành Java `SecurityException`/API-specific exception đúng chuẩn.

### R06-20260802-02 — Declared permission enforcement hiện bị tắt

- Severity: `HIGH`
- State: `OPEN`
- Required action: nhận metadata từ installer; required/optional semantics và trusted/untrusted domain phải có test.

### R06-20260802-03 — Prompt callback không được chạy dưới Runtime lock

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: prompt async/suspendable hoặc host-safe boundary; chống re-entry và policy mutation race.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-07"></a>
# Review 07 — RMS semantics, crash recovery và stress verification

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 07](PARALLEL_SUBAGENT_WORKPLAN.md#07-rms-semantics-crash-recovery-và-stress-verification)

## Initial audit findings

### R07-20260802-01 — Advanced RMS fixture chưa được chạy trong full suite

- Severity: `HIGH`
- State: `RESOLVED`
- Evidence: fixture có listener/live enumeration/comparator/multi-handle tests nhưng invocation chưa được ghép vào `CoreTests.cpp`.
- Required action: tạo standalone RMS test script hoặc handoff mục 20 ghép fixture; chứng minh test thực sự execute.
- Resolution evidence: commit `66a435f` thêm `RmsAdvancedTests.cpp`, fixture mở rộng và `test-rms-host.sh`; Java fixture được invoke thực sự và pass.

### R07-20260802-02 — Cần stress crash points và quota rollback

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: fault injection trước/sau temp write, fsync, rename, backup; verify reopen chọn đúng generation và không consume record ID sai.
- Resolution evidence: fault points bao phủ write/file fsync/backup link/rename/directory fsync và after-*; tests xác nhận rollback memory, generation recovery, record ID persistence, migration/future version và process crash.

### R07-20260802-03 — Concurrent semantics phải test sau scheduler

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: nhiều Java thread/handle cùng suite; listener re-entry và exception behavior phải xác định rõ.
- Resolution evidence: standalone suite có concurrent registry stress và Java VM fixture cho nhiều handles/listener/enumeration; sanitizer pass trên checkpoint.

## Review log

### Review checkpoint 2026-08-02 19:29 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed commit: `66a435f`
- Worktree/Branch: `/Users/duypham/Developer/phoneME-iOS @ main`
- Test evidence:
  - `bash Core/Tools/test-rms-host.sh` — pass.
  - `PHONEME_SANITIZE=1 bash Core/Tools/test-rms-host.sh` — pass.
- Build note: sanitizer compile phát hiện một warning không thuộc RMS tại `LcduiNatives.cpp`.

#### R07-20260802-04 — `delete_store` không rollback khi directory sync thất bại

- Severity: `HIGH`
- State: `RESOLVED`
- Files: `Core/src/runtime/RecordStoreRegistry.cpp`
- Evidence: `delete_store()` unlink canonical/tmp/bak trước, sau đó gọi `sync_directory_unlocked()`. Nếu sync hoặc injected directory-sync lỗi, method trả failure nhưng giữ store trong `stores_`; disk files có thể đã biến mất và không có tombstone/backup để restore.
- Impact: trong phiên hiện tại store có thể vẫn đọc từ memory, nhưng restart làm mất store dù API đã báo delete thất bại; memory và disk diverge.
- Required action: dùng tombstone/rename transaction hoặc preserve generation để rollback; thêm fault test tại directory sync của delete-store và restart validation.
- Resolution evidence: commit `a8cd498` stage toàn bộ path family bằng rename sang tombstone, rollback ngược thứ tự và fsync directory nếu fault xảy ra; test xác nhận current registry và restart đều giữ generation.

#### R07-20260802-05 — `list_store_names` che giấu store bị corruption

- Severity: `HIGH`
- State: `RESOLVED`
- Files: `Core/src/runtime/RecordStoreRegistry.cpp`
- Evidence: khi `recover_file_unlocked()` trả checksum/malformed/unsupported error, `list_store_names()` chỉ propagate `io_error`, còn các lỗi corruption bị `continue` và store biến mất khỏi danh sách.
- Impact: caller không phân biệt “không có store” với “store tồn tại nhưng hỏng”, trái mục tiêu corruption detection và có thể dẫn đến tạo store mới che dữ liệu hỏng.
- Required action: propagate corruption/future-version errors hoặc trả structured per-store diagnostic; thêm test list với canonical/tmp/bak đều corrupt.
- Resolution evidence: commit `a8cd498` chỉ bỏ qua `class_not_found`, propagate checksum/malformed/unsupported; tests thêm all-corrupt và future-version listing.

### Review checkpoint 2026-08-02 19:52 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed commits: `66a435f`, `a8cd498145fac85fbdfcf8848c252e31fea78f8b`
- Worktree: isolated detached worktree tại đúng commit `a8cd498`
- Test evidence:
  - Logic RMS compile/run thủ công từ commit, không kéo file ngoài history — PASS.
  - Cùng bộ test với ASan/UBSan — PASS.
  - Script `Core/Tools/test-rms-host.sh` trực tiếp — FAIL trước compile vì dependency file không tồn tại trong commit.

#### R07-20260802-06 — Script test handoff phụ thuộc file chưa có trong commit

- Severity: `HIGH`
- State: `RESOLVED`
- Files: `Core/Tools/test-rms-host.sh`.
- Evidence: tại worktree `a8cd498`, script từng phụ thuộc helper và filesystem source chưa có trong history.
- Required action: làm script tự chứa hoặc dependency-conditional.
- Resolution evidence: script có fallback isolated-root/sanitizer/timeout và chỉ thêm `SandboxResolver.cpp` khi file tồn tại; normal + ASan/UBSan PASS.

#### R07-20260802-07 — Cleanup tombstone sau delete nuốt lỗi

- Severity: `MEDIUM`
- State: `RESOLVED`
- Files: `Core/src/runtime/RecordStoreRegistry.cpp`.
- Evidence: tombstone post-commit từng có thể tồn tại vĩnh viễn nếu unlink thất bại.
- Required action: startup scavenger/retry và test stale tombstone.
- Resolution evidence: `configure()` chạy `scavenge_delete_tombstones_unlocked()`, chỉ nhận diện RMS tombstone hợp lệ, unlink và fsync directory; test xác nhận dọn tombstone nhưng giữ file không liên quan.

### Review checkpoint 2026-08-02 20:49 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: `a8cd498` cộng follow-up RMS scavenger/test-script fix trong shared checkout.
- Test evidence: normal và ASan/UBSan `Core/Tools/test-rms-host.sh` đều PASS.
- Remaining process action: tạo commit sạch chứa follow-up files và cập nhật Handoff SHA.

---

<a id="review-08"></a>
# Review 08 — Filesystem sandbox, resource loading và JSR-75 FileConnection

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 08](PARALLEL_SUBAGENT_WORKPLAN.md#08-filesystem-sandbox-resource-loading-và-jsr-75-fileconnection)

## Initial audit findings

### R08-20260802-01 — String path normalization không đủ chống symlink escape

- Severity: `BLOCKER`
- State: `RESOLVED`
- Required action: root-fd based traversal, `openat`/equivalent, no-follow policy, race-safe create/rename/delete.
- Resolution evidence: `SandboxResolver` giữ root directory FD, đi từng component bằng `openat`/`fstatat`, dùng `O_NOFOLLOW`, `AT_SYMLINK_NOFOLLOW` và `renameatx_np(..., RENAME_EXCL)` trên Apple; security/race tests pass.

### R08-20260802-02 — File permission gate chưa nối

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: read/write/delete/list phải đi qua mục 06 với resource URI canonical.
- Resolution evidence: `FileNatives.cpp` gọi `PermissionPolicy::require()` cho read/write tại connection open và operation/stream open; VM tests xác nhận deny và recheck bằng resource `file:///...` canonical.

### R08-20260802-03 — JSR-75 semantics cần matrix rõ

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: URI roots, directory suffix, listing filters, hidden/readable/writable, truncate/rename/open stream state và exception mapping.
- Resolution evidence: `FileOps.java`, `FileSystemSecurityTests.cpp` và `FileSystemVmTests.cpp` bao phủ root URL, listing/filter/hidden, modes, stream ownership, rename/delete/truncate, Unicode resource, symlink/race, large offset và temporary cleanup.

## Review log

### Review checkpoint 2026-08-02 19:43 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: shared checkout, chưa có commit/stage riêng
- Test evidence:
  - `PHONEME_FILESYSTEM_TEST_ROOT=... bash Core/Tools/test-filesystem-host.sh` — PASS.
  - `PHONEME_SANITIZE=1 PHONEME_FILESYSTEM_TEST_ROOT=... bash Core/Tools/test-filesystem-host.sh` — ASan/UBSan PASS.

#### R08-20260802-04 — Không có checkpoint commit có thể tái tạo

- Severity: `HIGH`
- State: `OPEN`
- Files: toàn bộ ownership mục 08, gồm file mới `SandboxResolver.*`, test và script.
- Evidence: hơn 1.000 dòng implementation đang nằm uncommitted trong shared checkout; handoff ghi rõ chưa commit/stage. Mục 20 không thể checkout, diff hoặc bisect chính xác phần mục 08 mà không kéo thay đổi agent khác.
- Required action: tạo commit sạch chỉ chứa ownership mục 08, cập nhật handoff bằng SHA và chứng minh test chạy từ đúng commit đó.

#### R08-20260802-05 — `atomic_write` không biểu diễn trạng thái commit không chắc chắn

- Severity: `MEDIUM`
- State: `RESOLVED`
- Files: `Core/src/filesystem/SandboxResolver.cpp`.
- Evidence: checkpoint cũ có thể thay destination rồi trả lỗi directory-sync mà không rollback.
- Required action: dùng transaction/backup có rollback và fault-injection test sau rename.
- Resolution evidence: atomic write giữ hard-link backup, sync backup directory, rollback replacement/new creation nếu install sync lỗi và fsync rollback; tests inject `atomic_install_sync` cho cả existing/absent destination. Normal + ASan/UBSan PASS.

#### R08-20260802-06 — Background-safe I/O chưa được chứng minh ở Runtime boundary

- Severity: `MEDIUM`
- State: `OPEN`
- Files: `Core/src/vm/FileNatives.cpp`, Runtime execution path thuộc mục 02.
- Evidence: filesystem native vẫn thực hiện POSIX I/O đồng bộ. Module không giữ mutex nội bộ trong syscall dài, nhưng Java native có thể vẫn được gọi trong lúc `Runtime::mutex_` đang giữ theo execution model hiện tại.
- Required action: ghi rõ dependency mục 02 và bổ sung integration test chứng minh blocking file I/O không chặn input/lifecycle của MIDlet khác. Không sửa Runtime ngoài ownership mục 08.

### Review checkpoint 2026-08-02 20:50 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: shared checkout follow-up atomic transaction.
- Test evidence: `Core/Tools/test-filesystem-host.sh` normal và ASan/UBSan đều PASS.
- Remaining findings: `R08-20260802-04` cần commit sạch; `R08-20260802-06` là integration dependency với mục 02.

---

<a id="review-09"></a>
# Review 09 — Network/GCF asynchronous completion, socket/TLS và cancellation

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 09](PARALLEL_SUBAGENT_WORKPLAN.md#09-networkgcf-asynchronous-completion-sockettls-và-cancellation)

## Initial audit findings

### R09-20260802-01 — Adapter tên async nhưng nhiều operation vẫn block

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: operation token + scheduler suspension/resume; không block Runtime mutex hoặc main thread.

### R09-20260802-02 — HTTPS bridge dùng semaphore wait

- Severity: `HIGH`
- State: `OPEN`
- Required action: chuyển sang callback completion/cancellation an toàn; late callback sau close/destroy phải bị drop theo generation.

### R09-20260802-03 — Security gate và ownership cleanup chưa được chứng minh end-to-end

- Severity: `HIGH`
- State: `OPEN`
- Required action: permission before DNS/connect/request; close all per MIDlet; stale handles không truy cập connection mới.

### R09-20260802-04 — Fake adapter không thay thế real-device test

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: device matrix cho HTTP redirect/chunked/TLS cert/timeouts/socket EOF/UDP.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-10"></a>
# Review 10 — Push Registry end-to-end Core

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 10](PARALLEL_SUBAGENT_WORKPLAN.md#10-push-registry-end-to-end-core)

## Initial audit findings

### R10-20260802-01 — Registry engine chưa có source listener/launch orchestration

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: nối network availability/alarm scheduler → eligible request → AMS launch/resume → ack/retry.

### R10-20260802-02 — Filter matching cần đúng protocol semantics

- Severity: `HIGH`
- State: `OPEN`
- Required action: không chỉ lưu raw filter; test source address/filter matching và malformed filter rejection.

### R10-20260802-03 — iOS background policy phải explicit

- Severity: `HIGH`
- State: `OPEN`
- Required action: không giả định listener chạy vô hạn; queue/persist event và chỉ launch khi host cấp execution window.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-11"></a>
# Review 11 — Graphics/Image/Font pixel accuracy và performance

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 11](PARALLEL_SUBAGENT_WORKPLAN.md#11-graphicsimagefont-pixel-accuracy-và-performance)

## Initial audit findings

### R11-20260802-01 — Cần golden-image tests cho primitive và transform

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: deterministic expected RGBA cho anchors, clipping, translation, drawRegion transforms, alpha, arcs, dotted stroke và overlap copy.
- Resolution evidence: commit `935db5b` thêm deterministic pixel assertions cho anchors/transforms, primitive edge rules, clipping, alpha và self-overlap; commit `ee56ae0` sửa text anchor rule.

### R11-20260802-02 — PNG support chưa được chứng minh đầy đủ

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: corpus color type/bit depth/transparency/interlace/corrupt chunks và size limits.
- Resolution evidence: decoder/test bao phủ grayscale, palette+tRNS, truecolor+tRNS, gray-alpha 16-bit, Adam7, CRC, order, truncation, mutation corpus và allocation limits.

### R11-20260802-03 — Text metrics có thể lệch thiết bị J2ME

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: phân biệt API metric correctness với visual font choice; test Vietnamese/Japanese và baseline/anchor consistency.
- Resolution evidence: platform measure/raster consistency, baseline/height, clipping allocation và Unicode Vietnamese/Japanese/fallback glyph tests pass. Visual parity với từng handset vẫn thuộc corpus/device review, không phải defect standalone của checkpoint này.

### R11-20260802-04 — Tránh full-frame/full-image copy không cần thiết

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: benchmark allocation/copy per frame; dirty region hoặc shared immutable storage với ownership rõ.
- Resolution evidence: `GraphicsStore::consume_dirty_update()` xuất đúng packed pixels của dirty rectangle và clear generation sau consume; module tests xác nhận full initial update, bounded update và no-op consume. C API/Swift partial upload tiếp tục thuộc mục 16/19.

## Review log

### Review checkpoint 2026-08-02 19:29 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed commits: `935db5b`, `ee56ae0`
- Additional dirty checkpoint: uncommitted changes trong Graphics VM/native files trên `main`
- Test evidence:
  - isolated Graphics module test — pass, benchmark `10,000` draws khoảng `18 ms` normal.
  - isolated Graphics VM test — pass trên current dirty snapshot.
  - sanitizer Graphics module test — pass, benchmark khoảng `59 ms` dưới ASan/UBSan.

#### R11-20260802-05 — Performance assertion chưa kiểm tra mục tiêu 60 FPS

- Severity: `MEDIUM`
- State: `RESOLVED`
- Files: `Core/Tests/GraphicsModuleTests.cpp`
- Evidence: benchmark cũ chỉ có ceiling 2 giây và không đại diện frame budget.
- Required action: report baseline ổn định và tách host regression khỏi device gate.
- Resolution evidence: benchmark chạy 5 mẫu, báo median/slowest/calls-per-second, tách `core_only_60fps_budget` khỏi host regression ceiling và ghi rõ device 60 FPS gate thuộc mục 19.

#### R11-20260802-06 — Handoff không phải checkpoint sạch

- Severity: `HIGH`
- State: `RESOLVED`
- Files: production Graphics/Image/Font source và VM semantics.
- Evidence: source follow-up đã được gom vào commit `153e88e`; isolated module normal/sanitizer PASS. VM normal/sanitizer PASS sau khi harness thêm Scheduler/IO dependency stubs.
- Required action: giữ source checkpoint riêng và commit test harness follow-up.

#### R11-20260802-07 — Follow-up test harness chưa có commit sạch

- Severity: `MEDIUM`
- State: `OPEN`
- Files: `Core/Tests/GraphicsVmTests.cpp`, `Core/Tools/test-graphics-vm-host.sh`.
- Evidence: test isolation fixes cho I/O connection symbols và Scheduler source đang ở shared checkout, chưa có SHA handoff.
- Required action: commit riêng test harness follow-up; không cần thay đổi production Graphics source.

### Review checkpoint 2026-08-02 20:51 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed source commit: `153e88e56d2515bd46778cfe69c76a4f5f36aa01`
- Test evidence: isolated module và VM, normal + ASan/UBSan đều PASS sau harness dependency fix.

---

<a id="review-12"></a>
# Review 12 — Canvas/GameCanvas/Input và event scheduling

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 12](PARALLEL_SUBAGENT_WORKPLAN.md#12-canvasgamecanvasinput-và-event-scheduling)

## Initial audit findings

### R12-20260802-01 — Callback hiện chạy đồng bộ từ host input

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: enqueue vào Java event execution context; preserve order, coalescing và app generation; không invoke Java dưới Runtime lock.

### R12-20260802-02 — Key repeat cần timer/scheduler semantics

- Severity: `HIGH`
- State: `OPEN`
- Required action: không xem repeated host press là toàn bộ repeat implementation; capability/reporting phải đúng profile.

### R12-20260802-03 — Paused/background Canvas visibility/input cần test

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: show/hide/sizeChanged/repaint ordering khi switch nhiều MIDlet và suspend/resume.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-13"></a>
# Review 13 — LCDUI extended widgets và callback hai chiều

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 13](PARALLEL_SUBAGENT_WORKPLAN.md#13-lcdui-extended-widgets-và-callback-hai-chiều)

## Initial audit findings

### R13-20260802-01 — Nhiều MIDP widget cốt lõi còn thiếu

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: Alert/AlertType, TextBox, DateField, ImageItem, Spacer, CustomItem, Ticker, ItemStateListener với class layout và Java callbacks đúng.
- Resolution evidence: builtin classes/natives và fixture bao phủ toàn bộ widget trên, Alert timeout/dismiss, image generations, date modes/timezone, ticker update và CustomItem render/input.

### R13-20260802-02 — Reverse action hiện có no-op/incomplete branches

- Severity: `HIGH`
- State: `RESOLVED`
- Evidence: checkpoint cũ thiếu scroll/date/focus/activate callback semantics.
- Required action: mỗi host action phải mutate Java state và dispatch listener đúng.
- Resolution evidence: `handle_lcdui_action()` triển khai text/choice/gauge/date/scroll/focus/activate/custom-key; VM fixture xác nhận ItemStateListener, ItemCommandListener, CommandListener và Java state round-trip.

### R13-20260802-03 — Native UI state phải namespaced theo app/generation

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: stale event từ màn hình cũ/app cũ không được tác động Java object mới.
- Resolution evidence: component registry theo từng `Machine`, IDs namespaced theo app; removed item/command IDs bị reject. iOS host đổi AppId thành monotonic trong vòng đời RuntimeContext, không quay vòng 1…64 nên namespace app cũ không được tái sử dụng.

#### R13-20260802-04 — Handoff chưa có commit sạch

- Severity: `HIGH`
- State: `OPEN`
- Files: LCDUI extended source/tests và `phoneME/Runtime/EmbeddedPhoneMEEngine.swift` follow-up.
- Evidence: implementation và host generation fix hiện ở shared checkout, chưa có SHA tái tạo độc lập.
- Required action: commit theo ownership, cập nhật Handoff và chạy lại standalone + app build từ checkpoint sạch.

## Review log

### Review checkpoint 2026-08-02 20:53 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: shared checkout.
- Test evidence: LCDUI extended normal và ASan/UBSan đều PASS; stale command/item actions bị reject.

---

<a id="review-14"></a>
# Review 14 — MIDP Game API: Layer, Sprite, TiledLayer, LayerManager

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 14](PARALLEL_SUBAGENT_WORKPLAN.md#14-midp-game-api-layer-sprite-tiledlayer-layermanager)

## Initial audit findings

### R14-20260802-01 — API phải bám MIDP Game API collision/transform semantics

- Severity: `HIGH`
- State: `OPEN`
- Required action: frame sequence, reference pixel, transform constants, collision rectangle/pixel-level collision và visibility ordering.

### R14-20260802-02 — Không nhân đôi renderer riêng

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: dùng Graphics/Image contract mục 11; tránh fork transform/alpha/clipping logic.

### R14-20260802-03 — Cần stress LayerManager/TiledLayer lớn

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: test tile mutation, animated tiles, viewport clipping, z-order và allocation behavior.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-15"></a>
# Review 15 — MMAPI media hardening và optional capture/video surface

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 15](PARALLEL_SUBAGENT_WORKPLAN.md#15-mmapi-media-hardening-và-optional-capturevideo-surface)

## Initial audit findings

### R15-20260802-01 — Player events cần async Java delivery an toàn

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: adapter callback → scheduler event; late callback after close/destroy dropped; listener exception isolated.

### R15-20260802-02 — Host fallback không chứng minh AVFoundation matrix

- Severity: `HIGH`
- State: `OPEN`
- Required action: device tests cho local/remote audio, codec, seek, loop, interruption, route change, background/resume và errors.

### R15-20260802-03 — Permission gate cho capture/record và remote resources

- Severity: `HIGH`
- State: `OPEN`
- Required action: integrate mục 06; không expose RecordControl/VideoControl success nếu platform/policy chưa hỗ trợ.

### R15-20260802-04 — Optional API phải corpus-driven

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: ưu tiên Player/Volume/Tone correctness; capture/video chỉ mở khi có capability và test thực tế.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-16"></a>
# Review 16 — C ABI và iOS Swift/Objective-C host integration

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 16](PARALLEL_SUBAGENT_WORKPLAN.md#16-c-abi-và-ios-swiftobjective-c-host-integration)

## Initial audit findings

### R16-20260802-01 — Swift chưa nối đầy đủ security/push APIs

- Severity: `HIGH`
- State: `OPEN`
- Required action: permission prompt/trust configuration, push poll/ack/background policy và lifecycle routing sau khi Core APIs ổn định.

### R16-20260802-02 — Không block main thread bằng semaphore/native call

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: mọi HTTPS/media/prompt operation có khả năng chờ phải async; verify queue confinement.

### R16-20260802-03 — Comment về worker pthread không khớp Core hiện tại

- Severity: `LOW`
- State: `OPEN`
- Required action: cập nhật documentation/comment theo execution model thật sau mục 01/02.

### R16-20260802-04 — Late callback và runtime lifetime

- Severity: `HIGH`
- State: `OPEN`
- Required action: runtime generation/token; callback sau destroy không dereference stale ref hoặc deliver sang session mới.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-17"></a>
# Review 17 — Real-game compatibility corpus và differential testing

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 17](PARALLEL_SUBAGENT_WORKPLAN.md#17-real-game-compatibility-corpus-và-differential-testing)

## Initial audit findings

### R17-20260802-01 — Corpus cần metadata và legal provenance

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: không commit commercial JAR không có quyền; lưu hash, source/provenance, device profile, expected behavior và minimized fixtures.
- Resolution evidence: manifest ghi license/provenance/device profile; fixture là source project-authored, local commercial placeholders bị disable và không commit JAR.

### R17-20260802-02 — Failure taxonomy phải tách rõ

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: install/class-load/verifier/thread/LCDUI/graphics/network/media/RMS/performance; không gom mọi lỗi thành “game không chạy”.
- Resolution evidence: analyzer có taxonomy riêng cho install, class loading/linkage, verifier, thread, LCDUI, graphics, network, media, RMS, performance và milestone.

### R17-20260802-03 — Differential behavior cần reproducible evidence

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: capture logs/screenshots/hash/frame/input sequence và reference runtime/version; tránh kết luận bằng quan sát thủ công không lưu lại.
- Resolution evidence: harness ghi JSON result, logs, PPM frame/hash, milestones, input sequence và metadata reference runner/runtime/version.

## Review log

### Review checkpoint 2026-08-02 19:43 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed commit: `2b8ce42101c027f40916595879dbbb7a04a78467`
- Worktree: isolated detached worktree tại đúng commit handoff
- Test evidence:
  - `python3 Core/Tests/Compatibility/test_analyzer.py` — 7/7 PASS.
  - `bash Core/Compatibility/run-corpus.sh --no-coverage-doc` — FAIL khi compile fixture.
  - `bash Core/Compatibility/run-corpus.sh --static-only --include-disabled --no-coverage-doc` — cùng lỗi compile.

#### R17-20260802-04 — Commit handoff không tự chứa dependency để build corpus

- Severity: `BLOCKER`
- State: `RESOLVED`
- Files: `Core/Compatibility/expected-results.json`, fixture build logic và test stubs.
- Evidence: mọi enabled item dùng cùng build source list có `GameApiMIDlet.java`; commit không chứa stubs `javax.microedition.lcdui.game.LayerManager`, `Sprite`, `TiledLayer`, nên `javac` lỗi 9 symbol ngay cả khi `fixture-game-api` đang disabled. Kết quả pass trước đó phụ thuộc thay đổi chưa tích hợp của mục 14 trong shared checkout.
- Required action: làm checkpoint tự build bằng stubs fixture-local hoặc chỉ compile source cần thiết.
- Resolution evidence: thêm Compatibility-owned `Layer/LayerManager/Sprite/TiledLayer` compile stubs; analyzer merge shared+local stubs theo relative path để local override. Full enabled corpus build/run 3/3 PASS.

#### R17-20260802-05 — Corpus enabled chưa đại diện cho mục tiêu real-game compatibility

- Severity: `HIGH`
- State: `OPEN`
- Files: `Core/Compatibility/expected-results.json`, `Core/Compatibility/CORPUS.md`.
- Evidence: chỉ ba fixture project-authored Canvas/LCDUI/RMS được enable. Threaded GameCanvas, Game API, network, media, obfuscated/preverified và JAR thật đều disabled hoặc placeholder; do đó `3/3 PASS` không chứng minh compatibility game thật.
- Required action: giữ harness có thể tích hợp như hạ tầng, nhưng không đánh dấu mục 17 hoàn thành; thêm ít nhất một corpus hợp pháp cho từng miền critical khi subsystem tương ứng sẵn sàng và báo coverage theo enabled/disabled rõ ràng.

#### R17-20260802-06 — Golden frame regression chưa được kích hoạt

- Severity: `MEDIUM`
- State: `RESOLVED`
- Files: `Core/Compatibility/expected-results.json`.
- Evidence: fixture Canvas yêu cầu `min_frames: 1` nhưng `frame_hashes` rỗng; một frame sai nội dung vẫn có thể PASS nếu được tạo.
- Required action: chốt hash hoặc pixel assertions cho scene deterministic; lưu cách regenerate có kiểm soát và không tự cập nhật golden trong normal run.
- Resolution evidence: hash PPM ổn định qua hai lần chạy là `8053e3a6e4bc19d5c029eaac6b2e8dd7c6ca9f4198012eaeca7175a733e1ee53`; manifest yêu cầu đúng hash trong normal run.

### Review checkpoint 2026-08-02 20:54 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Reviewed checkpoint: `2b8ce42` cộng local-stub/golden follow-up trong shared checkout.
- Test evidence: analyzer 7/7 PASS; enabled corpus Canvas/LCDUI/RMS 3/3 PASS; Canvas golden repeat ổn định.
- Remaining finding: `R17-20260802-05`; hạ tầng đã dùng được nhưng chưa đủ corpus game thật để tuyên bố compatibility hoàn tất.

---

<a id="review-18"></a>
# Review 18 — Test infrastructure, isolated build roots, sanitizer và CI

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 18](PARALLEL_SUBAGENT_WORKPLAN.md#18-test-infrastructure-isolated-build-roots-sanitizer-và-ci)

## Initial audit findings

### R18-20260802-01 — Default test roots gây race giữa subagent

- Severity: `BLOCKER`
- State: `RESOLVED`
- Required action: mọi script nhận unique root; không xóa shared directory.
- Resolution evidence: `common-test-root.sh` tạo marker-protected unique roots, sanitize task IDs, safe cleanup và timeout process-tree; two-host concurrent run đều PASS độc lập.

### R18-20260802-02 — Full suite chưa chạy một số advanced fixture

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: inventory method-level fixture coverage và negative paths.
- Resolution evidence: `test-coverage-inventory.sh` báo TAP 21/21, xác nhận toàn bộ RMS advanced methods/negative paths được compile và invoke; host matrix gồm RMS module.

### R18-20260802-03 — Platform adapter tests cần phân tầng

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: report riêng unit/host/iPhoneOS/Xcode/device.
- Resolution evidence: tooling tách host matrix, ASan/UBSan, optional TSan/fuzz, iPhoneOS archive verify và Xcode Debug/Release. Device runtime vẫn là gate mục 19, không bị gộp thành host pass.

### R18-20260802-04 — CI không được commit build products

- Severity: `LOW`
- State: `RESOLVED`
- Required action: isolated derived data, cleanup và git cleanliness.
- Resolution evidence: mọi matrix dùng isolated report/DerivedData roots; `.gitignore` và tooling self-test kiểm tra path/symlink safety, failure status và timeout cleanup.

#### R18-20260802-05 — Tooling checkpoint chưa có commit sạch

- Severity: `HIGH`
- State: `OPEN`
- Files: `Core/Tools/`, `Core/Tests/.gitignore`.
- Evidence: helper/matrix/fuzz/tooling files vẫn uncommitted trong shared checkout, nên CI không thể checkout một SHA duy nhất.
- Required action: commit toàn bộ ownership mục 18 và cập nhật Handoff; chạy `test-tooling`, concurrent hosts, host matrix và sanitizer matrix từ worktree sạch.

## Review log

### Review checkpoint 2026-08-02 21:34 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Test evidence:
  - tooling self-tests PASS;
  - concurrent host-a/host-b logs PASS;
  - host matrix 15/15 PASS;
  - ASan/UBSan matrix 15/15 PASS;
  - coverage inventory 21/21 PASS;
  - libFuzzer unavailable nên optional fuzz targets SKIP đúng thiết kế.

---

<a id="review-19"></a>
# Review 19 — Performance, multi-MIDlet soak, RAM/CPU/battery và device validation

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 19](PARALLEL_SUBAGENT_WORKPLAN.md#19-performance-multi-midlet-soak-ramcpubattery-và-device-validation)

## Initial audit findings

### R19-20260802-01 — Cần baseline và budget định lượng

- Severity: `HIGH`
- State: `OPEN`
- Required action: idle/active/background CPU, memory per MIDlet, frame pacing, wakeups, socket/media cases, thermal state và test duration.

### R19-20260802-02 — Phải profile trước khi tối ưu

- Severity: `MEDIUM`
- State: `OPEN`
- Required action: stack evidence/Instruments trace; không giảm correctness hoặc thêm polling workaround chỉ để hạ số đo cục bộ.

### R19-20260802-03 — Destroy-all memory recovery cần kiểm tra

- Severity: `HIGH`
- State: `OPEN`
- Required action: repeated create/start/switch/destroy; verify Java heap, native registries, image/media/network resources và Swift frame buffers release.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-20"></a>
# Review 20 — Integration owner

- Current review status: `CHANGES_REQUESTED`
- Plan item: [Mục 20](PARALLEL_SUBAGENT_WORKPLAN.md#20-integration-owner-registry-cmake-c-api-xcode-project-và-full-regression)

## Initial audit findings

### R20-20260802-01 — Integration phải kiểm tra review findings trước merge

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: không merge mục còn `BLOCKER OPEN`; ghi checkpoint commit và resolution evidence.
- Current evidence: các blocker chức năng của 01/05/07/08/11/13/17 đã được xử lý, nhưng nhiều follow-up chưa có commit sạch và mục 17 còn thiếu corpus game thật.

### R20-20260802-02 — Central files không được chứa implementation module

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: central files chỉ registration/build/C ABI composition.
- Resolution evidence: central changes đăng ký registrar/source/C API và test composition; implementation subsystem vẫn nằm file module. Builtin registry test pass, không duplicate registration.

### R20-20260802-03 — Full regression phải dùng isolated roots

- Severity: `HIGH`
- State: `RESOLVED`
- Required action: tránh shared default roots và ghi exact results.
- Resolution evidence: host matrix, sanitizer matrix, iPhoneOS archive và Xcode DerivedData đều dùng marker-protected isolated roots; concurrent host-a/host-b pass.

### R20-20260802-04 — Build success chưa đủ, cần symbol/source inventory

- Severity: `MEDIUM`
- State: `RESOLVED`
- Required action: source/object inventory, required symbols, vendor/import scan.
- Resolution evidence: iPhoneOS verify PASS với 79 implementation files/79 archive members, unique inventory, C API version symbol, không forbidden vendor symbol, không imported source reference và không pointer-to-32-bit cast.

#### R20-20260802-05 — Integration checkpoint chưa có commit sạch

- Severity: `HIGH`
- State: `OPEN`
- Files: central ownership files và module follow-up patches.
- Evidence: shared checkout chứa nhiều `MM/M/??`; kết quả full matrix áp dụng cho snapshot hiện tại nhưng không thể checkout bằng một SHA.
- Required action: sau khi module agents commit handoff, integration owner tạo commit composition sạch, chạy lại host+sanitizer+iPhoneOS+Xcode và ghi SHA.

## Review log

### Review checkpoint 2026-08-02 21:35 +07:00

- Review status: `CHANGES_REQUESTED`
- Reviewer: `GPT-5.6 Thinking`
- Test evidence:
  - host module matrix 15/15 PASS;
  - ASan/UBSan matrix 15/15 PASS;
  - iPhoneOS arm64/iOS 16 archive build + verify PASS;
  - Xcode Debug và Release generic iphoneos no-sign PASS;
  - C11 C API header test PASS.

---

<a id="review-21"></a>
# Review 21 — Optional APIs theo corpus

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 21](PARALLEL_SUBAGENT_WORKPLAN.md#21-optional-apis-theo-corpus-wireless-messaging-bluetooth-location-sip-pim)

## Initial audit findings

### R21-20260802-01 — Không mở JSR nếu chưa có corpus evidence

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: mỗi JSR cần JAR/hash/API usage list/platform capability/security model và acceptance test trước khi claim implementation.

### R21-20260802-02 — Mỗi JSR phải là module riêng

- Severity: `HIGH`
- State: `OPEN`
- Required action: không giao toàn bộ Wireless/Bluetooth/Location/SIP/PIM cho một implementation blob hoặc central native file.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

<a id="review-22"></a>
# Review 22 — Compatibility layer cho game cũ/không chuẩn

- Current review status: `NOT_REVIEWED`
- Plan item: [Mục 22](PARALLEL_SUBAGENT_WORKPLAN.md#22-optional-compatibility-layer-cho-game-j2me-cũkhông-chuẩn)

## Initial audit findings

### R22-20260802-01 — Compatibility profile phải strict-by-default

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: quirk chỉ bật theo deterministic suite identity/version; không nới verifier hoặc API toàn cục.

### R22-20260802-02 — Mọi quirk cần minimized fixture và audit log

- Severity: `HIGH`
- State: `OPEN`
- Required action: document evidence, affected hash/profile, transformation/result checksum và regression không ảnh hưởng suite khác.

### R22-20260802-03 — Không dùng compatibility layer che core bug

- Severity: `BLOCKER`
- State: `OPEN`
- Required action: memory corruption, parser/verifier defect, race hoặc incorrect standard semantics phải sửa ở module gốc.

## Review log

_Chưa có checkpoint implementation được gửi review._

---

# Review summary dashboard

| Mục | Review status | Blocker mở | Checkpoint gần nhất |
|---:|---|:---:|---|
| 01 | CHANGES_REQUESTED | Không | scheduler shutdown follow-up — 2026-08-02 21:33 |
| 02 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 03 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 04 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 05 | CHANGES_REQUESTED | Không | `6548b5f` isolated review — 2026-08-02 20:48 |
| 06 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 07 | CHANGES_REQUESTED | Không | RMS scavenger follow-up — 2026-08-02 20:49 |
| 08 | CHANGES_REQUESTED | Không | atomic rollback follow-up — 2026-08-02 20:50 |
| 09 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 10 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 11 | CHANGES_REQUESTED | Không | `153e88e` + harness fix — 2026-08-02 20:51 |
| 12 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 13 | CHANGES_REQUESTED | Không | shared LCDUI checkpoint — 2026-08-02 20:53 |
| 14 | NOT_REVIEWED | Không | Initial audit 2026-08-02 |
| 15 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 16 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 17 | CHANGES_REQUESTED | Không | local stubs + golden — 2026-08-02 20:54 |
| 18 | CHANGES_REQUESTED | Không | host/sanitizer matrix — 2026-08-02 21:34 |
| 19 | NOT_REVIEWED | Không | Initial audit 2026-08-02 |
| 20 | CHANGES_REQUESTED | Có | full integration snapshot — 2026-08-02 21:35 |
| 21 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |
| 22 | NOT_REVIEWED | Có | Initial audit 2026-08-02 |

Dashboard chỉ là tóm tắt. Quyết định review chính thức nằm trong section từng mục và finding state tương ứng.
