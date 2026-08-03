#!/usr/bin/env python3
"""Batch static/smoke test J2ME JARs and produce one fix-oriented report.

The tool deliberately runs every MIDlet in a separate CompatibilityHarness process
and runtime home so a crash, timeout, or corrupt suite cannot poison later tests.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import dataclasses
import datetime as dt
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import textwrap
import threading
import time
import zipfile
from typing import Any, Iterable, Mapping, Sequence

SCRIPT_PATH = pathlib.Path(__file__).resolve()
CORE_ROOT = SCRIPT_PATH.parent.parent
PROJECT_ROOT = CORE_ROOT.parent
DEFAULT_JAR_DIR = PROJECT_ROOT / "jar_test"
DEFAULT_TIMEOUT_MS = 2_500
DEFAULT_WIDTH = 320
DEFAULT_HEIGHT = 240
MAX_ERROR_TEXT = 700

FAILURE_KIND_ALIASES = {
    "native_crash": "native crash",
    "missing_class": "missing class",
    "missing_method": "missing method",
    "missing_native": "missing native",
    "verifier": "verifier/ClassFormat/StackMap failure",
    "install": "install/manifest/suite failure",
    "thread": "thread/scheduler/monitor failure",
    "lcdui": "LCDUI callback/bridge failure",
    "graphics": "graphics/framebuffer/render failure",
    "rms": "RMS/persistence failure",
    "performance": "performance/memory failure",
    "uncaught_exception": "uncaught Java exception",
    "network": "network failure",
    "media": "media failure",
    "timeout": "timeout",
}

PRIORITY_ORDER = {
    "native crash": 0,
    "asan/ubsan/native crash": 0,
    "timeout": 1,
    "verifier/ClassFormat/StackMap failure": 2,
    "bytecode runtime semantics": 3,
    "missing class": 4,
    "missing method": 5,
    "missing native": 6,
    "missing field": 7,
    "uncaught Java exception": 8,
    "thread/scheduler/monitor failure": 9,
    "LCDUI callback/bridge failure": 10,
    "graphics/framebuffer/render failure": 11,
    "RMS/persistence failure": 12,
    "network failure": 13,
    "media failure": 14,
    "performance/memory failure": 15,
    "install/manifest/suite failure": 16,
    "metadata": 17,
    "static scan": 18,
    "runtime failure": 19,
}

API_PREFIXES = (
    "java/",
    "javax/",
    "com/nokia/",
    "com/siemens/",
    "com/samsung/",
    "com/motorola/",
    "com/sony",
    "com/mascotcapsule/",
    "com/vodafone/",
    "com/sprintpcs/",
    "com/sun/midp/",
    "org/microemu/",
)

_PROGRESS_LOCK = threading.Lock()
_PROGRESS_DONE = 0
_PROGRESS_TOTAL = 0
_PROGRESS_STARTED = 0.0


@dataclasses.dataclass(frozen=True)
class MidletEntry:
    name: str
    class_name: str
    source: str


@dataclasses.dataclass(frozen=True)
class JarTarget:
    item_id: str
    jar_path: pathlib.Path
    relative_path: str
    midlet_name: str
    main_class: str
    main_source: str


@dataclasses.dataclass(frozen=True)
class DiscoveryIssue:
    jar_path: pathlib.Path
    relative_path: str
    kind: str
    detail: str


@dataclasses.dataclass
class TargetResult:
    target: JarTarget
    status: str
    duration_ms: int = 0
    static_error: str = ""
    launch_error: str = ""
    failures: list[dict[str, str]] = dataclasses.field(default_factory=list)
    observed: dict[str, Any] = dataclasses.field(default_factory=dict)
    referenced_classes: dict[str, int] = dataclasses.field(default_factory=dict)
    referenced_methods: dict[str, int] = dataclasses.field(default_factory=dict)
    referenced_fields: dict[str, int] = dataclasses.field(default_factory=dict)
    run_dir: str = ""
    stdout_log: str = ""
    stderr_log: str = ""
    result_json: str = ""


def load_compat_module() -> Any:
    analyzer_path = CORE_ROOT / "Compatibility" / "analyze-failures.py"
    spec = importlib.util.spec_from_file_location("phoneme_compat_analyzer", analyzer_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load compatibility analyzer: {analyzer_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def utc_timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def json_write(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def markdown_escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def compact_text(value: str, limit: int = MAX_ERROR_TEXT) -> str:
    compact = re.sub(r"\s+", " ", value).strip()
    if len(compact) <= limit:
        return compact
    return compact[: max(0, limit - 3)] + "..."


def stable_id(relative_path: str, main_class: str) -> str:
    stem = pathlib.Path(relative_path).stem.casefold()
    slug = re.sub(r"[^a-z0-9]+", "-", stem).strip("-") or "jar"
    digest = hashlib.sha256(f"{relative_path}\0{main_class}".encode("utf-8")).hexdigest()[:10]
    return f"{slug[:52]}-{digest}"


def decode_manifest(raw: bytes) -> str:
    for encoding in ("utf-8-sig", "cp1252", "latin-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", errors="replace")


def parse_manifest_text(text: str) -> dict[str, str]:
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    logical_lines: list[str] = []
    for line in normalized.split("\n"):
        if line.startswith(" ") and logical_lines:
            logical_lines[-1] += line[1:]
        else:
            logical_lines.append(line)

    attributes: dict[str, str] = {}
    for line in logical_lines:
        if not line or ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        if not key:
            continue
        attributes[key.casefold()] = value.lstrip()
    return attributes


def midlets_from_attributes(attributes: Mapping[str, str], source: str) -> list[MidletEntry]:
    indexed: list[tuple[int, MidletEntry]] = []
    for key, raw_value in attributes.items():
        match = re.fullmatch(r"midlet-(\d+)", key.casefold())
        if not match:
            continue
        parts = [part.strip() for part in raw_value.split(",")]
        if not parts:
            continue
        class_name = parts[-1].replace("/", ".").strip()
        if not class_name:
            continue
        display_name = parts[0] if len(parts) >= 2 and parts[0] else class_name
        indexed.append((int(match.group(1)), MidletEntry(display_name, class_name, source)))
    indexed.sort(key=lambda item: item[0])
    if indexed:
        return [entry for _, entry in indexed]

    for key in ("midlet-class", "main-class"):
        class_name = attributes.get(key, "").replace("/", ".").strip()
        if class_name:
            return [MidletEntry(class_name.rsplit(".", 1)[-1], class_name, source)]
    return []


def read_zip_manifest(archive: zipfile.ZipFile) -> dict[str, str]:
    manifest_name = next(
        (name for name in archive.namelist() if name.casefold() == "meta-inf/manifest.mf"),
        "",
    )
    if not manifest_name:
        return {}
    return parse_manifest_text(decode_manifest(archive.read(manifest_name)))


def read_jad_midlets(jar_path: pathlib.Path) -> list[MidletEntry]:
    candidates = [jar_path.with_suffix(".jad"), jar_path.with_suffix(".JAD")]
    for candidate in candidates:
        if not candidate.is_file():
            continue
        try:
            attributes = parse_manifest_text(decode_manifest(candidate.read_bytes()))
        except OSError:
            continue
        entries = midlets_from_attributes(attributes, f"JAD:{candidate.name}")
        if entries:
            return entries
    return []


def read_u1(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 1 > len(data):
        raise ValueError("truncated class file")
    return int(data[offset]), offset + 1


def read_u2(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 2 > len(data):
        raise ValueError("truncated class file")
    return struct.unpack_from(">H", data, offset)[0], offset + 2


def read_u4(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("truncated class file")
    return struct.unpack_from(">I", data, offset)[0], offset + 4


def class_and_super(raw: bytes) -> tuple[str, str]:
    data = memoryview(raw)
    if len(data) < 10 or data[:4].tobytes() != b"\xca\xfe\xba\xbe":
        raise ValueError("invalid class-file magic")
    offset = 8
    cp_count, offset = read_u2(data, offset)
    cp: list[Any] = [None] * cp_count
    index = 1
    while index < cp_count:
        tag, offset = read_u1(data, offset)
        if tag == 1:
            length, offset = read_u2(data, offset)
            if offset + length > len(data):
                raise ValueError("truncated UTF-8 constant")
            cp[index] = (tag, data[offset : offset + length].tobytes().decode("utf-8", "replace"))
            offset += length
        elif tag in (3, 4):
            _, offset = read_u4(data, offset)
        elif tag in (5, 6):
            _, offset = read_u4(data, offset)
            _, offset = read_u4(data, offset)
            index += 1
        elif tag in (7, 8, 16, 19, 20):
            value, offset = read_u2(data, offset)
            cp[index] = (tag, value)
        elif tag in (9, 10, 11, 12, 17, 18):
            first, offset = read_u2(data, offset)
            second, offset = read_u2(data, offset)
            cp[index] = (tag, first, second)
        elif tag == 15:
            kind, offset = read_u1(data, offset)
            reference, offset = read_u2(data, offset)
            cp[index] = (tag, kind, reference)
        else:
            raise ValueError(f"unsupported constant-pool tag {tag}")
        index += 1

    _, offset = read_u2(data, offset)  # access flags
    this_class, offset = read_u2(data, offset)
    super_class, offset = read_u2(data, offset)

    def utf8(cp_index: int) -> str:
        if cp_index <= 0 or cp_index >= len(cp):
            raise ValueError("invalid UTF-8 index")
        entry = cp[cp_index]
        if not entry or entry[0] != 1:
            raise ValueError("invalid UTF-8 entry")
        return str(entry[1])

    def class_name(cp_index: int) -> str:
        if cp_index == 0:
            return ""
        if cp_index <= 0 or cp_index >= len(cp):
            raise ValueError("invalid class index")
        entry = cp[cp_index]
        if not entry or entry[0] != 7:
            raise ValueError("invalid class entry")
        return utf8(int(entry[1]))

    return class_name(this_class), class_name(super_class)


def detect_midlet_classes(archive: zipfile.ZipFile) -> list[MidletEntry]:
    hierarchy: dict[str, str] = {}
    for name in archive.namelist():
        if not name.endswith(".class") or name.startswith("META-INF/versions/"):
            continue
        try:
            defined, parent = class_and_super(archive.read(name))
        except (KeyError, ValueError):
            continue
        hierarchy[defined] = parent

    base = "javax/microedition/midlet/MIDlet"

    def derives_from_midlet(class_name: str) -> bool:
        seen: set[str] = set()
        current = class_name
        while current and current not in seen:
            seen.add(current)
            parent = hierarchy.get(current, "")
            if parent == base:
                return True
            current = parent
        return False

    candidates = sorted(name for name in hierarchy if derives_from_midlet(name))
    return [
        MidletEntry(name.rsplit("/", 1)[-1], name.replace("/", "."), "class-hierarchy-fallback")
        for name in candidates
    ]


def discover_jar(
    jar_path: pathlib.Path,
    jar_root: pathlib.Path,
    all_midlets: bool,
) -> tuple[list[JarTarget], list[DiscoveryIssue]]:
    try:
        relative_path = str(jar_path.relative_to(jar_root))
    except ValueError:
        relative_path = str(jar_path)

    entries: list[MidletEntry] = []
    try:
        with zipfile.ZipFile(jar_path) as archive:
            attributes = read_zip_manifest(archive)
            entries = midlets_from_attributes(attributes, "JAR manifest")
            if not entries:
                entries = read_jad_midlets(jar_path)
            if not entries:
                entries = detect_midlet_classes(archive)
    except (OSError, zipfile.BadZipFile, RuntimeError, ValueError) as exc:
        return [], [DiscoveryIssue(jar_path, relative_path, "metadata", compact_text(str(exc)))]

    if not entries:
        return [], [
            DiscoveryIssue(
                jar_path,
                relative_path,
                "metadata",
                "No MIDlet-N/Main-Class entry and no class deriving from MIDlet was found",
            )
        ]

    selected = entries if all_midlets else entries[:1]
    targets = [
        JarTarget(
            item_id=stable_id(relative_path, entry.class_name),
            jar_path=jar_path,
            relative_path=relative_path,
            midlet_name=entry.name,
            main_class=entry.class_name,
            main_source=entry.source,
        )
        for entry in selected
    ]
    return targets, []


def matches_filters(path: pathlib.Path, filters: Sequence[str]) -> bool:
    if not filters:
        return True
    haystack = str(path).casefold()
    return any(token.casefold() in haystack for token in filters)


def discover_targets(
    jar_root: pathlib.Path,
    filters: Sequence[str],
    limit: int,
    all_midlets: bool,
) -> tuple[list[JarTarget], list[DiscoveryIssue], int]:
    jars = sorted(
        (
            path
            for path in jar_root.rglob("*")
            if path.is_file() and path.suffix.casefold() == ".jar" and matches_filters(path, filters)
        ),
        key=lambda path: str(path).casefold(),
    )
    discovered_count = len(jars)
    if limit > 0:
        jars = jars[:limit]

    targets: list[JarTarget] = []
    issues: list[DiscoveryIssue] = []
    for jar_path in jars:
        jar_targets, jar_issues = discover_jar(jar_path, jar_root, all_midlets)
        targets.extend(jar_targets)
        issues.extend(jar_issues)
    return targets, issues, discovered_count


def compiler_command() -> tuple[list[str], list[str]]:
    configured = os.environ.get("CXX", "").strip()
    if configured:
        return shlex.split(configured), []
    xcrun = shutil.which("xcrun")
    if xcrun:
        compiler = subprocess.check_output(
            [xcrun, "--sdk", "macosx", "--find", "clang++"], text=True
        ).strip()
        sdk = subprocess.check_output(
            [xcrun, "--sdk", "macosx", "--show-sdk-path"], text=True
        ).strip()
        return [compiler], ["-isysroot", sdk]
    compiler = shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        raise RuntimeError("a C++23 compiler is required")
    return [compiler], []


def build_harness(output_root: pathlib.Path, sanitize: bool) -> tuple[pathlib.Path | None, str]:
    build_root = output_root / "harness-build"
    build_root.mkdir(parents=True, exist_ok=True)
    binary = build_root / "CompatibilityHarness"
    stdout_path = build_root / "build.stdout.log"
    stderr_path = build_root / "build.stderr.log"

    try:
        compiler, sdk_flags = compiler_command()
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        return None, str(exc)

    sources = sorted(
        path
        for path in (CORE_ROOT / "src").rglob("*.cpp")
        if path != CORE_ROOT / "src" / "api" / "CAPI.cpp"
    )
    command = [
        *compiler,
        "-std=c++23",
        *sdk_flags,
        f"-I{CORE_ROOT / 'include'}",
        "-fno-exceptions",
        "-fno-rtti",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wshadow",
        "-Werror=return-type",
    ]
    if sanitize:
        command.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
    command.extend(
        [
            str(CORE_ROOT / "Tests" / "Compatibility" / "CompatibilityHarness.cpp"),
            *(str(path) for path in sources),
            "-lz",
        ]
    )
    if sys.platform == "darwin":
        command.extend(
            [
                "-framework",
                "CoreText",
                "-framework",
                "CoreGraphics",
                "-framework",
                "CoreFoundation",
                "-framework",
                "ImageIO",
            ]
        )
    command.extend(["-o", str(binary)])

    command_path = build_root / "build-command.txt"
    command_path.write_text(shlex.join(command) + "\n", encoding="utf-8")
    started = time.monotonic()
    try:
        with stdout_path.open("wb") as stdout_stream, stderr_path.open("wb") as stderr_stream:
            completed = subprocess.run(
                command,
                stdout=stdout_stream,
                stderr=stderr_stream,
                check=False,
                timeout=300,
            )
    except subprocess.TimeoutExpired:
        return None, "CompatibilityHarness build timed out after 300 seconds"
    duration = int((time.monotonic() - started) * 1000)
    if completed.returncode != 0 or not binary.is_file():
        stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
        return None, f"CompatibilityHarness build failed ({duration} ms): {compact_text(stderr, 2_000)}"
    return binary, ""


def add_failure(failures: list[dict[str, str]], kind: str, detail: str) -> None:
    kind = FAILURE_KIND_ALIASES.get(kind, kind)
    normalized = compact_text(detail)
    if not normalized:
        return
    key = (kind, normalized)
    if any((item.get("kind"), item.get("detail")) == key for item in failures):
        return
    failures.append({"kind": kind, "detail": normalized})


def classify_status(execution: Any, observed: Mapping[str, Any], failures: Sequence[Mapping[str, str]]) -> str:
    if execution is None:
        return "STATIC_ONLY"
    if execution.timed_out:
        return "TIMEOUT"
    if execution.launch_error:
        return "LAUNCH_ERROR"
    if execution.return_code is not None and execution.return_code < 0:
        return "NATIVE_CRASH"
    if execution.return_code not in (0, None):
        return "FAILED"
    if observed.get("install") != "success":
        return "FAILED"
    if observed.get("app_state") not in ("active", "paused", "destroyed"):
        return "FAILED"
    severe = {
        "native crash",
        "asan/ubsan/native crash",
        "verifier/ClassFormat/StackMap failure",
        "bytecode runtime semantics",
        "missing class",
        "missing method",
        "missing native",
        "missing field",
        "uncaught Java exception",
        "runtime failure",
    }
    if any(str(item.get("kind")) in severe for item in failures):
        return "FAILED"
    if int(observed.get("frames_produced", 0) or 0) > 0:
        return "STARTED_FRAME"
    milestones = set(str(value) for value in observed.get("milestones", []))
    if any(value.startswith(("canvas-", "lcdui-", "ui-")) for value in milestones):
        return "STARTED_UI"
    return "STARTED"


def inspect_target(
    compat: Any,
    target: JarTarget,
    output_root: pathlib.Path,
    runner: pathlib.Path | None,
    mode: str,
    timeout_ms: int,
    width: int,
    height: int,
) -> TargetResult:
    started = time.monotonic()
    static_error = ""
    referenced_classes: dict[str, int] = {}
    referenced_methods: dict[str, int] = {}
    referenced_fields: dict[str, int] = {}

    if mode in ("static", "both"):
        try:
            references = compat.scan_jar(target.jar_path, target.main_class)
            referenced_classes = dict(references.class_references)
            referenced_methods = dict(references.method_references)
            referenced_fields = dict(references.field_references)
        except Exception as exc:  # analyzer exposes CorpusError, but keep batch alive for all failures
            static_error = compact_text(str(exc))

    execution = None
    failures: list[dict[str, str]] = []
    run_dir = output_root / "items" / target.item_id
    if static_error:
        add_failure(failures, "static scan", static_error)

    if mode in ("smoke", "both") and runner is not None:
        item = {
            "id": target.item_id,
            "main_class": target.main_class,
            "input_sequence": [{"action": "launch"}],
            "expected": {
                "install": "success",
                "exit": "normal",
                "app_state": "active",
                "min_frames": 0,
                "milestones": [],
                "network_actions": [],
                "media_actions": [],
                "frame_hashes": [],
                "max_startup_ms": timeout_ms,
            },
        }
        execution = compat.execute_runner(
            [str(runner)],
            item,
            target.jar_path,
            run_dir,
            timeout_ms,
            {"width": width, "height": height},
        )
        logs = compat.combined_logs(execution)
        classified = compat.classify_failures(logs)
        if execution.timed_out:
            classified = [failure for failure in classified if failure.get("kind") != "timeout"]
        for failure in classified:
            add_failure(failures, str(failure.get("kind", "runtime failure")), str(failure.get("detail", "")))
        if execution.timed_out:
            add_failure(failures, "timeout", f"runner exceeded {timeout_ms} ms")
        if execution.launch_error:
            add_failure(failures, "runtime failure", execution.launch_error)
        if execution.return_code is not None and execution.return_code < 0:
            signal_number = -execution.return_code
            try:
                signal_name = signal.Signals(signal_number).name
            except ValueError:
                signal_name = str(signal_number)
            add_failure(failures, "native crash", f"process terminated by {signal_name}")
        java_exception = execution.result.get("java_exception_class") if execution.result else ""
        java_message = execution.result.get("error_message") if execution.result else ""
        message_text = str(java_message) if isinstance(java_message, str) else ""
        lowered_message = message_text.casefold()
        if (
            "abstractmethoderror" in lowered_message
            or "nosuchmethoderror" in lowered_message
            or "method was not found in the class hierarchy" in lowered_message
        ):
            add_failure(failures, "missing method", message_text)
        if "field was not found" in lowered_message or "nosuchfielderror" in lowered_message:
            add_failure(failures, "missing field", message_text)
        if "class is neither built into core nor present in the application jar" in lowered_message:
            missing_class = message_text.rsplit(":", 1)[-1].strip()
            if missing_class.startswith("["):
                add_failure(failures, "bytecode runtime semantics", message_text)
            else:
                add_failure(failures, "missing class", missing_class or message_text)
        if "native method is not ported" in lowered_message or "native method was not found" in lowered_message:
            missing_native = message_text.split(":", 1)[-1].strip()
            add_failure(failures, "missing native", missing_native or message_text)
        if "jar entry contains an unsafe path" in lowered_message:
            add_failure(failures, "install/manifest/suite failure", message_text)
        if (
            "array load opcode does not match element type" in lowered_message
            or "array store opcode does not match element type" in lowered_message
            or "operand stack" in lowered_message
            or "local slot" in lowered_message
        ):
            add_failure(failures, "bytecode runtime semantics", message_text)

        embedded_exception = re.search(
            r"((?:java|javax|com)/[A-Za-z0-9_$/]+(?:Exception|Error))",
            message_text,
        )
        if isinstance(java_exception, str) and java_exception:
            failures[:] = [
                failure
                for failure in failures
                if not (
                    failure.get("kind") == "uncaught Java exception"
                    and failure.get("detail") == "uncaught Java exception"
                )
            ]
            add_failure(
                failures,
                "uncaught Java exception",
                f"{java_exception}: {message_text}" if message_text else java_exception,
            )
        elif embedded_exception is not None:
            exception_name = embedded_exception.group(1)
            already_explained = (
                exception_name.endswith(("AbstractMethodError", "NoSuchMethodError", "NoSuchFieldError"))
                or "native method is not ported" in lowered_message
                or "class is neither built into core" in lowered_message
            )
            failures[:] = [
                failure
                for failure in failures
                if not (
                    failure.get("kind") == "uncaught Java exception"
                    and failure.get("detail") == "uncaught Java exception"
                )
            ]
            if not already_explained:
                add_failure(
                    failures,
                    "uncaught Java exception",
                    f"{exception_name}: {message_text}",
                )
        elif execution.return_code not in (0, None) and not failures:
            add_failure(
                failures,
                "runtime failure",
                f"runner returned {execution.return_code}; {message_text or 'no structured error'}",
            )

    observed = compat.observed_data(execution)
    status = classify_status(execution, observed, failures)
    duration_ms = int((time.monotonic() - started) * 1000)

    result = TargetResult(
        target=target,
        status=status,
        duration_ms=duration_ms,
        static_error=static_error,
        launch_error=execution.launch_error if execution is not None else "",
        failures=failures,
        observed=observed,
        referenced_classes=referenced_classes,
        referenced_methods=referenced_methods,
        referenced_fields=referenced_fields,
        run_dir=str(run_dir),
    )
    if execution is not None:
        result.stdout_log = str(execution.stdout_path)
        result.stderr_log = str(execution.stderr_path)
        result.result_json = str(execution.result_path)
    update_progress(result)
    return result


def update_progress(result: TargetResult) -> None:
    global _PROGRESS_DONE
    with _PROGRESS_LOCK:
        _PROGRESS_DONE += 1
        done = _PROGRESS_DONE
        total = _PROGRESS_TOTAL
        elapsed = max(0.001, time.monotonic() - _PROGRESS_STARTED)
        rate = done / elapsed
        if done == total or done <= 10 or done % 25 == 0:
            print(
                f"[{done:4d}/{total:4d}] {result.status:13s} "
                f"{result.target.relative_path} :: {result.target.main_class} "
                f"({rate:.1f}/s)",
                flush=True,
            )


def target_result_to_json(result: TargetResult) -> dict[str, Any]:
    return {
        "id": result.target.item_id,
        "jar": str(result.target.jar_path),
        "relative_path": result.target.relative_path,
        "midlet_name": result.target.midlet_name,
        "main_class": result.target.main_class,
        "main_source": result.target.main_source,
        "status": result.status,
        "duration_ms": result.duration_ms,
        "static_error": result.static_error,
        "launch_error": result.launch_error,
        "failures": result.failures,
        "observed": result.observed,
        "reference_counts": {
            "classes": sum(result.referenced_classes.values()),
            "methods": sum(result.referenced_methods.values()),
            "fields": sum(result.referenced_fields.values()),
            "unique_classes": len(result.referenced_classes),
            "unique_methods": len(result.referenced_methods),
            "unique_fields": len(result.referenced_fields),
        },
        "artifacts": {
            "run_dir": result.run_dir,
            "stdout": result.stdout_log,
            "stderr": result.stderr_log,
            "runner_result": result.result_json,
        },
    }


def failure_signature(failure: Mapping[str, str]) -> tuple[str, str]:
    kind = str(failure.get("kind", "runtime failure"))
    detail = compact_text(str(failure.get("detail", "")), 300)
    return kind, detail


def aggregate_report(
    jar_root: pathlib.Path,
    output_root: pathlib.Path,
    mode: str,
    timeout_ms: int,
    jobs: int,
    smoke_limit: int,
    smoke_filters: Sequence[str],
    runner: pathlib.Path | None,
    discovered_count: int,
    targets: Sequence[JarTarget],
    discovery_issues: Sequence[DiscoveryIssue],
    results: Sequence[TargetResult],
    build_error: str,
    command: Sequence[str],
) -> dict[str, Any]:
    statuses = collections.Counter(result.status for result in results)
    smoke_tested = sum(1 for result in results if result.status != "STATIC_ONLY")
    static_scanned = sum(1 for result in results if not result.static_error)
    failure_groups: dict[tuple[str, str], list[TargetResult]] = collections.defaultdict(list)
    for result in results:
        for failure in result.failures:
            failure_groups[failure_signature(failure)].append(result)
    for issue in discovery_issues:
        pseudo = TargetResult(
            target=JarTarget(
                item_id=stable_id(issue.relative_path, "metadata"),
                jar_path=issue.jar_path,
                relative_path=issue.relative_path,
                midlet_name="",
                main_class="",
                main_source="",
            ),
            status="METADATA_ERROR",
            failures=[{"kind": issue.kind, "detail": issue.detail}],
        )
        failure_groups[(issue.kind, issue.detail)].append(pseudo)

    class_counts: collections.Counter[str] = collections.Counter()
    method_counts: collections.Counter[str] = collections.Counter()
    field_counts: collections.Counter[str] = collections.Counter()
    for result in results:
        class_counts.update(result.referenced_classes)
        method_counts.update(result.referenced_methods)
        field_counts.update(result.referenced_fields)

    api_class_counts = {
        name: count
        for name, count in class_counts.most_common()
        if name.startswith(API_PREFIXES)
    }
    api_method_counts = {
        name: count
        for name, count in method_counts.most_common()
        if name.startswith(API_PREFIXES)
    }

    sorted_groups = sorted(
        failure_groups.items(),
        key=lambda item: (
            PRIORITY_ORDER.get(item[0][0], 99),
            -len({result.target.relative_path for result in item[1]}),
            item[0][1].casefold(),
        ),
    )

    return {
        "schema_version": 1,
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "command": list(command),
        "configuration": {
            "jar_root": str(jar_root),
            "output_root": str(output_root),
            "mode": mode,
            "timeout_ms": timeout_ms,
            "jobs": jobs,
            "smoke_limit": smoke_limit,
            "smoke_filters": list(smoke_filters),
            "runner": str(runner) if runner is not None else "",
            "runner_sha256": sha256_file(runner) if runner is not None and runner.is_file() else "",
        },
        "summary": {
            "jar_files_matching_filter": discovered_count,
            "targets_selected": len(targets),
            "metadata_errors": len(discovery_issues),
            "tested_targets": len(results),
            "static_scanned_without_parser_error": static_scanned,
            "smoke_tested": smoke_tested,
            "statuses": dict(sorted(statuses.items())),
            "unique_failure_signatures": len(failure_groups),
            "build_error": build_error,
        },
        "failure_groups": [
            {
                "kind": kind,
                "detail": detail,
                "affected_count": len({result.target.relative_path for result in affected}),
                "affected": sorted({result.target.relative_path for result in affected}),
            }
            for (kind, detail), affected in sorted_groups
        ],
        "top_referenced_api_classes": list(api_class_counts.items())[:200],
        "top_referenced_api_methods": list(api_method_counts.items())[:200],
        "top_referenced_fields": list(field_counts.most_common(100)),
        "discovery_issues": [dataclasses.asdict(issue) | {"jar_path": str(issue.jar_path)} for issue in discovery_issues],
        "items": [target_result_to_json(result) for result in results],
    }


def write_markdown_report(report: Mapping[str, Any], path: pathlib.Path) -> None:
    summary = report["summary"]
    config = report["configuration"]
    lines: list[str] = [
        "# phoneME C++ — JAR Test Fix Report",
        "",
        f"Generated: `{report['generated_at']}`  ",
        f"JAR root: `{config['jar_root']}`  ",
        f"Mode: `{config['mode']}`, timeout: `{config['timeout_ms']} ms`, jobs: `{config['jobs']}`, "
        f"smoke limit: `{config['smoke_limit'] or 'all'}`  ",
        f"Runner: `{config['runner'] or 'none'}`  ",
        f"Runner SHA-256: `{config['runner_sha256'] or 'n/a'}`",
        "",
        "## Summary",
        "",
        "| Metric | Count |",
        "| --- | ---: |",
        f"| Matching JAR files | {summary['jar_files_matching_filter']} |",
        f"| MIDlet targets selected | {summary['targets_selected']} |",
        f"| Metadata/discovery errors | {summary['metadata_errors']} |",
        f"| Targets included in report | {summary['tested_targets']} |",
        f"| Static scans without parser error | {summary['static_scanned_without_parser_error']} |",
        f"| Smoke-launched targets | {summary['smoke_tested']} |",
        f"| Unique failure signatures | {summary['unique_failure_signatures']} |",
    ]
    for status, count in summary["statuses"].items():
        lines.append(f"| `{markdown_escape(status)}` | {count} |")

    if summary.get("build_error"):
        lines.extend(
            [
                "",
                "## Harness build blocker",
                "",
                f"> {markdown_escape(str(summary['build_error']))}",
            ]
        )

    lines.extend(
        [
            "",
            "## Priority fix queue",
            "",
            "The same root cause is grouped once even when it breaks many games.",
            "",
            "| Priority | Kind | Affected JARs | Error signature | Samples |",
            "| ---: | --- | ---: | --- | --- |",
        ]
    )
    for index, group in enumerate(report["failure_groups"], start=1):
        samples = ", ".join(group["affected"][:5])
        lines.append(
            "| {priority} | `{kind}` | {count} | {detail} | {samples} |".format(
                priority=index,
                kind=markdown_escape(str(group["kind"])),
                count=group["affected_count"],
                detail=markdown_escape(str(group["detail"])),
                samples=markdown_escape(samples),
            )
        )

    lines.extend(
        [
            "",
            "## Most referenced J2ME/vendor API classes",
            "",
            "This is static demand from reachable classes, not a claim that every API is missing.",
            "",
            "| Class | References |",
            "| --- | ---: |",
        ]
    )
    for name, count in report["top_referenced_api_classes"][:100]:
        lines.append(f"| `{markdown_escape(str(name))}` | {count} |")

    lines.extend(
        [
            "",
            "## Most referenced J2ME/vendor methods",
            "",
            "| Method | References |",
            "| --- | ---: |",
        ]
    )
    for name, count in report["top_referenced_api_methods"][:100]:
        lines.append(f"| `{markdown_escape(str(name))}` | {count} |")

    discovery = report["discovery_issues"]
    if discovery:
        lines.extend(
            [
                "",
                "## Metadata/discovery errors",
                "",
                "| JAR | Error |",
                "| --- | --- |",
            ]
        )
        for issue in discovery:
            lines.append(
                f"| `{markdown_escape(str(issue['relative_path']))}` | "
                f"{markdown_escape(str(issue['detail']))} |"
            )

    lines.extend(
        [
            "",
            "## Per-game result",
            "",
            "| JAR | Main MIDlet | Status | Startup | Frame | Primary error | Artifacts |",
            "| --- | --- | --- | ---: | ---: | --- | --- |",
        ]
    )
    for item in report["items"]:
        observed = item["observed"]
        failures = item["failures"]
        primary = ""
        if failures:
            primary = f"{failures[0]['kind']}: {failures[0]['detail']}"
        artifacts = item["artifacts"]
        artifact_text = artifacts.get("run_dir", "")
        lines.append(
            "| `{jar}` | `{main}` | `{status}` | {startup} | {frames} | {error} | `{artifacts}` |".format(
                jar=markdown_escape(str(item["relative_path"])),
                main=markdown_escape(str(item["main_class"])),
                status=markdown_escape(str(item["status"])),
                startup=observed.get("startup_ms", ""),
                frames=observed.get("frames_produced", 0),
                error=markdown_escape(primary),
                artifacts=markdown_escape(str(artifact_text)),
            )
        )

    lines.extend(
        [
            "",
            "## Re-run commands",
            "",
            "```sh",
            "# Full folder: static scan + launch smoke test",
            "bash Core/Tools/test-jar-directory.sh",
            "",
            "# Test only matching names",
            "bash Core/Tools/test-jar-directory.sh --filter MIDPlay --filter Nicknsonet",
            "",
            "# Faster static inventory without launching games",
            "bash Core/Tools/test-jar-directory.sh --mode static",
            "",
            "# Longer startup window for a smaller subset",
            "bash Core/Tools/test-jar-directory.sh --filter Gameloft --limit 50 --timeout-ms 10000",
            "```",
            "",
            f"Machine-readable report: `{path.with_suffix('.json')}`",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Batch static/smoke test J2ME JARs and aggregate failures into one report."
    )
    parser.add_argument("--jar-dir", type=pathlib.Path, default=DEFAULT_JAR_DIR)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--mode", choices=("static", "smoke", "both"), default="both")
    parser.add_argument("--filter", action="append", default=[], help="case-insensitive path substring")
    parser.add_argument("--limit", type=int, default=0, help="maximum matching JAR files; 0 means all")
    parser.add_argument("--all-midlets", action="store_true", help="test every MIDlet-N entry, not only MIDlet-1")
    parser.add_argument(
        "--smoke-limit",
        type=int,
        default=0,
        help="launch only an evenly distributed sample; static scan still covers all selected JARs",
    )
    parser.add_argument(
        "--smoke-filter",
        action="append",
        default=[],
        help="always smoke-launch matching path/main class in addition to the sampled targets",
    )
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument("--timeout-ms", type=int, default=DEFAULT_TIMEOUT_MS)
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--runner", type=pathlib.Path, help="reuse an existing CompatibilityHarness binary")
    parser.add_argument("--sanitize", action="store_true", help="build harness with ASan/UBSan")
    args = parser.parse_args(argv)
    if args.limit < 0:
        parser.error("--limit must be >= 0")
    if args.smoke_limit < 0:
        parser.error("--smoke-limit must be >= 0")
    if args.jobs <= 0:
        parser.error("--jobs must be > 0")
    if args.timeout_ms <= 0:
        parser.error("--timeout-ms must be > 0")
    if not (1 <= args.width <= 8192 and 1 <= args.height <= 8192):
        parser.error("--width/--height must be in 1..8192")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_arguments(sys.argv[1:] if argv is None else argv)
    jar_root = args.jar_dir.expanduser().resolve()
    if not jar_root.is_dir():
        print(f"JAR directory does not exist: {jar_root}", file=sys.stderr)
        return 2

    output_root = (
        args.output.expanduser().resolve()
        if args.output
        else CORE_ROOT / "build" / "jar-directory-tests" / utc_timestamp()
    )
    output_root.mkdir(parents=True, exist_ok=True)
    report_path = (
        args.report.expanduser().resolve()
        if args.report
        else output_root / "JAR_TEST_FIX_REPORT.md"
    )

    print(f"Discovering JARs under {jar_root}...", flush=True)
    targets, discovery_issues, discovered_count = discover_targets(
        jar_root,
        args.filter,
        args.limit,
        args.all_midlets,
    )
    print(
        f"Found {discovered_count} matching JARs; selected {len(targets)} MIDlet targets; "
        f"metadata errors: {len(discovery_issues)}",
        flush=True,
    )

    compat = load_compat_module()
    runner: pathlib.Path | None = None
    build_error = ""
    if args.mode in ("smoke", "both"):
        if args.runner:
            runner = args.runner.expanduser().resolve()
            if not runner.is_file():
                build_error = f"runner does not exist: {runner}"
                runner = None
        else:
            print("Building isolated CompatibilityHarness...", flush=True)
            runner, build_error = build_harness(output_root, args.sanitize)
        if build_error:
            print(build_error, file=sys.stderr, flush=True)

    smoke_ids: set[str] = set()
    if args.mode in ("smoke", "both") and runner is not None:
        if args.smoke_limit <= 0 or args.smoke_limit >= len(targets):
            smoke_ids = {target.item_id for target in targets}
        elif args.smoke_limit == 1:
            smoke_ids = {targets[0].item_id}
        else:
            last = len(targets) - 1
            indices = {
                round(index * last / (args.smoke_limit - 1))
                for index in range(args.smoke_limit)
            }
            smoke_ids = {targets[index].item_id for index in sorted(indices)}
        for target in targets:
            smoke_haystack = f"{target.relative_path}\n{target.main_class}".casefold()
            if any(token.casefold() in smoke_haystack for token in args.smoke_filter):
                smoke_ids.add(target.item_id)
        print(
            f"Smoke-launching {len(smoke_ids)} of {len(targets)} targets; "
            "all selected targets still receive static scanning.",
            flush=True,
        )

    results: list[TargetResult] = []
    if targets and (args.mode == "static" or runner is not None):
        global _PROGRESS_DONE, _PROGRESS_TOTAL, _PROGRESS_STARTED
        _PROGRESS_DONE = 0
        _PROGRESS_TOTAL = len(targets)
        _PROGRESS_STARTED = time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [
                executor.submit(
                    inspect_target,
                    compat,
                    target,
                    output_root,
                    runner if target.item_id in smoke_ids else None,
                    args.mode,
                    args.timeout_ms,
                    args.width,
                    args.height,
                )
                for target in targets
            ]
            for future in concurrent.futures.as_completed(futures):
                try:
                    results.append(future.result())
                except Exception as exc:
                    # A tooling bug must be visible but must not discard all completed game evidence.
                    print(f"worker failed: {exc}", file=sys.stderr, flush=True)
        results.sort(key=lambda result: (result.target.relative_path.casefold(), result.target.main_class))

    report = aggregate_report(
        jar_root=jar_root,
        output_root=output_root,
        mode=args.mode,
        timeout_ms=args.timeout_ms,
        jobs=args.jobs,
        smoke_limit=args.smoke_limit,
        smoke_filters=args.smoke_filter,
        runner=runner,
        discovered_count=discovered_count,
        targets=targets,
        discovery_issues=discovery_issues,
        results=results,
        build_error=build_error,
        command=[str(SCRIPT_PATH), *(sys.argv[1:] if argv is None else argv)],
    )
    json_path = report_path.with_suffix(".json")
    json_write(json_path, report)
    write_markdown_report(report, report_path)

    print(f"Report: {report_path}", flush=True)
    print(f"JSON:   {json_path}", flush=True)

    if build_error:
        return 2
    failed_statuses = {"FAILED", "TIMEOUT", "NATIVE_CRASH", "LAUNCH_ERROR"}
    failed = sum(1 for result in results if result.status in failed_statuses)
    return 1 if failed or discovery_issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
