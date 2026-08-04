#!/usr/bin/env python3
"""Merge native-handler telemetry into a strict conformance inventory.

The OpenJDK classification TSV is the canonical registry snapshot because it
contains every registered native signature, including non-OpenJDK namespaces.
Additional coverage TSV files contribute invocation counts from phoneME,
platform, vendor, or internal-contract fixtures.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


Signature = tuple[str, str, str]


@dataclass
class Entry:
    owner: str
    name: str
    descriptor: str
    openjdk_status: str
    declaring_class: str
    counts: dict[str, int] = field(default_factory=dict)

    @property
    def signature(self) -> Signature:
        return (self.owner, self.name, self.descriptor)

    @property
    def total_invocations(self) -> int:
        return sum(self.counts.values())

    @property
    def covered(self) -> bool:
        return self.total_invocations > 0

    @property
    def oracle_domain(self) -> str:
        if self.openjdk_status.startswith("OPENJDK8_"):
            return "openjdk8"
        owner = self.owner
        if owner.startswith("java/"):
            return "java-mobile-extension"
        if owner.startswith((
            "javax/microedition/",
            "com/sun/midp/",
            "com/sun/cldc/",
        )):
            return "javame-reference"
        if owner.startswith((
            "javax/bluetooth/",
            "javax/obex/",
            "javax/wireless/",
            "javax/microedition/",
            "org/xml/sax/",
        )):
            return "jsr-reference"
        if owner.startswith((
            "com/mascotcapsule/",
            "com/nokia/",
            "com/siemens/",
            "com/samsung/",
            "com/motorola/",
            "com/vodafone/",
            "com/j_phone/",
        )):
            return "vendor-reference"
        if owner.startswith("phoneme/"):
            return "internal-contract"
        return "platform-or-vendor-contract"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Audit exact native-handler conformance coverage")
    parser.add_argument(
        "--openjdk-classification",
        type=Path,
        required=True,
        help="openjdk8-handler-signatures.tsv from test-vm-differential.sh",
    )
    parser.add_argument(
        "--coverage",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="additional owner/name/descriptor/invocations TSV (repeatable)",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="exit non-zero while any registered signature is uncovered",
    )
    return parser.parse_args()


def require_columns(path: Path, fieldnames: Iterable[str] | None,
                    required: set[str]) -> None:
    actual = set(fieldnames or [])
    missing = required - actual
    if missing:
        raise ValueError(
            f"{path}: missing TSV columns: {', '.join(sorted(missing))}")


def read_registry(path: Path) -> dict[Signature, Entry]:
    entries: dict[Signature, Entry] = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        require_columns(path, reader.fieldnames, {
            "owner", "name", "descriptor", "invocations", "status",
            "declaring_class",
        })
        for line_number, row in enumerate(reader, start=2):
            signature = (row["owner"], row["name"], row["descriptor"])
            if signature in entries:
                raise ValueError(
                    f"{path}:{line_number}: duplicate signature {signature}")
            try:
                invocations = int(row["invocations"])
            except ValueError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid invocation count") from error
            if invocations < 0:
                raise ValueError(
                    f"{path}:{line_number}: negative invocation count")
            entry = Entry(
                owner=row["owner"],
                name=row["name"],
                descriptor=row["descriptor"],
                openjdk_status=row["status"],
                declaring_class=row["declaring_class"],
                counts={"openjdk8-differential": invocations},
            )
            entries[signature] = entry
    if not entries:
        raise ValueError(f"{path}: registry snapshot is empty")
    return entries


def parse_coverage_argument(argument: str) -> tuple[str, Path]:
    name, separator, raw_path = argument.partition("=")
    if not separator or not name or not raw_path:
        raise ValueError(
            f"invalid --coverage value {argument!r}; expected NAME=PATH")
    if any(character in name for character in "\t\r\n"):
        raise ValueError(f"invalid coverage source name {name!r}")
    return name, Path(raw_path)


def merge_coverage(entries: dict[Signature, Entry], source: str,
                   path: Path) -> None:
    seen: set[Signature] = set()
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        require_columns(path, reader.fieldnames, {
            "owner", "name", "descriptor", "invocations",
        })
        for line_number, row in enumerate(reader, start=2):
            signature = (row["owner"], row["name"], row["descriptor"])
            if signature in seen:
                raise ValueError(
                    f"{path}:{line_number}: duplicate signature {signature}")
            seen.add(signature)
            entry = entries.get(signature)
            if entry is None:
                raise ValueError(
                    f"{path}:{line_number}: signature is absent from registry "
                    f"snapshot: {signature}")
            try:
                invocations = int(row["invocations"])
            except ValueError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid invocation count") from error
            if invocations < 0:
                raise ValueError(
                    f"{path}:{line_number}: negative invocation count")
            entry.counts[source] = invocations


def markdown_report(entries: list[Entry], sources: list[str]) -> str:
    domains: dict[str, list[Entry]] = defaultdict(list)
    for entry in entries:
        domains[entry.oracle_domain].append(entry)

    covered = sum(entry.covered for entry in entries)
    lines = [
        "# Native Conformance Coverage",
        "",
        f"- Registered exact signatures: **{len(entries)}**",
        f"- Oracle-backed invoked signatures: **{covered}**",
        f"- Uncovered signatures: **{len(entries) - covered}**",
        f"- Coverage: **{covered / len(entries) * 100.0:.2f}%**",
        "",
        "## Coverage by oracle domain",
        "",
        "| Domain | Covered | Total | Coverage |",
        "|---|---:|---:|---:|",
    ]
    for domain in sorted(domains):
        group = domains[domain]
        hit = sum(entry.covered for entry in group)
        lines.append(
            f"| `{domain}` | {hit} | {len(group)} | "
            f"{hit / len(group) * 100.0:.2f}% |")

    lines.extend([
        "",
        "## Invocation sources",
        "",
        "| Source | Invoked signatures | Calls |",
        "|---|---:|---:|",
    ])
    for source in sources:
        invoked = sum(entry.counts.get(source, 0) > 0 for entry in entries)
        calls = sum(entry.counts.get(source, 0) for entry in entries)
        lines.append(f"| `{source}` | {invoked} | {calls} |")

    uncovered = [entry for entry in entries if not entry.covered]
    lines.extend([
        "",
        "## Uncovered exact signatures",
        "",
    ])
    if not uncovered:
        lines.append("All registered signatures have oracle-backed runtime coverage.")
    else:
        lines.append("```text")
        for entry in uncovered:
            lines.append(
                f"{entry.oracle_domain}\t{entry.owner}.{entry.name}"
                f"{entry.descriptor}")
        lines.append("```")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    arguments = parse_args()
    try:
        entries_by_signature = read_registry(arguments.openjdk_classification)
        coverage_sources = ["openjdk8-differential"]
        for raw_coverage in arguments.coverage:
            source, path = parse_coverage_argument(raw_coverage)
            if source in coverage_sources:
                raise ValueError(f"duplicate coverage source name: {source}")
            merge_coverage(entries_by_signature, source, path)
            coverage_sources.append(source)
    except (OSError, ValueError) as error:
        print(f"native conformance audit error: {error}", file=sys.stderr)
        return 2

    entries = sorted(
        entries_by_signature.values(),
        key=lambda entry: entry.signature,
    )
    domain_totals = Counter(entry.oracle_domain for entry in entries)
    domain_covered = Counter(
        entry.oracle_domain for entry in entries if entry.covered)
    covered = sum(entry.covered for entry in entries)
    report = {
        "schema": 1,
        "registry_signatures": len(entries),
        "covered_signatures": covered,
        "uncovered_signatures": len(entries) - covered,
        "coverage_percent": covered / len(entries) * 100.0,
        "complete": covered == len(entries),
        "sources": {
            source: {
                "invoked_signatures": sum(
                    entry.counts.get(source, 0) > 0 for entry in entries),
                "invocations": sum(
                    entry.counts.get(source, 0) for entry in entries),
            }
            for source in coverage_sources
        },
        "domains": {
            domain: {
                "covered": domain_covered[domain],
                "total": total,
            }
            for domain, total in sorted(domain_totals.items())
        },
        "uncovered": [
            {
                "domain": entry.oracle_domain,
                "owner": entry.owner,
                "name": entry.name,
                "descriptor": entry.descriptor,
                "openjdk_status": entry.openjdk_status,
            }
            for entry in entries if not entry.covered
        ],
    }

    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    json_path = arguments.output_dir / "native-conformance-coverage.json"
    markdown_path = arguments.output_dir / "native-conformance-coverage.md"
    json_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    markdown_path.write_text(
        markdown_report(entries, coverage_sources),
        encoding="utf-8",
    )

    print(
        f"Native conformance coverage: {covered}/{len(entries)} "
        f"({report['coverage_percent']:.2f}%)")
    print(f"JSON report: {json_path}")
    print(f"Markdown report: {markdown_path}")
    if arguments.require_complete and not report["complete"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
