#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import sys
import unittest


CORE_ROOT = pathlib.Path(__file__).resolve().parents[2]
ANALYZER_PATH = CORE_ROOT / "Compatibility" / "analyze-failures.py"
SPEC = importlib.util.spec_from_file_location("phoneme_compat_analyzer", ANALYZER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load analyzer from {ANALYZER_PATH}")
ANALYZER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ANALYZER
SPEC.loader.exec_module(ANALYZER)


class FailureTaxonomyTests(unittest.TestCase):
    def test_classifies_linkage_verifier_and_native_failures(self) -> None:
        failures = ANALYZER.classify_failures(
            "\n".join(
                [
                    "ClassNotFoundException: com.example.Missing",
                    "NoSuchMethodError: javax.microedition.Foo.bar()V",
                    "UnsatisfiedLinkError: nativeMethod(I)V",
                    "ClassFormatError: Inconsistent or missing stackmap at target",
                ]
            )
        )
        kinds = {failure["kind"] for failure in failures}
        self.assertEqual(
            {"missing_class", "missing_method", "missing_native", "verifier"},
            kinds,
        )

    def test_classifies_runtime_domain_failures(self) -> None:
        failures = ANALYZER.classify_failures(
            "\n".join(
                [
                    "installation failed: invalid MIDlet suite",
                    "deadlock detected in scheduler",
                    "LCDUI bridge failed",
                    "framebuffer error",
                    "RecordStoreException: corrupt RMS",
                    "OutOfMemoryError",
                    "SocketException: connection reset",
                    "MediaException: unsupported codec",
                ]
            )
        )
        kinds = {failure["kind"] for failure in failures}
        self.assertTrue(
            {
                "install",
                "thread",
                "lcdui",
                "graphics",
                "rms",
                "performance",
                "network",
                "media",
            }.issubset(kinds)
        )

    def test_empty_result_exception_field_is_not_an_uncaught_exception(self) -> None:
        failures = ANALYZER.classify_failures(
            '{"java_exception_class":"","error_message":""}'
        )
        self.assertNotIn(
            "uncaught_exception", {failure["kind"] for failure in failures}
        )

    def test_uncaught_exception_text_is_detected(self) -> None:
        failures = ANALYZER.classify_failures(
            "Uncaught Java exception while starting MIDlet"
        )
        self.assertIn(
            "uncaught_exception", {failure["kind"] for failure in failures}
        )

    def test_uncaught_worker_thread_exception_is_detected(self) -> None:
        failures = ANALYZER.classify_failures(
            "phoneME Java thread 6 terminated with uncaught "
            "java/lang/ArrayIndexOutOfBoundsException at ad.c(String)V"
        )
        self.assertIn(
            "uncaught_exception", {failure["kind"] for failure in failures}
        )


class ExpectationTests(unittest.TestCase):
    def test_requires_observable_milestones_and_frames(self) -> None:
        expected = {
            "install": "success",
            "exit": "normal",
            "app_state": "active",
            "max_startup_ms": 1000,
            "min_frames": 1,
            "frame_hashes": [],
            "milestones": ["title-screen", "playable"],
            "network_actions": [],
            "media_actions": [],
        }
        observed = {
            "install": "success",
            "exit_code": 0,
            "app_state": "active",
            "startup_ms": 20,
            "frames_produced": 0,
            "frame_sha256": "",
            "milestones": ["title-screen"],
            "network_actions": [],
            "media_actions": [],
        }
        reasons = ANALYZER.evaluate_expectations(expected, observed, None)
        self.assertTrue(any("frames expected" in reason for reason in reasons))
        self.assertTrue(any("playable" in reason for reason in reasons))

    def test_complete_observable_contract_passes(self) -> None:
        expected = {
            "install": "success",
            "exit": "normal",
            "app_state": "active",
            "max_startup_ms": 1000,
            "min_frames": 1,
            "frame_hashes": ["abc"],
            "milestones": ["playable"],
            "network_actions": ["socket-connect"],
            "media_actions": ["tone-start"],
        }
        observed = {
            "install": "success",
            "exit_code": 0,
            "app_state": "active",
            "startup_ms": 20,
            "frames_produced": 1,
            "frame_sha256": "abc",
            "milestones": ["playable"],
            "network_actions": ["socket-connect"],
            "media_actions": ["tone-start"],
        }
        self.assertEqual([], ANALYZER.evaluate_expectations(expected, observed, None))


class DifferentialTests(unittest.TestCase):
    def test_differential_reports_semantic_difference(self) -> None:
        phone_me = {
            "install": "success",
            "app_state": "active",
            "exit_code": 0,
            "frames_produced": 1,
            "frame_sha256": "one",
            "milestones": ["title-screen"],
            "network_actions": [],
            "media_actions": [],
        }
        reference = {
            **phone_me,
            "frame_sha256": "two",
            "milestones": ["title-screen", "playable"],
        }
        result = ANALYZER.differential(phone_me, reference)
        self.assertFalse(result["matches"])
        self.assertIn("frame_sha256", result["differences"])
        self.assertEqual(
            ["playable"], result["differences"]["milestones"]["only_reference"]
        )


if __name__ == "__main__":
    unittest.main()
