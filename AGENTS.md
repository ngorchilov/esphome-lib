# AGENTS.md — esphome-lib

## Purpose

This repository is an ESPHome package library for reusable device, board, networking, and module templates.

It is actively used. Do not treat it as a throwaway experiment. Make small, careful, testable changes.

Primary current workstream: refactor and extend the `relay-control` module so it can replace older one-off relay/switch/light modules while remaining reusable across ESPHome and LibreTiny boards.

## How to work in this repository

- Prefer KISS.
- Prefer minimal diffs.
- Do not rewrite unrelated files.
- Do not rename files or move directories unless explicitly asked.
- Do not “fix” values that are intentionally chosen by the user.
- When unsure, inspect the current file tree and current YAML before editing.
- Preserve the user’s style unless a change is required for ESPHome compatibility.
- After a change, run the smallest relevant ESPHome validation command before broad compile attempts.

## Validation commands

Typical working directory for device validation:

```bash
cd /Users/ngorchilov/Desktop/dev/esphome-lib/devices
```

Preferred first check:

```bash
esphome config sonoff-basic-r4-switch.yaml
```

Compile only after config succeeds:

```bash
esphome compile sonoff-basic-r4-switch.yaml
```

For other devices, replace the YAML filename accordingly.

When a command fails, focus on the first real error block. Do not chase warnings unless they block validation or clearly indicate a broken template.

## Coding style

### YAML style

- Avoid quotes unless YAML requires them.
  - Empty string values may use `''`.
  - Plain strings like `local`, `detached`, `rocker`, `light.turn_on`, `RESTORE_DEFAULT_OFF` should remain unquoted.
- Prefer readable YAML over clever one-liners when parser or templating behavior is ambiguous.
- For ESPHome components, keep a consistent attribute order:
  - `platform`
  - `name`
  - `id`
  - visibility/internal
  - pin/output/entity-specific options
  - automations/actions
- When both `name` and `id` are present, prefer `name` before `id`.
- Keep comments short and useful.
- Do not add noisy explanatory comments for obvious YAML.

### ESPHome style

- Prefer package templates with `defaults:` and `vars:` for reusable, multi-instance modules.
- Avoid global `substitutions:` for instance-specific module values when the same module may be included multiple times.
- It is acceptable for board/base templates to define global platform facts such as `firmware_family`.
- Prefer list-form `packages:` when package names can be confused with component names or when nesting becomes fragile.

Example preferred include style for multi-instance modules:

```yaml
packages:
  - !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc_id: r1
      rc_name: none
      relay_pin: GPIO4
      relay_inverted: false
```

`rc_name: none` may be intentional to let Home Assistant inherit/use the device name. Do not change it to `${friendly_name}` unless explicitly asked.

## Current repository structure

Important paths:

```text
packages/
  boards/                         # board/base-board packages
  modules/                        # reusable functional modules
    networking.yaml
    networking/
    relay-control.yaml             # public relay-control assembler / entrypoint
    relay-control/
      core.yaml
      indicator-led.yaml
      control-target/
        local.yaml
        detached.yaml
      input/
        integrated-button-gpio.yaml
        external-gpio.yaml
        external-magic.yaml
      expose/
        light.yaml
    pin.yaml                       # shared GPIO pin wrapper / current investigation area
    pin/
      esphome.yaml
      libretiny.yaml

devices/                           # concrete device YAMLs
```

If the actual tree differs, inspect it and use the actual tree. Do not invent paths.

## Firmware families

The project currently distinguishes at least:

```yaml
firmware_family: esphome
firmware_family: libretiny
```

This is defined earlier in the board/base pipeline. Modules may assume it exists when included through normal device configs.

Important ESPHome-vs-LibreTiny difference already encountered:

- ESPHome/ESP32 pin schema supports options such as `ignore_strapping_warning`.
- LibreTiny pin schema rejects ESP-specific options.

Do not emit ESP-specific pin keys into LibreTiny configs.

## Relay-control architecture

### Goal

`packages/modules/relay-control.yaml` is the public entrypoint for a flexible relay control module.

It should replace older one-off modules such as:

- `relay-control-switch.yaml`
- `relay-control-light.yaml`
- `relay-control-light-magic.yaml`
- `relay-control-smart-light.yaml`
- `relay-control-smart-light-magic.yaml`

without duplicating component logic.

### Invariants

- The local power relay switch is always present.
- The power relay is always directly controllable, even in detached mode.
- The indicator LED, when present, always reflects the power relay state for safety.
- The integrated physical button, when present, always toggles the power relay.
- External wall-switch inputs toggle CONTROL.
- CONTROL may target the local relay or a detached Home Assistant entity.

### Control modes

`control_mode` values:

- `local`: control actions delegate to the power relay.
- `detached`: control actions call Home Assistant service/action on `control_entity_id`.
- `none`: control scripts remain no-op.

### Optional subcomponents

Optional relay-control subcomponents must be opt-in and easy to disable:

```yaml
has_indicator_led: false
has_integrated_button: false
has_external_switch_gpio: false
has_external_magic: false
has_expose_light: false
```

### Relay-control file contracts

`packages/modules/relay-control/core.yaml`

- Defines the always-present power relay switch.
- Defines scripts:
  - `${rc_id}_after_power_change`
  - `${rc_id}_power_on`
  - `${rc_id}_power_off`
  - `${rc_id}_power_toggle`
  - `${rc_id}_control_on`
  - `${rc_id}_control_off`
  - `${rc_id}_control_toggle`
- `control_*` scripts are placeholders/no-op in core and are implemented by control-target packages via `!extend`.

`packages/modules/relay-control/control-target/local.yaml`

- Extends `control_*` scripts.
- Delegates control to `power_*` scripts.

`packages/modules/relay-control/control-target/detached.yaml`

- Extends `control_*` scripts.
- Calls Home Assistant service/action on `control_entity_id`.
- Requires ESPHome `api:` to be enabled by the broader config.

`packages/modules/relay-control/indicator-led.yaml`

- Optional.
- LED must follow the power relay state, not the detached/control target state.
- Hooks into `${rc_id}_after_power_change` via `!extend`.

`packages/modules/relay-control/input/integrated-button-gpio.yaml`

- Optional.
- Integrated button toggles the power relay only.
- This is a safety/power-cut behavior and should not be redirected to detached/control target.

`packages/modules/relay-control/input/external-gpio.yaml`

- Optional.
- External switch toggles CONTROL.
- `ext_mode: momentary` means toggle on press; release is ignored.
- `ext_mode: rocker` means toggle on every state change; both press and release toggle.
- The actual physical rocker state is not authoritative because multi-way switch wiring is possible.

`packages/modules/relay-control/input/external-magic.yaml`

- Optional.
- Used for custom/magic switch style inputs.
- May preserve a virtual state in a template binary sensor.
- Still emits CONTROL toggles, not direct power toggles, unless explicitly changed.

`packages/modules/relay-control/expose/light.yaml`

- Optional.
- Exposes a light facade for appliance/light semantics.
- The power switch still exists independently.

## Multi-instance requirement

Relay-control must support multiple independent instances in one device.

Do not convert instance values to global substitutions unless there is no alternative.

Preferred pattern:

```yaml
packages:
  - !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc_id: r1
      rc_name: none
      relay_pin: GPIO4
      relay_inverted: false
      relay_strapping: false
      control_mode: local
      has_indicator_led: true
      led_pin: GPIO13
      led_inverted: true
      led_strapping: false

  - !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc_id: r2
      rc_name: Channel 2
      relay_pin: GPIO5
      relay_inverted: false
      relay_strapping: false
      control_mode: local
```

All generated ids must include `rc_id` or another caller-provided unique prefix.

## ESPHome templating pitfalls already discovered

These are known-bad or fragile in the current ESPHome 2025.12.x environment.

### 1. Do not return schema-sensitive mappings via `${ ... }`

This is fragile:

```yaml
pin: ${ firmware_family == 'esphome' and _pin_esphome or _pin_libretiny }
```

ESPHome may treat the expression result as an `EStr`/string scalar instead of a YAML mapping. Pin validation then sees a string like `{...}` rather than a mapping.

### 2. Dynamic `!include file:` path may not expand variables

This failed:

```yaml
!include
file: pin/${firmware_family}.yaml
```

ESPHome attempted to open the literal path containing `${firmware_family}`.

### 3. YAML merge cannot merge `${ ... }`

This failed:

```yaml
<<: ${ _pin_esphome if firmware_family == 'esphome' else _pin_libretiny }
```

YAML merge expects a real mapping node, but ESPHome provided an `EStr`.

### 4. Do not return raw package dicts with `file` / `vars` from Jinja`

This is fragile or invalid:

```yaml
packages:
  - ${ {'file':'relay-control/input/external-gpio.yaml','vars':{...}} if has_external_switch_gpio else {} }
```

If `vars:` is needed, it should usually be under a real `!include` block, not a raw dict produced by an expression.

### 5. Be careful with named package entries

Named package entries like this can be misread in some contexts:

```yaml
packages:
  relay: !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc_id: r1
```

During this refactor, prefer list-form package entries:

```yaml
packages:
  - !include
    file: ../packages/modules/relay-control.yaml
    vars:
      rc_id: r1
```

## Shared pin abstraction status

There is an active investigation around:

```text
packages/modules/pin.yaml
packages/modules/pin/esphome.yaml
packages/modules/pin/libretiny.yaml
```

Intended goal:

- Modules can include a shared pin wrapper instead of duplicating pin schemas.
- ESPHome-family configs may include ESP-specific pin options.
- LibreTiny configs must not include ESP-specific pin options.
- The call site should pass only values relevant to that pin use case.
- Defaults should live in the pin wrapper or pin implementation where possible.

Example desired call-site shape:

```yaml
pin: !include
  file: ../../pin.yaml
  vars:
    number: ${button_pin}
    inverted: ${button_inverted}
    input: true
    pullup: ${button_inverted}
    pulldown: ${!button_inverted}
    ignore_strapping_warning: ${button_strapping}
```

Known failed approaches are listed in the templating pitfalls above.

If ESPHome's newer templating system has gained a safe way to select mappings/includes, verify it by running `esphome config` before applying broadly.

If a generic `pin.yaml` wrapper cannot be made reliable, propose the least-bad alternative with minimal duplication. Possible alternatives:

1. Keep `pin.yaml` as a common/libretiny-safe subset and add small ESP-only patch packages where necessary.
2. Use package-level conditional includes per component type.
3. Use board-level static selection only if verified to work with the current ESPHome version.

Do not reintroduce expression-generated pin mappings unless verified by validation.

## Networking module context

The repository also contains a reusable networking module:

```text
packages/modules/networking.yaml
packages/modules/networking/
```

Networking has used conditional package inclusion successfully in some cases.

Do not break it while working on relay-control or pin helpers.

## Device migration context: Sonoff Basic R4

`devices/sonoff-basic-r4-switch.yaml` is the current test migration target from old `relay-control-switch.yaml` to the new `relay-control.yaml`.

Expected behavior:

- Power relay switch is present.
- `rc_name: none` may be intentional.
- Indicator LED follows the power relay.
- Integrated button toggles the power relay.
- External GPIO switch is rocker mode and toggles CONTROL.
- Current control mode is `local`.
- Optional light facade is disabled unless explicitly enabled.
- Optional magic switch is disabled unless explicitly enabled.

Do not alter unrelated metadata, project fields, board package selection, networking settings, or naming unless required to compile.

## Editing workflow for Codex

Before editing:

1. Inspect the relevant files.
2. Identify the smallest likely cause.
3. Explain the intended minimal change if the user asked for discussion first.

When editing:

1. Edit only files relevant to the issue.
2. Preserve existing style and intentional values.
3. Avoid broad formatting-only changes.
4. Run `esphome config` on the target device after changes when possible.

When reporting back:

- Summarize what changed.
- Mention files changed.
- Include the validation result or the first remaining error.
- Be explicit if a workaround relies on an ESPHome version-specific behavior.

```

```
