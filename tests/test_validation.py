"""Tests for the regression runner's pass/fail accounting (no firmware builds)."""

import unittest

import yaml

from validate import ROOT, classify, tracked_files, unique_pairs


class ValidationTests(unittest.TestCase):
    def test_normal_config(self):
        self.assertEqual(classify(0, "valid", {}), "PASS")
        self.assertEqual(classify(1, "error", {}), "FAIL")

    def test_known_failure_is_strict(self):
        case = {"known_failure": "missing ID"}
        self.assertEqual(classify(1, "Failed config: missing ID", case), "XFAIL")
        self.assertEqual(classify(0, "valid", case), "XPASS")
        self.assertEqual(classify(1, "Failed config: something else", case), "FAIL")
        self.assertEqual(classify(124, "Failed config: missing ID", case), "FAIL")
        self.assertEqual(classify(1, "Traceback: missing ID", case), "FAIL")

    def test_rejected_config_is_strict(self):
        case = {"reject": "unsupported"}
        self.assertEqual(classify(1, "Failed config: unsupported", case), "REJECTED")
        self.assertEqual(classify(0, "valid", case), "FAIL")
        self.assertEqual(classify(1, "Failed config: unrelated", case), "FAIL")

    def test_duplicate_json_keys_are_rejected(self):
        self.assertEqual(unique_pairs([("a", 1), ("b", 2)]), {"a": 1, "b": 2})
        with self.assertRaises(ValueError):
            unique_pairs([("a", 1), ("a", 2)])

    def test_devices_inherit_logger_thresholds(self):
        for path in tracked_files():
            if path.parts[0] != "devices" or path.suffix != ".yaml":
                continue
            with self.subTest(path=str(path)):
                config = yaml.load((ROOT / path).read_text(), Loader=yaml.BaseLoader)
                logger = config.get("logger", {})
                self.assertNotIn("level", logger)
                self.assertNotIn("logs", logger)


if __name__ == "__main__":
    unittest.main()
