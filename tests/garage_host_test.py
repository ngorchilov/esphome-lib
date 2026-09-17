"""Compile the garage YAML's actual lambdas against a small deterministic host double."""

import os
from pathlib import Path
import subprocess
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    device = yaml.load((ROOT / "devices/tuya-garage-opener.yaml").read_text(), Loader=yaml.BaseLoader)
    cover = device["cover"][0]
    source = (ROOT / "tests/garage_host_test.cpp").read_text()
    for marker, body in {
        "OPEN_BODY": cover["open_action"][0]["lambda"],
        "CLOSE_BODY": cover["close_action"][0]["lambda"],
        "TOGGLE_BODY": cover["toggle_action"][0]["lambda"],
        "POSITION_BODY": cover["lambda"],
        "BLE_REBOOT_BODY": yaml.load(
            (ROOT / "packages/modules/tuya-garage-opener-ble.yaml").read_text(),
            Loader=yaml.BaseLoader,
        )["packages"][0]["vars"]["ble_pulse"]["reboot_condition"],
    }.items():
        source = source.replace(marker, body)
    with tempfile.TemporaryDirectory(prefix="esphome-garage-host-") as temporary:
        binary = Path(temporary) / "garage-test"
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-x", "c++", "-", "-o", str(binary)],
                       input=source, text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
