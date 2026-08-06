#!/usr/bin/env python3
"""Reproducible phoneME compatibility corpus runner and analyzer.

The tool intentionally uses only Python's standard library.  It can:
- validate the corpus manifest and license metadata;
- build project-authored Java fixtures without packaging MIDP stubs;
- scan JAR class files for referenced classes/methods;
- execute the phoneME compatibility runner and an optional reference runner;
- classify install/verifier/linkage/runtime/network/media/frame failures;
- produce machine-readable JSON plus a Markdown dashboard;
- refresh the generated compatibility evidence section in J2ME_API_COVERAGE.md.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from typing import Any, Iterable, Iterator, Mapping, Sequence

SCHEMA_VERSION = 1
GENERATED_BEGIN = "<!-- COMPATIBILITY_CORPUS_GENERATED:BEGIN -->"
GENERATED_END = "<!-- COMPATIBILITY_CORPUS_GENERATED:END -->"

FAILURE_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("missing_class", re.compile(
        r"(?:ClassNotFoundException|NoClassDefFoundError|class\s+not\s+found)"
        r"\s*[:=-]?\s*([A-Za-z_$][A-Za-z0-9_.$/]*(?:\$[A-Za-z0-9_$]+)?)",
        re.IGNORECASE,
    )),
    ("missing_method", re.compile(
        r"(?:NoSuchMethodError|method\s+not\s+found)\s*[:=-]?\s*([^\r\n]+)",
        re.IGNORECASE,
    )),
    ("missing_native", re.compile(
        r"(?:UnsatisfiedLinkError|missing\s+native|native\s+method[^\r\n]*not\s+found)"
        r"\s*[:=-]?\s*([^\r\n]*)",
        re.IGNORECASE,
    )),
    ("verifier", re.compile(
        r"(?:VerifyError|ClassFormatError|verification\s+failed|"
        r"inconsistent\s+or\s+missing\s+stackmap|malformed\s+class)",
        re.IGNORECASE,
    )),
    ("install", re.compile(
        r"(?:install(?:ation)?\s+(?:failed|error)|invalid\s+(?:MIDlet|suite|manifest)|"
        r"malformed\s+manifest)",
        re.IGNORECASE,
    )),
    ("thread", re.compile(
        r"(?:IllegalThreadStateException|monitor\s+contention\s+requires\s+Java\s+scheduler|"
        r"deadlock\s+detected|thread\s+(?:failed|error)|scheduler\s+(?:failed|error))",
        re.IGNORECASE,
    )),
    ("lcdui", re.compile(
        r"(?:LCDUI(?:\s+bridge)?\s+(?:failed|error)|"
        r"Displayable[^\r\n]*(?:failed|unsupported)|UI\s+callback\s+(?:failed|error))",
        re.IGNORECASE,
    )),
    ("graphics", re.compile(
        r"(?:graphics\s+(?:failed|error)|framebuffer\s+(?:failed|error)|"
        r"PNG(?:\s+decode)?\s+(?:failed|error)|render(?:ing)?\s+(?:failed|error))",
        re.IGNORECASE,
    )),
    ("rms", re.compile(
        r"(?:RecordStore(?:Exception|FullException|NotOpenException|NotFoundException)|"
        r"RMS\s+(?:failed|error|corrupt))",
        re.IGNORECASE,
    )),
    ("performance", re.compile(
        r"(?:OutOfMemoryError|memory\s+pressure|CPU\s+limit|"
        r"frame\s+budget\s+exceeded|startup[^\r\n]*\s+exceeds)",
        re.IGNORECASE,
    )),
    ("uncaught_exception", re.compile(
        r"(?:uncaught(?:\s+java)?\s+(?:exception|throwable)|"
        r"terminated\s+with\s+uncaught\s+(?:java|javax|com)/"
        r"[A-Za-z0-9_$/]+(?:Exception|Error)|"
        r"application\s+has\s+unexpectedly\s+quit)",
        re.IGNORECASE,
    )),
    ("network", re.compile(
        r"(?:ConnectionNotFoundException|SocketException|UnknownHostException|"
        r"SSLHandshakeException|network\s+(?:error|failed|timeout))",
        re.IGNORECASE,
    )),
    ("media", re.compile(
        r"(?:MediaException|media\s+(?:error|failed)|unsupported\s+(?:codec|media))",
        re.IGNORECASE,
    )),
    ("timeout", re.compile(r"(?:timed?\s*out|timeout)", re.IGNORECASE)),
    ("native_crash", re.compile(
        r"(?:AddressSanitizer|UndefinedBehaviorSanitizer|SIG(?:ABRT|SEGV|BUS)|"
        r"segmentation\s+fault|abort\(\))",
        re.IGNORECASE,
    )),
)

MILESTONE_RE = re.compile(r"COMPAT_MILESTONE\s*:\s*([A-Za-z0-9_.:/-]+)")
NETWORK_ACTION_RE = re.compile(r"COMPAT_NETWORK\s*:\s*([A-Za-z0-9_.:/-]+)")
MEDIA_ACTION_RE = re.compile(r"COMPAT_MEDIA\s*:\s*([A-Za-z0-9_.:/-]+)")


class CorpusError(RuntimeError):
    """Manifest, fixture, or corpus execution error."""


@dataclasses.dataclass(frozen=True)
class ClassReferences:
    defined_classes: frozenset[str]
    class_references: collections.Counter[str]
    method_references: collections.Counter[str]
    field_references: collections.Counter[str]


@dataclasses.dataclass
class RunnerExecution:
    command: list[str]
    return_code: int | None
    timed_out: bool
    duration_ms: int
    stdout_path: pathlib.Path
    stderr_path: pathlib.Path
    result_path: pathlib.Path
    frame_path: pathlib.Path
    scenario_path: pathlib.Path
    result: dict[str, Any]
    launch_error: str = ""


@dataclasses.dataclass
class ItemEvaluation:
    item_id: str
    description: str
    categories: list[str]
    metadata: dict[str, Any]
    status: str
    reasons: list[str]
    failures: list[dict[str, str]]
    jar_path: str
    jar_sha256: str
    startup_ms: int | None
    frames_produced: int
    frame_sha256: str
    milestones: list[str]
    network_actions: list[str]
    media_actions: list[str]
    app_state: str
    runner_exit_code: int | None
    reference: dict[str, Any] | None
    differential: dict[str, Any] | None
    class_reference_count: int
    method_reference_count: int


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise CorpusError(f"cannot read JSON {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise CorpusError(f"invalid JSON {path}:{exc.lineno}:{exc.colno}: {exc.msg}") from exc
    if not isinstance(data, dict):
        raise CorpusError(f"top-level JSON value in {path} must be an object")
    return data


def write_json(path: pathlib.Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def resolve_path(base: pathlib.Path, value: str) -> pathlib.Path:
    candidate = pathlib.Path(value)
    return candidate if candidate.is_absolute() else (base / candidate).resolve()


def require_string(mapping: Mapping[str, Any], key: str, context: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        raise CorpusError(f"{context}.{key} must be a non-empty string")
    return value


def require_bool(mapping: Mapping[str, Any], key: str, context: str) -> bool:
    value = mapping.get(key)
    if not isinstance(value, bool):
        raise CorpusError(f"{context}.{key} must be a boolean")
    return value


def string_list(value: Any, context: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise CorpusError(f"{context} must be an array of strings")
    return [item for item in value if item]


def validate_manifest(manifest: Mapping[str, Any], manifest_path: pathlib.Path) -> None:
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise CorpusError(
            f"{manifest_path}: schema_version must be {SCHEMA_VERSION}, "
            f"got {manifest.get('schema_version')!r}"
        )
    defaults = manifest.get("defaults", {})
    if not isinstance(defaults, dict):
        raise CorpusError("defaults must be an object")
    timeout_ms = defaults.get("timeout_ms", 15_000)
    if not isinstance(timeout_ms, int) or timeout_ms <= 0:
        raise CorpusError("defaults.timeout_ms must be a positive integer")
    dimensions = defaults.get("dimensions", {"width": 320, "height": 240})
    if not isinstance(dimensions, dict):
        raise CorpusError("defaults.dimensions must be an object")
    for dimension in ("width", "height"):
        value = dimensions.get(dimension)
        if not isinstance(value, int) or value <= 0 or value > 8192:
            raise CorpusError(f"defaults.dimensions.{dimension} must be in 1..8192")
    require_string(defaults, "configuration", "defaults")
    require_string(defaults, "profile", "defaults")
    device_profile = defaults.get("device_profile")
    if not isinstance(device_profile, dict):
        raise CorpusError("defaults.device_profile must be an object")
    for key in ("name", "platform", "resolution", "locale"):
        require_string(device_profile, key, "defaults.device_profile")
    if not string_list(device_profile.get("input"), "defaults.device_profile.input"):
        raise CorpusError("defaults.device_profile.input must not be empty")
    default_input_sequence = defaults.get("input_sequence", [])
    if not isinstance(default_input_sequence, list):
        raise CorpusError("defaults.input_sequence must be an array")
    for step_index, step in enumerate(default_input_sequence):
        if not isinstance(step, dict):
            raise CorpusError(f"defaults.input_sequence[{step_index}] must be an object")
        require_string(step, "action", f"defaults.input_sequence[{step_index}]")

    corpus = manifest.get("corpus")
    if not isinstance(corpus, list) or not corpus:
        raise CorpusError("corpus must be a non-empty array")
    seen: set[str] = set()
    for index, raw_item in enumerate(corpus):
        context = f"corpus[{index}]"
        if not isinstance(raw_item, dict):
            raise CorpusError(f"{context} must be an object")
        item_id = require_string(raw_item, "id", context)
        if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]*", item_id):
            raise CorpusError(f"{context}.id has unsupported characters: {item_id}")
        if item_id in seen:
            raise CorpusError(f"duplicate corpus id: {item_id}")
        seen.add(item_id)
        require_string(raw_item, "description", context)
        require_string(raw_item, "jar", context)
        require_string(raw_item, "sha256", context)
        require_string(raw_item, "main_class", context)
        require_bool(raw_item, "enabled", context)
        require_bool(raw_item, "required", context)
        categories = string_list(raw_item.get("categories"), f"{context}.categories")
        if not categories:
            raise CorpusError(f"{context}.categories must not be empty")
        string_list(raw_item.get("required_apis"), f"{context}.required_apis")
        if "configuration" in raw_item:
            require_string(raw_item, "configuration", context)
        if "profile" in raw_item:
            require_string(raw_item, "profile", context)
        item_device_profile = raw_item.get("device_profile")
        if item_device_profile is not None and not isinstance(item_device_profile, dict):
            raise CorpusError(f"{context}.device_profile must be an object")
        input_sequence = raw_item.get("input_sequence", default_input_sequence)
        if not isinstance(input_sequence, list):
            raise CorpusError(f"{context}.input_sequence must be an array")
        for step_index, step in enumerate(input_sequence):
            if not isinstance(step, dict):
                raise CorpusError(
                    f"{context}.input_sequence[{step_index}] must be an object"
                )
            require_string(step, "action", f"{context}.input_sequence[{step_index}]")

        license_info = raw_item.get("license")
        if not isinstance(license_info, dict):
            raise CorpusError(f"{context}.license must be an object")
        require_string(license_info, "kind", f"{context}.license")
        require_string(license_info, "source", f"{context}.license")
        require_string(license_info, "provenance", f"{context}.license")
        require_bool(license_info, "redistributable", f"{context}.license")

        reference = raw_item.get("reference")
        if reference is not None:
            if not isinstance(reference, dict):
                raise CorpusError(f"{context}.reference must be an object")
            require_string(reference, "runtime", f"{context}.reference")
            require_string(reference, "version", f"{context}.reference")

        expected = raw_item.get("expected")
        if not isinstance(expected, dict):
            raise CorpusError(f"{context}.expected must be an object")
        install = expected.get("install")
        if install not in ("success", "failure", "any"):
            raise CorpusError(f"{context}.expected.install must be success/failure/any")
        exit_state = expected.get("exit")
        if exit_state not in ("normal", "nonzero", "any"):
            raise CorpusError(f"{context}.expected.exit must be normal/nonzero/any")
        for key in ("milestones", "network_actions", "media_actions", "frame_hashes"):
            string_list(expected.get(key), f"{context}.expected.{key}")
        min_frames = expected.get("min_frames", 0)
        if not isinstance(min_frames, int) or min_frames < 0:
            raise CorpusError(f"{context}.expected.min_frames must be >= 0")
        max_startup_ms = expected.get("max_startup_ms", timeout_ms)
        if not isinstance(max_startup_ms, int) or max_startup_ms <= 0:
            raise CorpusError(f"{context}.expected.max_startup_ms must be positive")

        build = raw_item.get("build")
        if build is not None:
            if not isinstance(build, dict):
                raise CorpusError(f"{context}.build must be an object")
            sources = string_list(build.get("sources"), f"{context}.build.sources")
            if not sources:
                raise CorpusError(f"{context}.build.sources must not be empty")
            require_string(build, "manifest", f"{context}.build")


def resolved_item_metadata(
    item: Mapping[str, Any], defaults: Mapping[str, Any]
) -> dict[str, Any]:
    device_profile = dict(defaults.get("device_profile", {}))
    item_device_profile = item.get("device_profile", {})
    if isinstance(item_device_profile, dict):
        device_profile.update(item_device_profile)
    input_sequence = item.get("input_sequence", defaults.get("input_sequence", []))
    return {
        "configuration": item.get("configuration", defaults.get("configuration", "")),
        "profile": item.get("profile", defaults.get("profile", "")),
        "device_profile": device_profile,
        "input_sequence": input_sequence if isinstance(input_sequence, list) else [],
        "required_apis": list(item.get("required_apis", [])),
        "license": dict(item.get("license", {})),
        "reference": dict(item.get("reference", {})),
    }


def iter_selected_items(
    manifest: Mapping[str, Any], filters: Sequence[str], include_disabled: bool
) -> Iterator[dict[str, Any]]:
    normalized = set(filters)
    for raw_item in manifest["corpus"]:
        item = dict(raw_item)
        if not include_disabled and not item["enabled"]:
            continue
        if normalized:
            item_tokens = {item["id"], *item.get("categories", [])}
            if not normalized.intersection(item_tokens):
                continue
        yield item


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def command_path(name_or_path: str) -> str:
    located = shutil.which(name_or_path)
    if located:
        return located
    candidate = pathlib.Path(name_or_path)
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return str(candidate.resolve())
    raise CorpusError(f"required executable not found: {name_or_path}")


def build_fixture_jars(
    items: Iterable[dict[str, Any]], manifest_dir: pathlib.Path, build_root: pathlib.Path
) -> None:
    groups: dict[pathlib.Path, dict[str, Any]] = {}
    for item in items:
        build = item.get("build")
        if build is None:
            continue
        jar_path = resolve_path(manifest_dir, item["jar"])
        existing = groups.get(jar_path)
        if existing is not None and existing != build:
            raise CorpusError(f"conflicting build recipes for {jar_path}")
        groups[jar_path] = build

    if not groups:
        return
    javac = command_path(os.environ.get("JAVAC", "javac"))
    jar_tool = command_path(os.environ.get("JAR", "jar"))
    shared_stub_root = (manifest_dir.parent / "Tests" / "stubs").resolve()
    local_stub_root = (manifest_dir / "fixtures" / "stubs").resolve()
    if not shared_stub_root.is_dir():
        raise CorpusError(
            f"compile-time MIDP stubs not found: {shared_stub_root}"
        )

    # Compatibility-owned stubs make the corpus checkpoint self-contained for
    # APIs that may still be under development in another workplan item. They
    # override shared stubs by relative Java source path to avoid duplicate
    # class definitions when the subsystem is later integrated.
    stub_sources_by_name: dict[str, pathlib.Path] = {}
    for root in (shared_stub_root, local_stub_root):
        if not root.is_dir():
            continue
        for source in sorted(root.rglob("*.java")):
            stub_sources_by_name[source.relative_to(root).as_posix()] = source
    stub_sources = [stub_sources_by_name[name]
                    for name in sorted(stub_sources_by_name)]
    if not stub_sources:
        raise CorpusError(
            f"no Java stubs found under {shared_stub_root} or {local_stub_root}"
        )

    stub_classes = build_root / "compile-stubs"
    if stub_classes.exists():
        shutil.rmtree(stub_classes)
    stub_classes.mkdir(parents=True)
    subprocess.run(
        [
            javac,
            "-source", "8",
            "-target", "8",
            "-Xlint:-options",
            "-Xlint:-unchecked",
            "-d", str(stub_classes),
            *map(str, stub_sources),
        ],
        check=True,
    )

    for jar_path, build in groups.items():
        safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", jar_path.stem)
        classes = build_root / f"classes-{safe_name}"
        if classes.exists():
            shutil.rmtree(classes)
        classes.mkdir(parents=True)
        sources = [resolve_path(manifest_dir, source) for source in build["sources"]]
        missing = [str(source) for source in sources if not source.is_file()]
        if missing:
            raise CorpusError(f"fixture source(s) missing: {', '.join(missing)}")
        manifest_file = resolve_path(manifest_dir, build["manifest"])
        if not manifest_file.is_file():
            raise CorpusError(f"fixture manifest missing: {manifest_file}")
        subprocess.run(
            [
                javac,
                "-source", "8",
                "-target", "8",
                "-Xlint:-options",
                "-classpath", str(stub_classes),
                "-d", str(classes),
                *map(str, sources),
            ],
            check=True,
        )
        jar_path.parent.mkdir(parents=True, exist_ok=True)
        if jar_path.exists():
            jar_path.unlink()
        subprocess.run(
            [jar_tool, "cfm", str(jar_path), str(manifest_file), "-C", str(classes), "."],
            check=True,
        )
        with zipfile.ZipFile(jar_path) as archive:
            forbidden = [
                name for name in archive.namelist()
                if name.startswith("javax/microedition/") or name.startswith("com/sun/midp/")
            ]
        if forbidden:
            jar_path.unlink(missing_ok=True)
            raise CorpusError(
                f"fixture JAR {jar_path} accidentally packaged compile-time stubs: "
                + ", ".join(forbidden[:5])
            )


def read_u1(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 1 > len(data):
        raise CorpusError("truncated class file")
    return data[offset], offset + 1


def read_u2(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 2 > len(data):
        raise CorpusError("truncated class file")
    return struct.unpack_from(">H", data, offset)[0], offset + 2


def read_u4(data: memoryview, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise CorpusError("truncated class file")
    return struct.unpack_from(">I", data, offset)[0], offset + 4


def parse_class_file(raw: bytes, source: str) -> tuple[str, list[str], list[str], list[str]]:
    data = memoryview(raw)
    if len(data) < 10 or data[:4].tobytes() != b"\xca\xfe\xba\xbe":
        raise CorpusError(f"{source}: invalid class-file magic")
    offset = 8
    cp_count, offset = read_u2(data, offset)
    cp: list[Any] = [None] * cp_count
    index = 1
    while index < cp_count:
        tag, offset = read_u1(data, offset)
        if tag == 1:
            length, offset = read_u2(data, offset)
            if offset + length > len(data):
                raise CorpusError(f"{source}: truncated UTF-8 constant")
            cp[index] = (tag, data[offset:offset + length].tobytes().decode("utf-8", "replace"))
            offset += length
        elif tag in (3, 4):
            value, offset = read_u4(data, offset)
            cp[index] = (tag, value)
        elif tag in (5, 6):
            high, offset = read_u4(data, offset)
            low, offset = read_u4(data, offset)
            cp[index] = (tag, (high << 32) | low)
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
            raise CorpusError(f"{source}: unsupported constant-pool tag {tag}")
        index += 1

    _access_flags, offset = read_u2(data, offset)
    this_class, offset = read_u2(data, offset)

    def utf8(cp_index: int) -> str:
        if cp_index <= 0 or cp_index >= len(cp) or not cp[cp_index] or cp[cp_index][0] != 1:
            raise CorpusError(f"{source}: invalid UTF-8 constant index {cp_index}")
        return str(cp[cp_index][1])

    def class_name(cp_index: int) -> str:
        if cp_index <= 0 or cp_index >= len(cp) or not cp[cp_index] or cp[cp_index][0] != 7:
            raise CorpusError(f"{source}: invalid class constant index {cp_index}")
        return utf8(cp[cp_index][1])

    def name_and_type(cp_index: int) -> tuple[str, str]:
        if cp_index <= 0 or cp_index >= len(cp) or not cp[cp_index] or cp[cp_index][0] != 12:
            raise CorpusError(f"{source}: invalid NameAndType constant index {cp_index}")
        return utf8(cp[cp_index][1]), utf8(cp[cp_index][2])

    defined = class_name(this_class)
    classes: list[str] = []
    methods: list[str] = []
    fields: list[str] = []
    for cp_index, entry in enumerate(cp[1:], start=1):
        if not entry:
            continue
        tag = entry[0]
        if tag == 7:
            name = class_name(cp_index)
            if not name.startswith("["):
                classes.append(name)
        elif tag in (9, 10, 11):
            owner = class_name(entry[1])
            name, descriptor = name_and_type(entry[2])
            formatted = f"{owner}.{name}{descriptor}"
            if tag == 9:
                fields.append(formatted)
            else:
                methods.append(formatted)
    return defined, classes, methods, fields


def scan_jar(jar_path: pathlib.Path, main_class: str) -> ClassReferences:
    parsed_classes: dict[str, tuple[list[str], list[str], list[str]]] = {}
    try:
        with zipfile.ZipFile(jar_path) as archive:
            for name in sorted(archive.namelist()):
                if not name.endswith(".class") or name.startswith("META-INF/versions/"):
                    continue
                parsed = parse_class_file(archive.read(name), f"{jar_path}:{name}")
                parsed_classes[parsed[0]] = (parsed[1], parsed[2], parsed[3])
    except (OSError, zipfile.BadZipFile, KeyError) as exc:
        raise CorpusError(f"cannot scan JAR {jar_path}: {exc}") from exc

    root = main_class.replace(".", "/")
    if root not in parsed_classes:
        raise CorpusError(f"main class {main_class} is not present in {jar_path}")

    reachable: set[str] = set()
    pending = [root]
    while pending:
        current = pending.pop()
        if current in reachable:
            continue
        reachable.add(current)
        class_refs, _method_refs, _field_refs = parsed_classes[current]
        for referenced in class_refs:
            if referenced in parsed_classes and referenced not in reachable:
                pending.append(referenced)

    classes: collections.Counter[str] = collections.Counter()
    methods: collections.Counter[str] = collections.Counter()
    fields: collections.Counter[str] = collections.Counter()
    for name in sorted(reachable):
        class_refs, method_refs, field_refs = parsed_classes[name]
        classes.update(class_refs)
        methods.update(method_refs)
        fields.update(field_refs)
    for class_name in parsed_classes:
        classes.pop(class_name, None)
    return ClassReferences(frozenset(reachable), classes, methods, fields)


def parse_runner_result(path: pathlib.Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def execute_runner(
    runner_tokens: Sequence[str],
    item: Mapping[str, Any],
    jar_path: pathlib.Path,
    run_dir: pathlib.Path,
    timeout_ms: int,
    dimensions: Mapping[str, int],
) -> RunnerExecution:
    run_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = run_dir / "stdout.log"
    stderr_path = run_dir / "stderr.log"
    result_path = run_dir / "runner-result.json"
    frame_path = run_dir / "frame.ppm"
    scenario_path = run_dir / "scenario.json"
    runtime_home = run_dir / "runtime-home"
    runtime_home.mkdir(parents=True, exist_ok=True)
    scenario = {
        "schema_version": SCHEMA_VERSION,
        "item_id": item["id"],
        "main_class": item["main_class"],
        "dimensions": dict(dimensions),
        "input_sequence": item.get("input_sequence", []),
        "expected": item.get("expected", {}),
    }
    write_json(scenario_path, scenario)
    command = [
        *runner_tokens,
        "--jar", str(jar_path),
        "--main", item["main_class"],
        "--runtime-home", str(runtime_home),
        "--result", str(result_path),
        "--frame", str(frame_path),
        "--width", str(dimensions["width"]),
        "--height", str(dimensions["height"]),
    ]
    start = time.monotonic()
    return_code: int | None = None
    timed_out = False
    launch_error = ""
    try:
        with stdout_path.open("wb") as stdout_stream, stderr_path.open("wb") as stderr_stream:
            process = subprocess.run(
                command,
                stdout=stdout_stream,
                stderr=stderr_stream,
                timeout=timeout_ms / 1000.0,
                check=False,
                env={
                    **os.environ,
                    "PHONEME_COMPAT_ITEM_ID": str(item["id"]),
                    "PHONEME_COMPAT_SCENARIO": str(scenario_path),
                },
            )
            return_code = process.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        stderr_path.write_text(
            stderr_path.read_text(encoding="utf-8", errors="replace")
            + f"\ncompatibility runner timed out after {timeout_ms} ms\n",
            encoding="utf-8",
        )
    except OSError as exc:
        launch_error = str(exc)
        stderr_path.write_text(f"runner launch failed: {exc}\n", encoding="utf-8")
    duration_ms = int((time.monotonic() - start) * 1000)
    return RunnerExecution(
        command=list(command),
        return_code=return_code,
        timed_out=timed_out,
        duration_ms=duration_ms,
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        result_path=result_path,
        frame_path=frame_path,
        scenario_path=scenario_path,
        result=parse_runner_result(result_path),
        launch_error=launch_error,
    )


def combined_logs(execution: RunnerExecution | None) -> str:
    if execution is None:
        return ""
    chunks: list[str] = []
    for path in (execution.stdout_path, execution.stderr_path):
        if path.is_file():
            chunks.append(path.read_text(encoding="utf-8", errors="replace"))
    if execution.result:
        chunks.append(json.dumps(execution.result, sort_keys=True))
    return "\n".join(chunks)


def classify_failures(text: str) -> list[dict[str, str]]:
    failures: list[dict[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for kind, pattern in FAILURE_PATTERNS:
        for match in pattern.finditer(text):
            detail = match.group(1).strip() if match.lastindex else match.group(0).strip()
            detail = re.sub(r"\s+", " ", detail)[:300]
            key = (kind, detail)
            if key not in seen:
                seen.add(key)
                failures.append({"kind": kind, "detail": detail})
    return failures


def result_string_list(result: Mapping[str, Any], key: str) -> list[str]:
    value = result.get(key, [])
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if isinstance(item, (str, int, float))]


def observed_data(execution: RunnerExecution | None) -> dict[str, Any]:
    if execution is None:
        return {
            "install": "unknown",
            "startup_ms": None,
            "frames_produced": 0,
            "frame_sha256": "",
            "milestones": [],
            "network_actions": [],
            "media_actions": [],
            "app_state": "unknown",
            "exit_code": None,
        }
    result = execution.result
    logs = combined_logs(execution)
    milestones = set(result_string_list(result, "milestones"))
    milestones.update(MILESTONE_RE.findall(logs))
    network_actions = set(result_string_list(result, "network_actions"))
    network_actions.update(NETWORK_ACTION_RE.findall(logs))
    media_actions = set(result_string_list(result, "media_actions"))
    media_actions.update(MEDIA_ACTION_RE.findall(logs))
    frame_sha = sha256_file(execution.frame_path) if execution.frame_path.is_file() else ""
    frames = result.get("frames_produced", 1 if frame_sha else 0)
    if not isinstance(frames, int) or frames < 0:
        frames = 0
    startup_ms = result.get("startup_ms")
    if not isinstance(startup_ms, int) or startup_ms < 0:
        startup_ms = execution.duration_ms
    install = result.get("install", "success" if result.get("installed") is True else "failure")
    if install not in ("success", "failure"):
        install = "unknown"
    app_state = result.get("app_state", "unknown")
    if not isinstance(app_state, str):
        app_state = "unknown"
    exit_code = result.get("exit_code", execution.return_code)
    if not isinstance(exit_code, int):
        exit_code = execution.return_code
    return {
        "install": install,
        "startup_ms": startup_ms,
        "frames_produced": frames,
        "frame_sha256": frame_sha,
        "milestones": sorted(milestones),
        "network_actions": sorted(network_actions),
        "media_actions": sorted(media_actions),
        "app_state": app_state,
        "exit_code": exit_code,
    }


def evaluate_expectations(
    expected: Mapping[str, Any], observed: Mapping[str, Any], execution: RunnerExecution | None
) -> list[str]:
    reasons: list[str] = []
    install = expected.get("install", "success")
    if install != "any" and observed["install"] != install:
        reasons.append(f"install expected {install}, observed {observed['install']}")
    expected_exit = expected.get("exit", "normal")
    exit_code = observed["exit_code"]
    if expected_exit == "normal" and exit_code != 0:
        reasons.append(f"runner exit expected 0, observed {exit_code}")
    if expected_exit == "nonzero" and (exit_code is None or exit_code == 0):
        reasons.append(f"runner exit expected nonzero, observed {exit_code}")
    if execution is not None and execution.timed_out:
        reasons.append("runner timed out")
    if execution is not None and execution.launch_error:
        reasons.append(f"runner launch failed: {execution.launch_error}")

    startup_ms = observed["startup_ms"]
    max_startup_ms = expected.get("max_startup_ms")
    if isinstance(max_startup_ms, int) and isinstance(startup_ms, int) and startup_ms > max_startup_ms:
        reasons.append(f"startup {startup_ms} ms exceeds {max_startup_ms} ms")
    min_frames = expected.get("min_frames", 0)
    if observed["frames_produced"] < min_frames:
        reasons.append(
            f"frames expected at least {min_frames}, observed {observed['frames_produced']}"
        )
    frame_hashes = set(string_list(expected.get("frame_hashes"), "expected.frame_hashes"))
    if frame_hashes and observed["frame_sha256"] not in frame_hashes:
        reasons.append(
            f"frame hash {observed['frame_sha256'] or '<missing>'} not in golden set"
        )

    for key in ("milestones", "network_actions", "media_actions"):
        wanted = set(string_list(expected.get(key), f"expected.{key}"))
        missing = sorted(wanted.difference(observed[key]))
        if missing:
            reasons.append(f"missing {key}: {', '.join(missing)}")
    expected_state = expected.get("app_state")
    if isinstance(expected_state, str) and expected_state != "any" and observed["app_state"] != expected_state:
        reasons.append(
            f"app state expected {expected_state}, observed {observed['app_state']}"
        )
    return reasons


def expectation_failure_kind(reason: str) -> str:
    lowered = reason.lower()
    if lowered.startswith("install ") or "jar not present" in lowered:
        return "install"
    if lowered.startswith("startup "):
        return "performance"
    if lowered.startswith("frames ") or lowered.startswith("frame hash "):
        return "graphics"
    if lowered.startswith("missing network_actions"):
        return "network"
    if lowered.startswith("missing media_actions"):
        return "media"
    if lowered.startswith("missing milestones"):
        return "milestone"
    if lowered.startswith("app state ") or lowered.startswith("runner exit "):
        return "runtime_state"
    if "timed out" in lowered:
        return "timeout"
    if "sha-256" in lowered:
        return "artifact_integrity"
    return "expectation"


def differential(current: Mapping[str, Any], reference: Mapping[str, Any]) -> dict[str, Any]:
    differences: dict[str, Any] = {}
    for key in ("install", "app_state", "exit_code", "frames_produced", "frame_sha256"):
        if current.get(key) != reference.get(key):
            differences[key] = {"phoneME": current.get(key), "reference": reference.get(key)}
    for key in ("milestones", "network_actions", "media_actions"):
        current_set = set(current.get(key, []))
        reference_set = set(reference.get(key, []))
        if current_set != reference_set:
            differences[key] = {
                "only_phoneME": sorted(current_set - reference_set),
                "only_reference": sorted(reference_set - current_set),
            }
    return {"matches": not differences, "differences": differences}


def execute_item(
    item: dict[str, Any],
    manifest_dir: pathlib.Path,
    output_root: pathlib.Path,
    runner_tokens: Sequence[str] | None,
    reference_tokens: Sequence[str] | None,
    reference_metadata: Mapping[str, str],
    defaults: Mapping[str, Any],
    static_only: bool,
) -> tuple[ItemEvaluation, ClassReferences | None]:
    item_id = item["id"]
    metadata = resolved_item_metadata(item, defaults)
    item_dir = output_root / "items" / item_id
    item_dir.mkdir(parents=True, exist_ok=True)
    jar_path = resolve_path(manifest_dir, item["jar"])
    if not jar_path.is_file():
        status = "FAIL" if item["required"] else "SKIP"
        reason = f"JAR not present: {jar_path}"
        return ItemEvaluation(
            item_id=item_id,
            description=item["description"],
            categories=list(item["categories"]),
            metadata=metadata,
            status=status,
            reasons=[reason],
            failures=[{"kind": "missing_jar", "detail": reason}],
            jar_path=str(jar_path),
            jar_sha256="",
            startup_ms=None,
            frames_produced=0,
            frame_sha256="",
            milestones=[],
            network_actions=[],
            media_actions=[],
            app_state="unknown",
            runner_exit_code=None,
            reference=None,
            differential=None,
            class_reference_count=0,
            method_reference_count=0,
        ), None

    actual_sha = sha256_file(jar_path)
    expected_sha = item.get("sha256", "auto")
    sha_reason = ""
    if isinstance(expected_sha, str) and expected_sha not in ("", "auto") and expected_sha != actual_sha:
        sha_reason = f"SHA-256 expected {expected_sha}, observed {actual_sha}"

    references: ClassReferences | None = None
    scan_failure = ""
    try:
        references = scan_jar(jar_path, item["main_class"])
    except CorpusError as exc:
        scan_failure = str(exc)

    if static_only or runner_tokens is None:
        reasons = [reason for reason in (sha_reason, scan_failure) if reason]
        status = "FAIL" if reasons else "STATIC"
        return ItemEvaluation(
            item_id=item_id,
            description=item["description"],
            categories=list(item["categories"]),
            metadata=metadata,
            status=status,
            reasons=reasons or ["static analysis completed; runtime execution not requested"],
            failures=[{"kind": "static_scan", "detail": reason} for reason in reasons],
            jar_path=str(jar_path),
            jar_sha256=actual_sha,
            startup_ms=None,
            frames_produced=0,
            frame_sha256="",
            milestones=[],
            network_actions=[],
            media_actions=[],
            app_state="not_run",
            runner_exit_code=None,
            reference=None,
            differential=None,
            class_reference_count=sum(references.class_references.values()) if references else 0,
            method_reference_count=sum(references.method_references.values()) if references else 0,
        ), references

    dimensions = dict(defaults.get("dimensions", {"width": 320, "height": 240}))
    dimensions.update(item.get("dimensions", {}))
    timeout_ms = int(item.get("timeout_ms", defaults.get("timeout_ms", 15_000)))
    execution_item = dict(item)
    execution_item["input_sequence"] = metadata["input_sequence"]
    execution = execute_runner(
        runner_tokens,
        execution_item,
        jar_path,
        item_dir / "phoneme",
        timeout_ms,
        dimensions,
    )
    observed = observed_data(execution)
    logs = combined_logs(execution)
    failures = classify_failures(logs)
    reasons = [reason for reason in (sha_reason, scan_failure) if reason]
    reasons.extend(evaluate_expectations(item["expected"], observed, execution))
    existing_failures = {(failure["kind"], failure["detail"]) for failure in failures}
    for reason in reasons:
        classified = (expectation_failure_kind(reason), reason)
        if classified not in existing_failures:
            failures.append({"kind": classified[0], "detail": classified[1]})
            existing_failures.add(classified)

    reference_summary: dict[str, Any] | None = None
    diff_summary: dict[str, Any] | None = None
    if reference_tokens is not None:
        reference_execution = execute_runner(
            reference_tokens,
            execution_item,
            jar_path,
            item_dir / "reference",
            timeout_ms,
            dimensions,
        )
        reference_summary = observed_data(reference_execution)
        reference_summary["runner_return_code"] = reference_execution.return_code
        reference_summary["timed_out"] = reference_execution.timed_out
        item_reference = item.get("reference", {})
        if not isinstance(item_reference, dict):
            item_reference = {}
        reference_summary["runtime"] = reference_metadata.get(
            "runtime", str(item_reference.get("runtime", "unspecified"))
        )
        reference_summary["version"] = reference_metadata.get(
            "version", str(item_reference.get("version", "unspecified"))
        )
        reference_summary["command"] = reference_execution.command
        reference_summary["scenario_path"] = str(reference_execution.scenario_path)
        write_json(
            item_dir / "reference-execution.json",
            {
                "command": reference_execution.command,
                "return_code": reference_execution.return_code,
                "timed_out": reference_execution.timed_out,
                "duration_ms": reference_execution.duration_ms,
                "launch_error": reference_execution.launch_error,
                "runtime": reference_summary["runtime"],
                "version": reference_summary["version"],
                "scenario_path": str(reference_execution.scenario_path),
                "observed": reference_summary,
                "classified_failures": classify_failures(
                    combined_logs(reference_execution)
                ),
            },
        )
        diff_summary = differential(observed, reference_summary)
        if item.get("require_reference_match", False) and not diff_summary["matches"]:
            reasons.append("differential behavior differs from reference runner")

    status = "PASS" if not reasons else "FAIL"
    execution_metadata = {
        "command": execution.command,
        "return_code": execution.return_code,
        "timed_out": execution.timed_out,
        "duration_ms": execution.duration_ms,
        "launch_error": execution.launch_error,
        "scenario_path": str(execution.scenario_path),
        "metadata": metadata,
        "observed": observed,
        "expectation_failures": reasons,
        "classified_failures": failures,
    }
    write_json(item_dir / "execution.json", execution_metadata)

    return ItemEvaluation(
        item_id=item_id,
        description=item["description"],
        categories=list(item["categories"]),
        metadata=metadata,
        status=status,
        reasons=reasons,
        failures=failures,
        jar_path=str(jar_path),
        jar_sha256=actual_sha,
        startup_ms=observed["startup_ms"],
        frames_produced=observed["frames_produced"],
        frame_sha256=observed["frame_sha256"],
        milestones=observed["milestones"],
        network_actions=observed["network_actions"],
        media_actions=observed["media_actions"],
        app_state=observed["app_state"],
        runner_exit_code=observed["exit_code"],
        reference=reference_summary,
        differential=diff_summary,
        class_reference_count=sum(references.class_references.values()) if references else 0,
        method_reference_count=sum(references.method_references.values()) if references else 0,
    ), references


def public_api_name(internal_name: str) -> str:
    return internal_name.replace("/", ".")


def build_coverage(
    evaluations: Sequence[ItemEvaluation], reference_sets: Mapping[str, ClassReferences]
) -> dict[str, Any]:
    class_counts: collections.Counter[str] = collections.Counter()
    method_counts: collections.Counter[str] = collections.Counter()
    field_counts: collections.Counter[str] = collections.Counter()
    class_items: dict[str, set[str]] = collections.defaultdict(set)
    method_items: dict[str, set[str]] = collections.defaultdict(set)
    for evaluation in evaluations:
        references = reference_sets.get(evaluation.item_id)
        if references is None:
            continue
        for name, count in references.class_references.items():
            class_counts[name] += count
            class_items[name].add(evaluation.item_id)
        for name, count in references.method_references.items():
            method_counts[name] += count
            method_items[name].add(evaluation.item_id)
        field_counts.update(references.field_references)

    def is_j2me_api(name: str) -> bool:
        return name.startswith((
            "java/", "javax/microedition/", "com/nokia/", "com/siemens/",
            "com/motorola/", "com/samsung/", "com/sonyericsson/",
        ))

    classes = [
        {
            "class": public_api_name(name),
            "references": count,
            "corpus_items": sorted(class_items[name]),
        }
        for name, count in class_counts.most_common()
        if is_j2me_api(name)
    ]
    methods = [
        {
            "method": public_api_name(name),
            "references": count,
            "corpus_items": sorted(method_items[name]),
        }
        for name, count in method_counts.most_common()
        if is_j2me_api(name.split(".", 1)[0])
    ]

    missing_counter: collections.Counter[str] = collections.Counter()
    verifier_counter: collections.Counter[str] = collections.Counter()
    runtime_counter: collections.Counter[str] = collections.Counter()
    for evaluation in evaluations:
        for failure in evaluation.failures:
            kind = failure["kind"]
            detail = failure["detail"]
            if kind in ("missing_class", "missing_method", "missing_native"):
                missing_counter[f"{kind}: {detail}"] += 1
            elif kind == "verifier":
                verifier_counter[detail] += 1
            else:
                runtime_counter[f"{kind}: {detail}"] += 1

    return {
        "generated_at": utc_now(),
        "class_references": classes,
        "method_references": methods,
        "field_reference_count": sum(field_counts.values()),
        "top_missing_apis": [
            {"failure": name, "affected_items": count}
            for name, count in missing_counter.most_common(50)
        ],
        "top_verifier_failures": [
            {"failure": name, "affected_items": count}
            for name, count in verifier_counter.most_common(25)
        ],
        "top_runtime_failures": [
            {"failure": name, "affected_items": count}
            for name, count in runtime_counter.most_common(25)
        ],
    }


def markdown_escape(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_report(
    manifest_path: pathlib.Path,
    evaluations: Sequence[ItemEvaluation],
    coverage: Mapping[str, Any],
    command_line: Sequence[str],
) -> str:
    counts = collections.Counter(item.status for item in evaluations)
    lines = [
        "# phoneME Compatibility Corpus Report",
        "",
        f"Generated: `{utc_now()}`  ",
        f"Manifest: `{manifest_path}`  ",
        f"Command: `{' '.join(shlex.quote(arg) for arg in command_line)}`",
        "",
        "## Summary",
        "",
        "| PASS | FAIL | SKIP | STATIC | Total |",
        "| ---: | ---: | ---: | ---: | ---: |",
        f"| {counts['PASS']} | {counts['FAIL']} | {counts['SKIP']} | "
        f"{counts['STATIC']} | {len(evaluations)} |",
        "",
        "## Corpus results",
        "",
        "| Item | Status | Categories | Profile | Device | Startup | Frames | App state | Evidence |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- | --- |",
    ]
    for item in evaluations:
        startup = f"{item.startup_ms} ms" if item.startup_ms is not None else "-"
        evidence = "; ".join(item.reasons) if item.reasons else ", ".join(item.milestones)
        profile = (
            f"{item.metadata.get('configuration', '-')}/"
            f"{item.metadata.get('profile', '-')}"
        )
        device_profile = item.metadata.get("device_profile", {})
        if not isinstance(device_profile, dict):
            device_profile = {}
        device = str(device_profile.get("name", "-"))
        resolution = str(device_profile.get("resolution", ""))
        if resolution:
            device += f" ({resolution})"
        lines.append(
            f"| `{markdown_escape(item.item_id)}` | **{item.status}** | "
            f"{markdown_escape(', '.join(item.categories))} | "
            f"{markdown_escape(profile)} | {markdown_escape(device)} | {startup} | "
            f"{item.frames_produced} | {markdown_escape(item.app_state)} | "
            f"{markdown_escape(evidence or '-')} |"
        )

    lines.extend(["", "## Top missing APIs", ""])
    missing = coverage.get("top_missing_apis", [])
    if missing:
        lines.extend(["| Failure | Affected items |", "| --- | ---: |"])
        for entry in missing[:25]:
            lines.append(
                f"| {markdown_escape(entry['failure'])} | {entry['affected_items']} |"
            )
    else:
        lines.append("No missing class/method/native signature was observed in this run.")

    lines.extend(["", "## Top verifier failures", ""])
    verifier = coverage.get("top_verifier_failures", [])
    if verifier:
        lines.extend(["| Failure | Affected items |", "| --- | ---: |"])
        for entry in verifier:
            lines.append(
                f"| {markdown_escape(entry['failure'])} | {entry['affected_items']} |"
            )
    else:
        lines.append("No verifier failure was observed in this run.")

    lines.extend(["", "## Most referenced J2ME API classes", ""])
    class_refs = coverage.get("class_references", [])[:50]
    if class_refs:
        lines.extend(["| Class | References | Corpus items |", "| --- | ---: | --- |"])
        for entry in class_refs:
            lines.append(
                f"| `{markdown_escape(entry['class'])}` | {entry['references']} | "
                f"{markdown_escape(', '.join(entry['corpus_items']))} |"
            )
    else:
        lines.append("No J2ME API class reference was found in available JARs.")

    differential_items = [item for item in evaluations if item.differential is not None]
    if differential_items:
        lines.extend(["", "## Differential results", ""])
        for item in differential_items:
            assert item.differential is not None
            state = "MATCH" if item.differential["matches"] else "DIFF"
            reference = item.reference or {}
            runtime = reference.get("runtime", "unspecified")
            version = reference.get("version", "unspecified")
            lines.append(
                f"- `{item.item_id}`: **{state}** against "
                f"`{markdown_escape(runtime)} {markdown_escape(version)}`"
            )
            if not item.differential["matches"]:
                lines.append(
                    "  - `" + json.dumps(item.differential["differences"], sort_keys=True) + "`"
                )

    lines.extend([
        "",
        "## Reproduction",
        "",
        "Run a subset by item id or category:",
        "",
        "```sh",
        "bash Core/Compatibility/run-corpus.sh --filter canvas",
        "bash Core/Compatibility/run-corpus.sh --filter fixture-rms",
        "```",
        "",
        "A PASS requires every configured observable milestone. A process that merely "
        "avoids crashing is not considered compatible.",
        "",
    ])
    return "\n".join(lines)


def generated_coverage_section(
    evaluations: Sequence[ItemEvaluation], coverage: Mapping[str, Any]
) -> str:
    counts = collections.Counter(item.status for item in evaluations)
    referenced_classes = coverage.get("class_references", [])
    top_classes = referenced_classes[:15]
    lines = [
        GENERATED_BEGIN,
        "## Corpus-derived compatibility evidence",
        "",
        "This section is generated by `Core/Compatibility/analyze-failures.py`. "
        "It reports observed corpus evidence and does not upgrade an API to Working by itself.",
        "",
        f"Last corpus run: `{utc_now()}`",
        "",
        f"- PASS: {counts['PASS']}",
        f"- FAIL: {counts['FAIL']}",
        f"- SKIP: {counts['SKIP']}",
        f"- STATIC-only: {counts['STATIC']}",
        f"- Distinct referenced J2ME API classes: {len(referenced_classes)}",
        "",
    ]
    if top_classes:
        lines.extend([
            "Most frequently referenced classes in the available corpus:",
            "",
            "| Class | References |",
            "| --- | ---: |",
        ])
        for entry in top_classes:
            lines.append(f"| `{entry['class']}` | {entry['references']} |")
        lines.append("")
    missing = coverage.get("top_missing_apis", [])
    if missing:
        lines.extend([
            "Observed missing API failures:",
            "",
            "| Failure | Affected items |",
            "| --- | ---: |",
        ])
        for entry in missing[:15]:
            lines.append(f"| {markdown_escape(entry['failure'])} | {entry['affected_items']} |")
        lines.append("")
    else:
        lines.extend(["No missing class/method/native failure was observed in the latest run.", ""])
    lines.append(GENERATED_END)
    return "\n".join(lines)


def update_coverage_doc(
    path: pathlib.Path, evaluations: Sequence[ItemEvaluation], coverage: Mapping[str, Any]
) -> None:
    section = generated_coverage_section(evaluations, coverage)
    try:
        original = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CorpusError(f"cannot read coverage document {path}: {exc}") from exc
    if GENERATED_BEGIN in original and GENERATED_END in original:
        prefix, remainder = original.split(GENERATED_BEGIN, 1)
        _old, suffix = remainder.split(GENERATED_END, 1)
        updated = prefix.rstrip() + "\n\n" + section + suffix
    else:
        updated = original.rstrip() + "\n\n" + section + "\n"
    path.write_text(updated, encoding="utf-8")


def runner_tokens(value: str | None) -> list[str] | None:
    if value is None or not value.strip():
        return None
    tokens = shlex.split(value)
    if not tokens:
        return None
    executable = command_path(tokens[0])
    return [executable, *tokens[1:]]


def item_to_json(item: ItemEvaluation) -> dict[str, Any]:
    return dataclasses.asdict(item)


def run_command(args: argparse.Namespace) -> int:
    manifest_path = pathlib.Path(args.manifest).resolve()
    manifest = load_json(manifest_path)
    validate_manifest(manifest, manifest_path)
    selected = list(iter_selected_items(manifest, args.filter, args.include_disabled))
    if not selected:
        raise CorpusError("no corpus item matched the requested filter")
    output_root = pathlib.Path(args.output).resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    if not args.no_build:
        build_fixture_jars(selected, manifest_path.parent, output_root / "fixture-build")

    current_runner = None if args.static_only else runner_tokens(
        args.runner or os.environ.get("PHONEME_CORPUS_RUNNER")
    )
    reference_runner = None if args.static_only else runner_tokens(
        args.reference_runner or os.environ.get("PHONEME_REFERENCE_RUNNER")
    )
    reference_metadata = {
        "runtime": args.reference_name
        or os.environ.get("PHONEME_REFERENCE_RUNTIME", ""),
        "version": args.reference_version
        or os.environ.get("PHONEME_REFERENCE_VERSION", ""),
    }
    reference_metadata = {
        key: value for key, value in reference_metadata.items() if value
    }
    evaluations: list[ItemEvaluation] = []
    references: dict[str, ClassReferences] = {}
    for item in selected:
        evaluation, item_references = execute_item(
            item,
            manifest_path.parent,
            output_root,
            current_runner,
            reference_runner,
            reference_metadata,
            manifest.get("defaults", {}),
            args.static_only,
        )
        evaluations.append(evaluation)
        if item_references is not None:
            references[evaluation.item_id] = item_references
        print(f"[{evaluation.status:6}] {evaluation.item_id}")
        for reason in evaluation.reasons:
            print(f"         {reason}")

    coverage = build_coverage(evaluations, references)
    report = {
        "schema_version": SCHEMA_VERSION,
        "generated_at": utc_now(),
        "manifest": str(manifest_path),
        "command": sys.argv,
        "summary": dict(collections.Counter(item.status for item in evaluations)),
        "items": [item_to_json(item) for item in evaluations],
        "coverage": coverage,
    }
    write_json(output_root / "report.json", report)
    write_json(output_root / "api-coverage.json", coverage)
    (output_root / "report.md").write_text(
        render_report(manifest_path, evaluations, coverage, sys.argv),
        encoding="utf-8",
    )
    if args.update_coverage_doc:
        update_coverage_doc(pathlib.Path(args.update_coverage_doc).resolve(), evaluations, coverage)

    failures = sum(item.status == "FAIL" for item in evaluations)
    print(f"Report: {output_root / 'report.md'}")
    return 1 if failures else 0


def list_command(args: argparse.Namespace) -> int:
    manifest_path = pathlib.Path(args.manifest).resolve()
    manifest = load_json(manifest_path)
    validate_manifest(manifest, manifest_path)
    for item in manifest["corpus"]:
        enabled = "enabled" if item["enabled"] else "disabled"
        required = "required" if item["required"] else "optional"
        print(
            f"{item['id']:<32} {enabled:<8} {required:<8} "
            f"{','.join(item['categories'])}"
        )
    return 0


def build_command(args: argparse.Namespace) -> int:
    manifest_path = pathlib.Path(args.manifest).resolve()
    manifest = load_json(manifest_path)
    validate_manifest(manifest, manifest_path)
    selected = list(iter_selected_items(manifest, args.filter, args.include_disabled))
    build_fixture_jars(selected, manifest_path.parent, pathlib.Path(args.output).resolve())
    return 0


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        default=str(pathlib.Path(__file__).with_name("expected-results.json")),
        help="corpus manifest JSON",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="list corpus entries")
    list_parser.set_defaults(handler=list_command)

    build_parser = subparsers.add_parser("build-fixtures", help="build project-authored fixtures")
    build_parser.add_argument("--output", required=True)
    build_parser.add_argument("--filter", action="append", default=[])
    build_parser.add_argument("--include-disabled", action="store_true")
    build_parser.set_defaults(handler=build_command)

    run_parser = subparsers.add_parser("run", help="run and analyze the corpus")
    run_parser.add_argument("--output", required=True)
    run_parser.add_argument("--filter", action="append", default=[])
    run_parser.add_argument("--include-disabled", action="store_true")
    run_parser.add_argument("--runner", help="runner command implementing the harness CLI")
    run_parser.add_argument("--reference-runner", help="optional differential reference runner")
    run_parser.add_argument("--reference-name", help="reference runtime name recorded in evidence")
    run_parser.add_argument("--reference-version", help="reference runtime version recorded in evidence")
    run_parser.add_argument("--static-only", action="store_true")
    run_parser.add_argument("--no-build", action="store_true")
    run_parser.add_argument("--update-coverage-doc")
    run_parser.set_defaults(handler=run_command)
    return parser


def main() -> int:
    parser = create_parser()
    args = parser.parse_args()
    try:
        return int(args.handler(args))
    except CorpusError as exc:
        print(f"compatibility corpus error: {exc}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as exc:
        print(f"compatibility corpus command failed ({exc.returncode}): {exc.cmd}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
