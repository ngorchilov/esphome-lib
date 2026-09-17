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

There are currently no `known_failure` cases. Offline networking now passes; devices that explicitly
need an absent clock or HA actions are covered by rejected-input tests rather than known failures.
The phase-2 regressions now pass, with resolved-output assertions for optional relay controls,
EARU sampling/units, packet-radio frequency and Wi-Fi credentials. Partial-secret fixtures supply
only the missing credential; this prevents an unused fallback secret from masking a regression.
Configuration validation does not prove actuator behavior or hardware safety. The invalid six-zone
valve GPIO draft is no longer tracked; its Tuya model remains in the archive.

The pin matrix covers ESP32 (classic, C3 and S3), ESP8266, Beken, Realtek and Lightning. It checks
the resolved GPIO options, explicit true/false values, shared inputs, open-drain outputs, ESP32
drive strength and strapping flags, and number-only mappings through ESPHome's pin-number
validator. Concrete Ethernet devices exercise number-only consumers in the full sweep. Invalid
schema names and unsupported ESP8266 pullups must fail; ESP8266 relay roles must now pass.

The RF fan matrix covers SX127x and CC1101, explicit IDs and shorthand addresses, combined fan
membership, individual-only and learn-only configurations, all four synchronization anchors, and
custom protocol settings. Resolved-output assertions check entity metadata, each member's generated
address/ID/direction, startup and queue guards, synchronization commands, and transmission timing.
These are configuration contracts, not proof of RF reception or physical fan state. When rewriting
the templates, compare the resolved configurations (including lambda text) before and after; an
unchanged emitted C++ program does not need a firmware build.

Networking checks cover offline ESP32/ESP8266/Beken/Realtek, local relay control, explicit RTC time
for Tuya and integrated RTC appliances, both dual-interface priorities, disabled services, interface
startup policy, distinct IP/MAC diagnostics, and appliance forwarding. Invalid selectors, service
dependencies, boolean options and missing clocks are rejected. Config validation does not test
physical cable failover, radio association or HA reconnection; those need hardware verification.

## C++ And CI

YAML configuration-only changes require config validation only. Compile firmware only after
changing C++, including lambda bodies in YAML, and only for affected targets after config passes.
Host tests supplement this; they do not replace an affected embedded build or hardware verification.

The garage regression compiles the actual YAML lambdas with ASan/UBSan, checking endpoint guards,
contact-confirmed close, timeout publication, wraparound and the BLE reboot interlock:

```sh
python tests/garage_host_test.py
```

Run it when changing garage C++ behavior, then config/compile both the standalone garage and
`tests/fixtures/garage-ble.yaml`. These host tests are deliberately separate from the config-only
CI workflow, so routine YAML configuration edits do not trigger firmware or host compilation.

The existing BLE host suite tests the checked-out implementation with ASan/UBSan and an SDK double:

```sh
python components/bk72xx_ble_server/tests/test_recovery.py
```

It requires `clang++` (or `CXX` pointing to a compatible compiler), and builds only a small host
executable, not firmware. CI runs the config matrix, all tracked devices/kickstarts and JSON checks
on pushes/PRs. The separate BLE host job runs for changes to that component or its tests, or on
manual dispatch. Neither workflow compiles firmware. Changes to embedded C++ still require the
focused embedded build above and its result in the change report.
