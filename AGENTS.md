# AGENTS.md - esphome-lib

## What This Repository Is

`esphome-lib` is a reusable ESPHome package library. It provides:

- board packages that describe MCU families and physical board targets
- base-board packages that add common ESPHome services and diagnostics
- appliance packages that assemble a board and its integrated peripherals
- networking packages for Wi-Fi, Ethernet, API, OTA, and mDNS
- functional modules for relays, lights, switches, energy monitors, and integrations
- concrete `devices/*.yaml` files that compose those packages into real devices
- minimal `kickstart/*.yaml` firmware for first UART flashes of supported boards

This repository is actively used. Treat it like production infrastructure: make small, deliberate,
validated changes, and preserve intentional device values.

## Composition Model

The library is layered. Most concrete devices follow this shape:

```text
devices/<device>.yaml
  -> packages/appliances/<appliance>.yaml (when a complete hardware assembly exists)
    -> packages/boards/<vendor-or-family>/<board>.yaml
    -> packages/modules/*.yaml
  -> packages/boards/<vendor-or-family>/<board>.yaml (otherwise)
    -> packages/boards/templates/base-board-<family>.yaml
      -> packages/boards/templates/base-board.yaml
        -> packages/modules/networking.yaml
  -> optional packages/modules/*.yaml
  -> device-specific components and overrides
```

### Devices

`devices/*.yaml` files are concrete firmware configurations. They should mostly:

- define device substitutions such as `name`, `friendly_name`, pins, inversion flags, and feature choices
- include exactly one board package, directly or through an appliance package
- include reusable modules when possible
- contain device-specific behavior only when it truly belongs to that one device

Do not casually change project metadata, names, pins, restore modes, or entity names in device files.
Values like `none`, `None`, quoted booleans, commented pin options, or unusual defaults may be
intentional for a specific device or for Home Assistant entity behavior.

### Kickstart Firmware

`kickstart/*.yaml` files are temporary board bring-up configurations. Each kickstart should:

- map one-to-one to a concrete package under `packages/boards/`
- include that board package and no application-specific components
- use a generic kickstart identity
- explicitly enable the fallback Wi-Fi AP for first-boot recovery and OTA handoff

Name firmware files `kickstart-<board-package>.yaml`. Add aliases only when an established
flashing workflow needs them.

### Board Packages

`packages/boards/*/*.yaml` files describe hardware targets. They should:

- include the appropriate base-board template
- define the ESPHome platform component (`esp32`, `esp8266`, `bk72xx`, `rtl87xx`, etc.)
- set board, variant, framework, and family-specific compatibility options
- avoid device behavior such as relay logic, sensors, lights, and user-facing automations

Board packages may define platform facts such as `firmware_family` through the base-board pipeline.

### Base-Board Packages

`packages/boards/templates/base-board*.yaml` files provide shared firmware foundations:

- `esphome` name/friendly name and minimum ESPHome version
- logger defaults
- API, OTA, mDNS, and networking through `packages/modules/networking.yaml`
- common diagnostics such as uptime, status, version, restart button, and time
- family-specific adjustments for ESPHome, LibreTiny, ESP8266, ESP32, Beken, and Realtek

Keep base-board packages broad and boring. If a behavior is not useful across many devices, it
probably belongs in a device file or a functional module instead.

### Appliance Packages

`packages/appliances/*.yaml` files represent complete, reusable hardware assemblies. An appliance
may select a concrete board package and compose integrated peripherals whose wiring is fixed by that
board. Use an appliance when it removes physical pin knowledge from device files and presents a
smaller capability-oriented API; do not use one merely to regroup a few component declarations.

`packages/appliances/lora-receiver.yaml` supports the Heltec WiFi LoRa 32 V2 and LilyGO TTGO LoRa32
V2.1 profiles. Its public API is:

```yaml
packages:
  - !include
    file: ../packages/appliances/lora-receiver.yaml
    vars:
      lora:
        board: lilygo # heltec | lilygo
        radio:
          driver: sx127x # sx127x | wmbus
        display:
          enabled: true
```

The appliance owns the board target, radio and display buses, integrated GPIO wiring, OLED font,
and status LED. Applications extend `lora_radio` and `lora_display` with protocol callbacks and
presentation logic. Stable infrastructure ids include `lora_spi`, `lora_i2c`, `lora_oled_font`,
and `lora_status_led`. Keep external component declarations and driver-specific toolchain workarounds
in the consuming device when they are not intrinsic to the physical board.

### Functional Modules

`packages/modules/*.yaml` files add reusable behavior. Modules should be designed around explicit
inputs and narrow responsibilities.

Important modules:

- `networking.yaml` selects Wi-Fi, Ethernet, or no network support and owns API/OTA/mDNS defaults.
- `pin.yaml` is the shared pin schema wrapper used by modules that need ESPHome/LibreTiny-safe pins.
- `relay-control.yaml` is the public relay-control assembler and composes smaller relay-control packages.
- `energy-monitoring-bl0937.yaml` and `energy-monitoring-bl0942.yaml` expose reusable energy monitor setups.
- `homekit.yaml` adds HAP-ESPHome support and ESP32 framework options.
- `lora/radio.yaml` and `lora/display.yaml` provide the capability layers used by the LoRa receiver appliance.

Prefer a module over copy/paste device logic when the behavior is reusable. Prefer device-local YAML
when the behavior is unique, experimental, or depends on one physical product.

## Firmware Families

The project currently distinguishes at least:

```yaml
firmware_family: esphome
firmware_family: libretiny
```

This value is defined by the board/base-board layer. Modules included through normal device configs
may assume it exists.

Important compatibility rule:

- ESPHome/ESP32 pin schemas may accept ESP-specific keys such as `ignore_strapping_warning`.
- LibreTiny pin schemas reject ESP-specific keys.

## Shared Pin Infrastructure

Use `packages/modules/pin.yaml` for reusable module pins unless there is a proven reason not to.
It supports:

- full GPIO pin schemas for switches, binary sensors, UART pins, outputs, and similar components
- `schema: number` for components such as Ethernet that accept pin-number schemas rather than full GPIO mappings
- a namespaced `pin:` vars object
- ESP-specific pin keys only when `firmware_family == 'esphome'`
- LibreTiny-safe mappings when `firmware_family == 'libretiny'`

Preferred shape:

```yaml
pin: !include
  file: ../pin.yaml
  vars:
    pin:
      number: GPIO4
      inverted: false
      mode:
        output: true
      ignore_strapping_warning: false
```

`packages/modules/pin/esphome.yaml` and `packages/modules/pin/libretiny.yaml` exist as platform
variants. The preferred long-term design is for `pin.yaml` to wrap those variants, but nested dynamic
include behavior has been unreliable. The current expression-generated mapping in `pin.yaml` is a
contained compatibility workaround. Do not copy that pattern into unrelated modules unless it has
been validated and documented.

## Networking Framework

`packages/modules/networking.yaml` is included by `base-board.yaml` and chooses the network stack via:

```yaml
vars:
  networking:
    mode: wifi # wifi | ethernet | none
```

Responsibilities:

- include Wi-Fi support when `networking.mode: wifi`
- include Ethernet support when `networking.mode: ethernet`
- expose API, OTA, and mDNS defaults
- use `pin.yaml` for Ethernet pin-number fields
- keep the fallback Wi-Fi AP and captive portal disabled unless a device explicitly sets
  `wifi_ap_enabled: true` under `substitutions:`

Device-specific networking overrides should travel through the board package include so the base-board
layer and nested networking package see the same package context:

```yaml
packages:
  - !include
    file: ../packages/boards/espressif/m5stack-core-esp32.yaml
    vars:
      networking:
        mode: ethernet
        ethernet:
          type: IP101
          mdc_pin: GPIO23
          mdio_pin: GPIO18
          clk_pin: GPIO0
          power_pin: GPIO5
```

The networking module uses conditional package inclusion. Preserve the existing include style unless
you have validated a different approach with concrete devices.

## Relay-Control Framework

`packages/modules/relay-control.yaml` is the public entrypoint for reusable relay, switch, and light
control behavior. It is an assembler package: it composes core relay behavior, one control target,
and optional feature packages.

Relay-control concepts:

- POWER is the local physical relay.
- CONTROL is the logical target for external inputs and optional light facade.
- CONTROL may map to POWER locally, call a detached Home Assistant entity, or remain no-op.

Core invariants:

- The local power relay switch is always present.
- The power relay is always directly controllable, even in detached mode.
- Indicator LEDs, when present, follow the power relay state for safety.
- Integrated physical buttons, when present, toggle the power relay.
- External wall-switch inputs toggle CONTROL.
- The optional light facade targets CONTROL while the power switch remains independent.

`rc.control.mode` values:

- `local`: CONTROL delegates to POWER.
- `detached`: CONTROL calls Home Assistant service/action on `rc.control.entity_id`.
- `none`: CONTROL scripts remain no-op.

Relay-control is multi-instance by design. Every generated id must include `rc.id` or another
caller-provided unique prefix. Do not move instance-specific values into global substitutions unless
there is no workable alternative.

Preferred include shape:

```yaml
packages:
  - !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc:
        id: r1
        name: none
        power:
          pin: GPIO4
          inverted: false
          strapping: false
        control:
          mode: local
```

## Package Design Standards

### Defaults, Vars, And Substitutions

- Public reusable module APIs must use one namespaced object under `vars:` such as `rc:`, `pin:`,
  `networking:`, or `energy:`.
- Use `defaults:` to normalize namespaced objects into local helper values inside the module.
- Use guarded lookups such as `rc.get("power", {}).get("pin", "GPIO0")`; do not rely on deep
  default merging.
- Use global `substitutions:` for device-local aliases, names, friendly names, board facts, and
  ESPHome values that are intentionally global.
- Avoid global substitutions for reusable module inputs.
- Internal package references may use private-looking names such as `_rc_target_local_pkg`.
- Keep defaults close to the module that owns them.

### IDs And Names

- Generated ids in reusable modules must be deterministic and collision-safe.
- Multi-instance modules must include an instance prefix such as normalized `rc_id` in every
  generated id.
- Shared base-board diagnostics may use stable `mcu_*` ids.
- Single-instance modules may use simple ids only when the module is clearly not multi-instance.
- Do not change entity names just because they look odd. Some names intentionally influence Home
  Assistant display behavior.

### Optional Features

- Optional subcomponents should be opt-in and easy to disable.
- Prefer clear namespaced booleans such as `rc.indicator_led.enabled`,
  `rc.external_switch.enabled`, or `rc.light.enabled`.
- Disabled optional packages should contribute no components.
- Feature flags should not require callers to pass irrelevant pins or entity ids.

### Includes And Conditional Packages

- Prefer list-form `packages:` entries:

```yaml
packages:
  - !include ../packages/modules/example.yaml
```

- Use real `!include` blocks with `vars:` when passing module variables.
- Be careful with named package entries; use them only when they are clearly needed.
- Do not return raw `{file: ..., vars: ...}` package dictionaries from expressions.
- Validate conditional include behavior with `esphome config` before applying it broadly.

## YAML Coding Standards

- Avoid quotes unless YAML requires them.
  - Empty string values may use `''`.
  - Plain values such as `local`, `detached`, `rocker`, `light.turn_on`, and `RESTORE_DEFAULT_OFF` should remain unquoted.
- Preserve existing quoted values when they appear intentional.
- Prefer readable YAML over compact one-liners.
- Keep comments short and useful.
- Do not add explanatory comments for obvious YAML.

ESPHome component attribute order:

```text
platform
name
id
visibility/internal/entity category
pin/output/entity-specific options
automations/actions
```

When both `name` and `id` are present, prefer `name` before `id`.

## ESPHome Templating Standards

ESPHome templating behavior depends on version and context. Be conservative.

Known fragile patterns:

- returning schema-sensitive mappings from `${ ... }`
- using dynamic paths inside nested `!include` wrappers
- using YAML merge (`<<`) with `${ ... }` values
- returning raw package dictionaries with `file` / `vars` from expressions
- relying on named package entries where list-form entries are clearer

If a templating approach is required for reusable infrastructure, validate it with `esphome config`
on concrete ESPHome and LibreTiny devices when applicable.

## Validation Standards

Run validation from:

```bash
cd devices
```

Run `esphome config` before compile:

```bash
esphome config sonoff-basic-r4-switch.yaml
```

Compile only after config succeeds:

```bash
esphome compile sonoff-basic-r4-switch.yaml
```

Choose validation targets by impact:

- Base-board, networking, or common diagnostics: validate at least one Wi-Fi ESPHome device and one relevant Ethernet or LibreTiny device.
- Pin wrapper changes: validate an ESPHome device and a LibreTiny device.
- Relay-control changes: validate a relay-control device and any device mode touched by the change.
- Device-only changes: validate that device.

When validation fails, report the first real error block and the file involved.

## Editing Discipline

- Inspect before editing.
- Change only files relevant to the task.
- Preserve unrelated user changes.
- Do not clean up generated ESPHome cache/build output unless explicitly asked.
- Do not rename, move, or reorganize packages unless explicitly asked.

## Reporting Back

When reporting changes:

- summarize what changed
- mention files changed
- include validation results or the first remaining error
- call out any ESPHome-version-specific workaround
