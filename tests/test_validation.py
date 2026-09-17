"""Tests for the regression runner's pass/fail accounting (no firmware builds)."""

import subprocess
import unittest
from pathlib import Path

import yaml

from validate import ROOT, classify, tracked_files, unique_pairs


def min_version_lines(node):
    """Inspect YAML structure without resolving includes or custom tags."""
    found = []
    seen = set()

    def visit(item):
        if id(item) in seen:
            return
        seen.add(id(item))
        if isinstance(item, yaml.MappingNode):
            for key, value in item.value:
                if key.value == "esphome" and isinstance(value, yaml.MappingNode):
                    found.extend(k.start_mark.line + 1 for k, _ in value.value if k.value == "min_version")
                visit(value)
        elif isinstance(item, yaml.SequenceNode):
            for value in item.value:
                visit(value)

    visit(node)
    return found


class ValidationTests(unittest.TestCase):
    def test_min_version_scanner(self):
        config = yaml.compose("""
defaults:
  optional_package:
    esphome: {min_version: 2026.8.0}
packages:
  - !include another.yaml
  - esphome:
      min_version: 2026.7.0
substitutions:
  esphome_requirements:
    - source: test
      version: 2026.8.0
""")
        self.assertEqual(min_version_lines(config), [4, 8])

    def test_min_version_has_one_owner(self):
        result = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--",
             "devices", "kickstart", "packages", "tests/fixtures"],
            cwd=ROOT, capture_output=True, text=True, check=True,
        )
        owner = Path("packages/boards/templates/base-board.yaml")
        declarations = []
        for name in sorted(set(result.stdout.split("\0")) - {""}):
            path = Path(name)
            if path.suffix not in (".yaml", ".yml"):
                continue
            node = yaml.compose((ROOT / path).read_text())
            declarations.extend((path, line) for line in min_version_lines(node))
        self.assertEqual([path for path, _ in declarations], [owner],
                         f"Only the base resolver may set esphome.min_version: {declarations}")

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
