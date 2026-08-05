#!/usr/bin/env python3
"""Compare legacy and decoded CompatibilityHarness corpus reports."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
from typing import Any, Mapping, Sequence

PASSING_STATUSES = {"STARTED", "STARTED_UI", "STARTED_FRAME"}


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read JSON report {path}: {error}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"JSON report is not an object: {path}")
    return value


def write_json(path: pathlib.Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def items_by_path(report: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    items = report.get("items", [])
    if not isinstance(items, list):
        raise RuntimeError("corpus report has no item list")
    indexed: dict[str, Mapping[str, Any]] = {}
    for item in items:
        if not isinstance(item, dict):
            continue
        relative_path = item.get("relative_path")
        if isinstance(relative_path, str) and relative_path:
            indexed[relative_path] = item
    return indexed


def observed(item: Mapping[str, Any]) -> Mapping[str, Any]:
    value = item.get("observed", {})
    return value if isinstance(value, dict) else {}


def failures(item: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    value = item.get("failures", [])
    if not isinstance(value, list):
        return []
    return [entry for entry in value if isinstance(entry, dict)]


def frame_path(item: Mapping[str, Any]) -> pathlib.Path | None:
    artifacts = item.get("artifacts", {})
    if not isinstance(artifacts, dict):
        return None
    run_dir = artifacts.get("run_dir")
    if not isinstance(run_dir, str) or not run_dir:
        return None
    return pathlib.Path(run_dir) / "frame.ppm"


def load_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise RuntimeError(f"cannot read frame {path}: {error}") from error
    header = data.split(b"\n", 3)
    if len(header) != 4 or header[0] != b"P6" or header[2] != b"255":
        raise RuntimeError(f"unsupported PPM frame format: {path}")
    try:
        width_text, height_text = header[1].split()
        width = int(width_text)
        height = int(height_text)
    except (TypeError, ValueError) as error:
        raise RuntimeError(f"invalid PPM dimensions: {path}") from error
    pixels = header[3]
    expected_size = width * height * 3
    if width <= 0 or height <= 0 or len(pixels) != expected_size:
        raise RuntimeError(
            f"PPM payload size mismatch: {path} "
            f"({len(pixels)} != {expected_size})"
        )
    return width, height, pixels


def evaluate_screen_probe(
    probe: Mapping[str, Any],
    item: Mapping[str, Any],
) -> tuple[bool, list[str], dict[str, Any]]:
    errors: list[str] = []
    data = observed(item)
    milestone_values = data.get("milestones", [])
    milestones = {
        str(value) for value in milestone_values
    } if isinstance(milestone_values, list) else set()
    required_milestones = probe.get("required_milestones", [])
    if isinstance(required_milestones, list):
        missing = [
            str(value) for value in required_milestones
            if str(value) not in milestones
        ]
        if missing:
            errors.append("missing milestones: " + ", ".join(missing))

    path = frame_path(item)
    evidence: dict[str, Any] = {
        "frame_path": str(path) if path is not None else "",
        "regions": [],
    }
    if path is None:
        errors.append("frame artifact path is missing")
        return False, errors, evidence

    try:
        width, height, pixels = load_ppm(path)
    except RuntimeError as error:
        errors.append(str(error))
        return False, errors, evidence

    colors = {
        pixels[offset:offset + 3]
        for offset in range(0, len(pixels), 3)
    }
    nonblack_pixels = sum(
        pixels[offset:offset + 3] != b"\x00\x00\x00"
        for offset in range(0, len(pixels), 3)
    )
    frames_produced = int(data.get("frames_produced", 0) or 0)
    evidence.update({
        "width": width,
        "height": height,
        "frames_produced": frames_produced,
        "unique_colors": len(colors),
        "nonblack_pixels": nonblack_pixels,
        "frame_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    })

    thresholds = (
        ("min_frames_produced", frames_produced),
        ("min_unique_colors", len(colors)),
        ("min_nonblack_pixels", nonblack_pixels),
    )
    for key, actual in thresholds:
        expected = probe.get(key)
        if isinstance(expected, int) and actual < expected:
            errors.append(f"{key} not met: {actual} < {expected}")

    regions = probe.get("regions", [])
    if not isinstance(regions, list) or not regions:
        errors.append("screen probe has no region hashes")
        return False, errors, evidence
    for index, region in enumerate(regions):
        if not isinstance(region, dict):
            errors.append(f"region {index} is not an object")
            continue
        values = [region.get(key) for key in ("x", "y", "width", "height")]
        if not all(isinstance(value, int) for value in values):
            errors.append(f"region {index} has invalid coordinates")
            continue
        x, y, region_width, region_height = values
        if (x < 0 or y < 0 or region_width <= 0 or region_height <= 0 or
                x + region_width > width or y + region_height > height):
            errors.append(f"region {index} is outside the frame")
            continue
        region_bytes = b"".join(
            pixels[(row * width + x) * 3:
                   (row * width + x + region_width) * 3]
            for row in range(y, y + region_height)
        )
        actual_hash = hashlib.sha256(region_bytes).hexdigest()
        expected_hash = region.get("sha256")
        evidence["regions"].append({
            "x": x,
            "y": y,
            "width": region_width,
            "height": region_height,
            "expected_sha256": expected_hash,
            "actual_sha256": actual_hash,
            "matched": actual_hash == expected_hash,
        })
        if not isinstance(expected_hash, str) or actual_hash != expected_hash:
            errors.append(f"region {index} hash mismatch")

    return not errors, errors, evidence


def startup_surface_reached(item: Mapping[str, Any]) -> bool:
    status = str(item.get("status", ""))
    data = observed(item)
    if status not in PASSING_STATUSES:
        return False
    if data.get("install") != "success":
        return False
    if data.get("app_state") not in {"active", "paused", "destroyed"}:
        return False
    if int(data.get("frames_produced", 0) or 0) > 0:
        return True
    milestones = data.get("milestones", [])
    return isinstance(milestones, list) and any(
        str(value).startswith(("canvas-", "lcdui-", "ui-"))
        for value in milestones
    )


def automated_milestone_reached(
    target_milestone: str,
    item: Mapping[str, Any],
    exact_screen_probe_available: bool = False,
) -> tuple[bool, bool]:
    data = observed(item)
    frames = int(data.get("frames_produced", 0) or 0)
    status = str(item.get("status", ""))
    if target_milestone == "first-interactive-frame":
        return frames > 0 and status == "STARTED_FRAME", False
    if target_milestone == "first-displayable":
        return startup_surface_reached(item), False
    if target_milestone in {"login-screen", "connection-screen"}:
        return startup_surface_reached(item), not exact_screen_probe_available
    return startup_surface_reached(item), True


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare decoded OFF/ON JAR corpus smoke reports."
    )
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--legacy", type=pathlib.Path, required=True)
    parser.add_argument("--decoded", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_arguments(sys.argv[1:] if argv is None else argv)
    manifest = load_json(args.manifest)
    legacy_report = load_json(args.legacy)
    decoded_report = load_json(args.decoded)
    legacy_items = items_by_path(legacy_report)
    decoded_items = items_by_path(decoded_report)

    benchmarks = manifest.get("benchmarks", [])
    if not isinstance(benchmarks, list):
        raise RuntimeError("benchmark manifest has no benchmark list")

    compared: list[dict[str, Any]] = []
    overall_passed = True
    for benchmark in benchmarks:
        if not isinstance(benchmark, dict):
            continue
        manifest_jar = str(benchmark.get("jar", ""))
        relative_path = pathlib.PurePosixPath(manifest_jar).name
        target_milestone = str(benchmark.get("target_milestone", ""))
        raw_screen_probe = benchmark.get("screen_probe")
        screen_probe = raw_screen_probe if isinstance(raw_screen_probe, dict) else None
        legacy = legacy_items.get(relative_path)
        decoded = decoded_items.get(relative_path)
        errors: list[str] = []
        if legacy is None:
            errors.append("legacy report is missing the JAR")
        if decoded is None:
            errors.append("decoded report is missing the JAR")

        legacy_reached = False
        decoded_reached = False
        legacy_screen_probe_passed: bool | None = None
        decoded_screen_probe_passed: bool | None = None
        legacy_screen_probe: dict[str, Any] = {}
        decoded_screen_probe: dict[str, Any] = {}
        manual_required = False
        if legacy is not None and decoded is not None:
            legacy_status = str(legacy.get("status", ""))
            decoded_status = str(decoded.get("status", ""))
            if legacy_status != decoded_status:
                errors.append(
                    f"status differs: legacy={legacy_status}, decoded={decoded_status}"
                )
            if failures(legacy):
                errors.append("legacy run contains classified failures")
            if failures(decoded):
                errors.append("decoded run contains classified failures")
            exact_screen_probe_available = screen_probe is not None
            legacy_reached, legacy_manual = automated_milestone_reached(
                target_milestone, legacy, exact_screen_probe_available
            )
            decoded_reached, decoded_manual = automated_milestone_reached(
                target_milestone, decoded, exact_screen_probe_available
            )
            if screen_probe is not None:
                (legacy_screen_probe_passed,
                 legacy_probe_errors,
                 legacy_screen_probe) = evaluate_screen_probe(screen_probe, legacy)
                (decoded_screen_probe_passed,
                 decoded_probe_errors,
                 decoded_screen_probe) = evaluate_screen_probe(screen_probe, decoded)
                legacy_reached = legacy_reached and legacy_screen_probe_passed
                decoded_reached = decoded_reached and decoded_screen_probe_passed
                errors.extend(
                    f"legacy screen probe: {error}"
                    for error in legacy_probe_errors
                )
                errors.extend(
                    f"decoded screen probe: {error}"
                    for error in decoded_probe_errors
                )
            manual_required = legacy_manual or decoded_manual
            if not legacy_reached:
                errors.append("legacy run did not reach the automated milestone")
            if not decoded_reached:
                errors.append("decoded run did not reach the automated milestone")

        item_passed = not errors
        overall_passed = overall_passed and item_passed
        legacy_observed = observed(legacy or {})
        decoded_observed = observed(decoded or {})
        compared.append(
            {
                "id": benchmark.get("id", ""),
                "jar": manifest_jar,
                "target_milestone": target_milestone,
                "passed": item_passed,
                "errors": errors,
                "manual_exact_milestone_verification_required": manual_required,
                "legacy": {
                    "status": (legacy or {}).get("status", "MISSING"),
                    "startup_ms": legacy_observed.get("startup_ms"),
                    "frames_produced": legacy_observed.get("frames_produced", 0),
                    "frame_sha256": legacy_observed.get("frame_sha256", ""),
                    "automated_milestone_reached": legacy_reached,
                    "screen_probe_passed": legacy_screen_probe_passed,
                    "screen_probe": legacy_screen_probe,
                },
                "decoded": {
                    "status": (decoded or {}).get("status", "MISSING"),
                    "startup_ms": decoded_observed.get("startup_ms"),
                    "frames_produced": decoded_observed.get("frames_produced", 0),
                    "frame_sha256": decoded_observed.get("frame_sha256", ""),
                    "automated_milestone_reached": decoded_reached,
                    "screen_probe_passed": decoded_screen_probe_passed,
                    "screen_probe": decoded_screen_probe,
                },
                "frame_hash_equal": (
                    legacy_observed.get("frame_sha256", "")
                    == decoded_observed.get("frame_sha256", "")
                ),
            }
        )

    result = {
        "schema_version": 1,
        "passed": overall_passed,
        "manifest": str(args.manifest),
        "legacy_report": str(args.legacy),
        "decoded_report": str(args.decoded),
        "benchmarks": compared,
        "manual_verification_required": any(
            item["manual_exact_milestone_verification_required"]
            for item in compared
        ),
    }
    write_json(args.output, result)
    print(
        f"Decoded corpus comparison: "
        f"{sum(1 for item in compared if item['passed'])}/{len(compared)} passed"
    )
    print(f"Report: {args.output}")
    return 0 if overall_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
