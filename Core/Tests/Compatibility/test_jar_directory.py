#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile
import unittest
import zipfile

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[3]
TOOL_PATH = PROJECT_ROOT / "Core" / "Tools" / "test-jar-directory.py"

spec = importlib.util.spec_from_file_location("phoneme_jar_directory_tool", TOOL_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {TOOL_PATH}")
tool = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = tool
spec.loader.exec_module(tool)


class ManifestParsingTests(unittest.TestCase):
    def test_folded_midlet_entry_and_order(self) -> None:
        attributes = tool.parse_manifest_text(
            "Manifest-Version: 1.0\r\n"
            "MIDlet-2: Second, /second.png, demo.Second\r\n"
            "MIDlet-1: First, /first.png, demo.\r\n"
            " Main\r\n"
        )
        entries = tool.midlets_from_attributes(attributes, "test")
        self.assertEqual([entry.class_name for entry in entries], ["demo.Main", "demo.Second"])
        self.assertEqual(entries[0].name, "First")

    def test_main_class_fallback(self) -> None:
        attributes = tool.parse_manifest_text("Main-Class: sample/Entry\n")
        entries = tool.midlets_from_attributes(attributes, "test")
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].class_name, "sample.Entry")


class DiscoveryTests(unittest.TestCase):
    def test_discovers_manifest_midlet(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            jar_path = root / "game.jar"
            with zipfile.ZipFile(jar_path, "w") as archive:
                archive.writestr(
                    "META-INF/MANIFEST.MF",
                    "Manifest-Version: 1.0\nMIDlet-1: Game, /icon.png, game.Main\n",
                )
            targets, issues = tool.discover_jar(jar_path, root, all_midlets=False)
            self.assertFalse(issues)
            self.assertEqual(len(targets), 1)
            self.assertEqual(targets[0].main_class, "game.Main")
            self.assertEqual(targets[0].main_source, "JAR manifest")

    def test_uses_sibling_jad(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            jar_path = root / "game.jar"
            with zipfile.ZipFile(jar_path, "w") as archive:
                archive.writestr("resource.bin", b"x")
            jar_path.with_suffix(".jad").write_text(
                "MIDlet-1: JAD Game, /icon.png, jad.Main\n", encoding="utf-8"
            )
            targets, issues = tool.discover_jar(jar_path, root, all_midlets=False)
            self.assertFalse(issues)
            self.assertEqual(targets[0].main_class, "jad.Main")
            self.assertTrue(targets[0].main_source.startswith("JAD:"))

    def test_invalid_zip_becomes_metadata_issue(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            jar_path = root / "broken.jar"
            jar_path.write_bytes(b"not a zip")
            targets, issues = tool.discover_jar(jar_path, root, all_midlets=False)
            self.assertFalse(targets)
            self.assertEqual(len(issues), 1)
            self.assertEqual(issues[0].kind, "metadata")

    def test_limit_is_applied_after_filtering(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("Alpha.jar", "Beta.jar", "Alpha2.jar"):
                with zipfile.ZipFile(root / name, "w") as archive:
                    archive.writestr(
                        "META-INF/MANIFEST.MF",
                        f"MIDlet-1: {name},, sample.Main\n",
                    )
            targets, issues, matching = tool.discover_targets(
                root, ["alpha"], limit=1, all_midlets=False
            )
            self.assertFalse(issues)
            self.assertEqual(matching, 2)
            self.assertEqual(len(targets), 1)
            self.assertIn("Alpha", targets[0].relative_path)


class ReportingTests(unittest.TestCase):
    def test_failure_signature_compacts_whitespace(self) -> None:
        kind, detail = tool.failure_signature(
            {"kind": "missing class", "detail": "  javax/foo/Bar\n  more "}
        )
        self.assertEqual(kind, "missing class")
        self.assertEqual(detail, "javax/foo/Bar more")

    def test_classify_timeout(self) -> None:
        class Execution:
            timed_out = True
            launch_error = ""
            return_code = None

        status = tool.classify_status(
            Execution(),
            {"install": "unknown", "app_state": "unknown", "frames_produced": 0},
            [],
        )
        self.assertEqual(status, "TIMEOUT")


if __name__ == "__main__":
    unittest.main()
