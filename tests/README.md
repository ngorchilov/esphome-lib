# Validation

Use the Python interpreter from your ESPHome environment. The current CI baseline is
ESPHome **2026.8.2**; that is a tested version, not a new minimum supported firmware version.

From the repository root:

```sh
python tests/validate.py                              # Consumer matrix and JSON models
python tests/validate.py --all                        # Also every tracked device/kickstart
python tests/validate.py source-tuya-product c3-light  # Named cases from cases.yaml
python tests/validate.py devices/jummico-dehumidifier.yaml
python -m unittest discover -s tests -p 'test_*.py'    # Runner accounting tests
```

The runner invokes `esphome config`, never `esphome compile`. It copies working-tree content
to a fresh temporary directory, excludes secrets and caches, and supplies dummy secrets there.
Configurations with `secrets: false` have no secrets file anywhere in that copy. Device/kickstart
discovery for `--all` uses Git-tracked files; new public consumer combinations belong in `cases.yaml`.
Logs, the snapshot and `results.json` remain at the printed artifact path for inspection.
Third-party components and fonts can require network access. `ESPHOME_DATA_DIR` may select a
dedicated reusable test cache; otherwise the cache is temporary too.

## Local Components

Every external-component declaration for this library includes `packages/component-source.yaml`.
Its normal value is the unpinned `github://ngorchilov/esphome-lib`; remote ESPHome dashboard users
need no override. To use local components when working directly in a checkout:

```sh
cd devices
ESPHOME_LIB_SOURCE="$(pwd)/../components" esphome config jummico-dehumidifier.yaml
```

Use an absolute `components` path, and set it in the environment of the process actually running
ESPHome. This is an explicit development switch, not automatic local/remote package detection.
The runner sets it to the isolated snapshot's components and checks actual imported Python module
paths for every own-repository component. It also checks the unset-variable GitHub default without
fetching it. Adding a second local `external_components` entry is insufficient: ESPHome still
processes the original remote source first. Third-party component sources are not changed.

## Expected Results

- `PASS`: a supported configuration validates, including provenance checks where specified.
- `REJECTED`: a deliberately invalid configuration fails with its expected configuration error.
- `XFAIL`: a known bug remains reproducible. This is not a working/supported configuration.
- `XPASS`: a known bug unexpectedly validates; the run fails until its expectation is reviewed.
- `FAIL`: a new failure, wrong failure reason, rejected-input acceptance or subprocess timeout.

The six temporary `known_failure` entries in `cases.yaml` track disabled BASIC R4/two-relay
consumers, packet-radio frequency and secret-free Wi-Fi (phase 2), ESP8266 pins (phase 3), and
offline networking (phase 4). Each records a specific expected error, not just a nonzero exit code.
Remove its `known_failure` and `reason` when fixing it, keeping the fixture as a passing regression.
Configuration validation does not prove actuator behavior or hardware safety; the six-zone valve
draft in particular must not be deployed even if its configuration validates.

## C++ And CI

YAML configuration-only changes require config validation only. Compile firmware only after
changing C++, including lambda bodies in YAML, and only for affected targets after config passes.
Host tests supplement this; they do not replace an affected embedded build or hardware verification.

The existing BLE host suite tests the checked-out implementation with ASan/UBSan and an SDK double:

```sh
python components/bk72xx_ble_server/tests/test_recovery.py
```

It requires `clang++` (or `CXX` pointing to a compatible compiler), and builds only a small host
executable, not firmware. CI runs the config matrix, all tracked devices/kickstarts and JSON checks
on pushes/PRs. The separate BLE host job runs for changes to that component or its tests, or on
manual dispatch. Neither workflow compiles firmware. Changes to embedded C++ still require the
focused embedded build above and its result in the change report.
