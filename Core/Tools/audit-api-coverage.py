#!/usr/bin/env python3
"""Audit external J2ME API references against Core's builtin class registry.

The report distinguishes application-provided classes from genuinely external
classes and uses the compiled builtin registry as the source of truth. This
avoids false positives from merely grepping class names in C++ source files.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import importlib.util
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from typing import Any, Iterable, Sequence

SCRIPT_PATH = pathlib.Path(__file__).resolve()
CORE_ROOT = SCRIPT_PATH.parent.parent
PROJECT_ROOT = CORE_ROOT.parent
DEFAULT_JAR_ROOT = PROJECT_ROOT / "jar_test"
# These symbols are constant-pool bootstrap metadata for Java 8 lambdas. The
# interpreter resolves LambdaMetafactory call sites directly; applications do
# not link or invoke these classes as ordinary runtime APIs.
VM_BOOTSTRAP_METADATA_CLASSES = {
    "java/lang/invoke/LambdaMetafactory",
    "java/lang/invoke/MethodHandles",
    "java/lang/invoke/MethodHandles$Lookup",
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
    "org/xml/sax/",
)


@dataclass
class ReferenceDemand:
    references: int = 0
    jars: set[str] = field(default_factory=set)
    samples: list[str] = field(default_factory=list)

    def add(self, count: int, jar: str, sample_limit: int = 8) -> None:
        self.references += count
        self.jars.add(jar)
        if jar not in self.samples and len(self.samples) < sample_limit:
            self.samples.append(jar)


def load_module(path: pathlib.Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load Python module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


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
    compiler = shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        raise RuntimeError("no C++ compiler was found")
    return [compiler], []


def build_builtin_checker(build_root: pathlib.Path) -> pathlib.Path:
    source = build_root / "BuiltinCoverageChecker.cpp"
    binary = build_root / "BuiltinCoverageChecker"
    source.write_text(
        """#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "phoneme/vm/BuiltinClasses.hpp"

namespace {

bool has_method(std::string_view owner,
                std::string_view name,
                std::string_view descriptor,
                std::unordered_set<std::string>& visited) {
    if (!visited.emplace(owner).second) return false;
    auto loaded = phoneme::vm::load_builtin_class(owner);
    if (!loaded) return false;
    for (const auto& method : (*loaded)->methods()) {
        if (method.name == name && method.descriptor == descriptor) return true;
    }
    if (name == "<init>" || name == "<clinit>") return false;
    if (!(*loaded)->super_name().empty() &&
        has_method((*loaded)->super_name(), name, descriptor, visited)) {
        return true;
    }
    for (const std::string& interface_name : (*loaded)->interfaces()) {
        if (has_method(interface_name, name, descriptor, visited)) return true;
    }
    return false;
}

bool has_field(std::string_view owner,
               std::string_view name,
               std::string_view descriptor,
               std::unordered_set<std::string>& visited) {
    if (!visited.emplace(owner).second) return false;
    auto loaded = phoneme::vm::load_builtin_class(owner);
    if (!loaded) return false;
    for (const auto& field : (*loaded)->fields()) {
        if (field.name == name && field.descriptor == descriptor) return true;
    }
    if (!(*loaded)->super_name().empty() &&
        has_field((*loaded)->super_name(), name, descriptor, visited)) {
        return true;
    }
    for (const std::string& interface_name : (*loaded)->interfaces()) {
        if (has_field(interface_name, name, descriptor, visited)) return true;
    }
    return false;
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto first = line.find('\t');
        if (first == std::string::npos) {
            std::cout << '0' << '\\n';
            continue;
        }
        const std::string_view kind(line.data(), first);
        const std::string_view payload(line.data() + first + 1U,
                                       line.size() - first - 1U);
        bool found = false;
        if (kind == "C") {
            found = phoneme::vm::is_builtin_class(payload);
        } else {
            const auto second = payload.find('\t');
            const auto third = second == std::string_view::npos
                ? std::string_view::npos
                : payload.find('\t', second + 1U);
            if (second != std::string_view::npos &&
                third != std::string_view::npos) {
                const auto owner = payload.substr(0U, second);
                const auto name = payload.substr(second + 1U,
                                                 third - second - 1U);
                const auto descriptor = payload.substr(third + 1U);
                std::unordered_set<std::string> visited;
                found = kind == "M"
                    ? has_method(owner, name, descriptor, visited)
                    : (kind == "F"
                        ? has_field(owner, name, descriptor, visited)
                        : false);
            }
        }
        std::cout << (found ? '1' : '0') << '\\n';
    }
    return 0;
}
""",
        encoding="utf-8",
    )
    compiler, sdk_flags = compiler_command()
    builtin_sources = [
        path
        for path in sorted((CORE_ROOT / "src" / "vm").glob("*BuiltinClasses.cpp"))
        if path.name != "BuiltinClasses.cpp"
    ]
    command = [
        *compiler,
        "-std=c++23",
        *sdk_flags,
        f"-I{CORE_ROOT / 'include'}",
        f"-I{CORE_ROOT / 'src' / 'vm'}",
        "-fno-exceptions",
        "-fno-rtti",
        str(source),
        str(CORE_ROOT / "src" / "classfile" / "ClassFile.cpp"),
        str(CORE_ROOT / "src" / "classfile" / "BytecodeVerifier.cpp"),
        str(CORE_ROOT / "src" / "vm" / "BuiltinClassRegistry.cpp"),
        str(CORE_ROOT / "src" / "vm" / "BuiltinClasses.cpp"),
        *(str(path) for path in builtin_sources),
        "-o",
        str(binary),
    ]
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        detail = (completed.stdout + "\n" + completed.stderr).strip()
        raise RuntimeError(f"builtin checker build failed:\n{detail}")
    return binary


def builtin_status(binary: pathlib.Path, queries: Sequence[str]) -> dict[str, bool]:
    completed = subprocess.run(
        [str(binary)],
        input="".join(f"{query}\n" for query in queries),
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "builtin checker failed")
    lines = completed.stdout.splitlines()
    if len(lines) != len(queries):
        raise RuntimeError("builtin checker returned an unexpected result count")
    return {query: line == "1" for query, line in zip(queries, lines)}


def is_field_descriptor(value: str) -> bool:
    if value in {"B", "C", "D", "F", "I", "J", "S", "Z"}:
        return True
    if value.startswith("["):
        return is_field_descriptor(value[1:])
    return value.startswith("L") and value.endswith(";") and len(value) > 2


def is_unqualified_field_name(value: str) -> bool:
    return bool(value) and not any(character in value for character in ".;[/<>")


def member_parts(reference: str) -> tuple[str, str, str]:
    owner, separator, member = reference.partition(".")
    if not separator:
        raise ValueError(f"invalid member reference: {reference}")
    descriptor_start = member.find("(")
    if descriptor_start >= 0:
        return owner, member[:descriptor_start], member[descriptor_start:]
    for descriptor_start in range(len(member) - 1, 0, -1):
        name = member[:descriptor_start]
        descriptor = member[descriptor_start:]
        if is_unqualified_field_name(name) and is_field_descriptor(descriptor):
            return owner, name, descriptor
    raise ValueError(f"invalid member descriptor: {reference}")


def member_query(kind: str, reference: str) -> str:
    owner, name, descriptor = member_parts(reference)
    return f"{kind}\t{owner}\t{name}\t{descriptor}"


def field_member_queries(reference: str) -> list[str]:
    owner, name, descriptor = member_parts(reference)
    return [f"F\t{owner}\t{name}\t{descriptor}"]


def jar_classes(path: pathlib.Path) -> set[str]:
    with zipfile.ZipFile(path) as archive:
        return {
            name[:-6]
            for name in archive.namelist()
            if name.endswith(".class") and not name.startswith("META-INF/versions/")
        }


def is_api_reference(name: str) -> bool:
    return name.startswith(API_PREFIXES)


def markdown_escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def render_table(
    title: str,
    rows: Iterable[tuple[str, ReferenceDemand]],
    symbol_label: str = "Symbol",
) -> list[str]:
    lines = [
        f"## {title}",
        "",
        f"| {symbol_label} | References | JARs | Samples |",
        "| --- | ---: | ---: | --- |",
    ]
    row_count = 0
    for name, demand in rows:
        row_count += 1
        samples = ", ".join(markdown_escape(sample) for sample in demand.samples)
        lines.append(
            f"| `{markdown_escape(name)}` | {demand.references} | "
            f"{len(demand.jars)} | {samples} |"
        )
    if row_count == 0:
        lines.append("| _None_ | 0 | 0 | |")
    lines.append("")
    return lines


def sorted_demands(values: dict[str, ReferenceDemand]) -> list[tuple[str, ReferenceDemand]]:
    return sorted(
        values.items(),
        key=lambda item: (-len(item[1].jars), -item[1].references, item[0]),
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare J2ME API references with Core's builtin classes."
    )
    parser.add_argument("--jar-dir", type=pathlib.Path, default=DEFAULT_JAR_ROOT)
    parser.add_argument("--output", type=pathlib.Path,
                        default=CORE_ROOT / "build" / "api-coverage-audit")
    parser.add_argument("--jobs", type=int, default=max(1, min(12, os.cpu_count() or 1)))
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--all-midlets", action="store_true")
    args = parser.parse_args()

    jar_root = args.jar_dir.resolve()
    if not jar_root.is_dir():
        print(f"JAR directory does not exist: {jar_root}", file=sys.stderr)
        return 2
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    batch = load_module(SCRIPT_PATH.parent / "test-jar-directory.py",
                        "phoneme_batch_audit")
    compat = load_module(CORE_ROOT / "Compatibility" / "analyze-failures.py",
                         "phoneme_compat_audit")
    targets, issues, discovered = batch.discover_targets(
        jar_root, [], args.limit, args.all_midlets
    )
    print(
        f"Discovered {discovered} JARs, {len(targets)} MIDlet targets, "
        f"{len(issues)} metadata issues."
    )

    class_cache: dict[pathlib.Path, set[str]] = {}
    class_cache_lock = __import__("threading").Lock()

    def inspect(
        target: Any,
    ) -> tuple[Any, dict[str, int], dict[str, int], dict[str, int], set[str], str]:
        try:
            references = compat.scan_jar(target.jar_path, target.main_class)
            with class_cache_lock:
                provided = class_cache.get(target.jar_path)
            if provided is None:
                provided = jar_classes(target.jar_path)
                with class_cache_lock:
                    class_cache[target.jar_path] = provided
            return (
                target,
                dict(references.class_references),
                dict(references.method_references),
                dict(references.field_references),
                provided,
                "",
            )
        except Exception as exc:  # Keep the corpus audit alive.
            return target, {}, {}, {}, set(), str(exc)

    scanned: list[
        tuple[Any, dict[str, int], dict[str, int], dict[str, int], set[str], str]
    ] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for index, result in enumerate(pool.map(inspect, targets), start=1):
            scanned.append(result)
            if index == len(targets) or index % 250 == 0:
                print(f"Scanned {index}/{len(targets)} targets")

    external_class_demands: dict[str, ReferenceDemand] = collections.defaultdict(ReferenceDemand)
    external_method_demands: dict[str, ReferenceDemand] = collections.defaultdict(ReferenceDemand)
    external_field_demands: dict[str, ReferenceDemand] = collections.defaultdict(ReferenceDemand)
    app_provided_demands: dict[str, ReferenceDemand] = collections.defaultdict(ReferenceDemand)
    scan_errors: list[dict[str, str]] = []
    for target, classes, methods, fields, provided, error in scanned:
        if error:
            scan_errors.append({"jar": target.relative_path, "error": error})
            continue
        for name, count in classes.items():
            if (not is_api_reference(name) or
                    name in VM_BOOTSTRAP_METADATA_CLASSES):
                continue
            destination = (
                app_provided_demands if name in provided else external_class_demands
            )
            destination[name].add(count, target.relative_path)
        for reference, count in methods.items():
            owner = reference.partition(".")[0]
            if (is_api_reference(owner) and owner not in provided and
                    owner not in VM_BOOTSTRAP_METADATA_CLASSES):
                external_method_demands[reference].add(count, target.relative_path)
        for reference, count in fields.items():
            owner = reference.partition(".")[0]
            if is_api_reference(owner) and owner not in provided:
                external_field_demands[reference].add(count, target.relative_path)

    class_queries = {
        name: f"C\t{name}" for name in sorted(external_class_demands)
    }
    method_queries = {
        reference: member_query("M", reference)
        for reference in sorted(external_method_demands)
    }
    field_queries = {
        reference: field_member_queries(reference)
        for reference in sorted(external_field_demands)
    }
    all_queries = [
        *class_queries.values(),
        *method_queries.values(),
        *(query for queries in field_queries.values() for query in queries),
    ]
    with tempfile.TemporaryDirectory(prefix="phoneme-api-coverage-") as temporary:
        checker = build_builtin_checker(pathlib.Path(temporary))
        status = builtin_status(checker, all_queries)

    def partition_demands(
        demands: dict[str, ReferenceDemand],
        queries: dict[str, str],
    ) -> tuple[dict[str, ReferenceDemand], dict[str, ReferenceDemand]]:
        builtin: dict[str, ReferenceDemand] = {}
        missing: dict[str, ReferenceDemand] = {}
        for name, demand in demands.items():
            destination = builtin if status.get(queries[name], False) else missing
            destination[name] = demand
        return builtin, missing

    builtin_classes, missing_classes = partition_demands(
        external_class_demands, class_queries
    )
    builtin_methods, missing_methods = partition_demands(
        external_method_demands, method_queries
    )
    builtin_fields: dict[str, ReferenceDemand] = {}
    missing_fields: dict[str, ReferenceDemand] = {}
    for name, demand in external_field_demands.items():
        destination = (
            builtin_fields
            if any(status.get(query, False) for query in field_queries[name])
            else missing_fields
        )
        destination[name] = demand

    def serialized_demands(
        demands: dict[str, ReferenceDemand],
        include_jars: bool,
    ) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for name, demand in sorted_demands(demands):
            row: dict[str, Any] = {
                "name": name,
                "references": demand.references,
                "jar_count": len(demand.jars),
            }
            if include_jars:
                row["jars"] = sorted(demand.jars)
            rows.append(row)
        return rows

    result = {
        "summary": {
            "jar_files_discovered": discovered,
            "midlet_targets": len(targets),
            "metadata_issues": len(issues),
            "scan_errors": len(scan_errors),
            "unique_external_api_classes": len(external_class_demands),
            "builtin_api_classes": len(builtin_classes),
            "missing_external_api_classes": len(missing_classes),
            "unique_external_api_methods": len(external_method_demands),
            "builtin_api_methods": len(builtin_methods),
            "missing_external_api_methods": len(missing_methods),
            "unique_external_api_fields": len(external_field_demands),
            "builtin_api_fields": len(builtin_fields),
            "missing_external_api_fields": len(missing_fields),
            "application_provided_api_classes": len(app_provided_demands),
        },
        "missing_external_api_classes": serialized_demands(
            missing_classes, include_jars=True
        ),
        "missing_external_api_methods": serialized_demands(
            missing_methods, include_jars=True
        ),
        "missing_external_api_fields": serialized_demands(
            missing_fields, include_jars=True
        ),
        "builtin_api_classes": serialized_demands(
            builtin_classes, include_jars=False
        ),
        "builtin_api_methods": serialized_demands(
            builtin_methods, include_jars=False
        ),
        "builtin_api_fields": serialized_demands(
            builtin_fields, include_jars=False
        ),
        "application_provided_api_classes": serialized_demands(
            app_provided_demands, include_jars=True
        ),
        "metadata_issues": [
            {"jar": issue.relative_path, "kind": issue.kind, "detail": issue.detail}
            for issue in issues
        ],
        "scan_errors": scan_errors,
    }
    json_path = output / "api-coverage-audit.json"
    json_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                         encoding="utf-8")

    summary = result["summary"]
    report = [
        "# phoneME C++ API Coverage Audit",
        "",
        f"JAR root: `{jar_root}`",
        "",
        "## Summary",
        "",
        "| Metric | Count |",
        "| --- | ---: |",
    ]
    for key, value in summary.items():
        report.append(f"| `{key}` | {value} |")
    report.append("")
    report.extend(render_table(
        "Missing external API classes", sorted_demands(missing_classes), "Class"
    ))
    report.extend(render_table(
        "Missing external API methods", sorted_demands(missing_methods), "Method"
    ))
    report.extend(render_table(
        "Missing external API fields", sorted_demands(missing_fields), "Field"
    ))
    report.extend(render_table(
        "Application-provided API classes",
        sorted_demands(app_provided_demands),
        "Class",
    ))
    report.extend(render_table(
        "Builtin API classes", sorted_demands(builtin_classes), "Class"
    ))
    report.extend(render_table(
        "Builtin API methods", sorted_demands(builtin_methods), "Method"
    ))
    report.extend(render_table(
        "Builtin API fields", sorted_demands(builtin_fields), "Field"
    ))
    report_path = output / "api-coverage-audit.md"
    report_path.write_text("\n".join(report), encoding="utf-8")

    print(f"Report: {report_path}")
    print(f"JSON:   {json_path}")
    has_missing = bool(missing_classes or missing_methods or missing_fields)
    return 1 if has_missing or scan_errors or issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
