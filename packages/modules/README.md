# Shared Behavior Modules

Complete devices already include the helpers they need. You only need these APIs when composing
your own firmware. Each example below is an entry in a remote package's `files:` list:

```yaml
packages:
  - url: https://github.com/ngorchilov/esphome-lib
    ref: main
    refresh: 0d
    files:
      - path: packages/modules/relay-group.yaml
        vars:
          relay_group:
            id: sockets
            members: [desk, printer]
            indicator_id: socket_led
```

This example requires an existing board package, relay-control instances `desk` and `printer`
with power cycling enabled, and a light with ID `socket_led`. It does not define physical hardware.

## Relay Groups

[`relay-group.yaml`](relay-group.yaml) combines existing relay-control members. It reports whether
any member is powered, updates the supplied indicator light, and provides an all-off/all-on toggle
and a power-cycle button. A physical button can call `script.execute: sockets_toggle`.

| `relay_group` field | Default | Meaning |
| --- | --- | --- |
| `id` | Required | Unique prefix for the group's command and button IDs. |
| `members` | Required | Nonempty list of unique relay-control `rc.id` values, in command order. |
| `indicator_id` | Required | Existing light ID; the group controls this light at boot and on state changes. |
| `state_id` | `<id>_power_status` | Aggregate binary sensor ID. |
| `state_name` | Power Status | Aggregate entity name. |
| `cycle_name` | Reboot All Relays | Power-cycle button name. |

Members must provide their POWER state and on/off/cycle scripts. Do not include disabled relay
entities or members without `rc.power_cycle.enabled: true`. The group operates physical POWER,
not detached HA targets. It does not create relays, alter restore modes or change per-member cycle
delays. Power cycles start consecutively without waiting for each cycle to finish.

Generated IDs are `<id>_toggle`, `<id>_cycle` (a button), and `state_id`. Multiple groups are
supported: use distinct IDs, entity names and indicator lights. State is sampled by the template
sensor as in the power-strip devices; this is not a safety interlock or an atomic multi-relay command.

## Fan Power Facade

[`fan-power-sync.yaml`](fan-power-sync.yaml) exposes a template fan backed by an existing power
switch. It writes power only when the switch differs from the requested state. An OFF command does
not send speed or oscillation commands. An ON command sends power first, then runs `on_running`.
It does not wait for MCU acknowledgement before those actions.

```yaml
- path: packages/modules/fan-power-sync.yaml
  vars:
    fan_sync:
      id: room_fan
      power_id: fan_power
      speed_count: 2
      name: Ventilation
      command_condition:
        lambda: return !id(syncing_from_mcu);
      on_running:
        - lambda: |-
            // The device defines this number and its protocol mapping.
            auto call = id(fan_speed_command).make_call();
            call.set_value(x->speed);
            call.perform();
```

This example assumes `fan_power`, `syncing_from_mcu` and `fan_speed_command` already exist.
`on_running` receives the native fan `on_state` argument `x` (`fan::Fan *`). Read it immediately
when taking a command snapshot; delayed actions see the fan's later state.

| `fan_sync` field | Default | Meaning |
| --- | --- | --- |
| `id` | Required | Fan ID. |
| `power_id` | Required | Existing switch ID. Hide the backing switch in your device when appropriate. |
| `speed_count` | Required | Number of supported speeds, validated by ESPHome's template fan. |
| `on_running` | Required | Action list for speed/oscillation commands; `[]` is allowed. |
| `name` | Empty | Fan entity name; empty uses the device name in HA. |
| `has_oscillating` | `false` | Advertise oscillation capability; the device must implement it. |
| `restore_mode` | `RESTORE_DEFAULT_OFF` | Native ESPHome fan restore mode. |
| `command_condition` | Always true | Native ESPHome condition checked before any power or running action. |

The module creates only the fan with the supplied ID. Multiple independent instances are supported.
It does not create datapoints or MCU listeners. The device must publish readback to the fan and
implement any feedback guard, boot-speed policy, invalid-value handling and speed/oscillation mapping.
It also owns modes, humidity settings, fault entities and their control/configuration categories.

Pro Breeze and JUMMICO deliberately retain different readback policies. Pro Breeze reflects low speed
after power-on and maps enum 0 to high. JUMMICO maps enum 0 to low, ignores unavailable speed reports,
and blocks outgoing commands while applying MCU readback. Physical JUMMICO swing remains local-only.

## CT16 Channels

[`tuya-ct16-channel.yaml`](tuya-ct16-channel.yaml) is the product-specific channel profile used 16
times by `devices/tuya-ct-clamp-16em-200a.yaml`. It is not a generic Tuya energy-meter decoder.
Normal dashboard users should include that device and use its `c01_name` through `c16_name`,
corresponding `_internal` substitutions, `sol_internal`, and `energy_scale`.

Its `ct16_channel` fields are `number` (required integer 1..16), `name` (required), `tuya_id`
(required existing MCU ID), `energy_scale` (required), `internal` (default false), and
`solar_internal` (default false). It creates energy from DP `114 + number`, current, power, CT
presence and solar-mode entities. Energy remains kWh with two decimal places and a nonnegative
clamp; the caller supplies its multiplier. Hidden channels remain available to the decoder.

The device owns raw payload decoding, phase/total measurements, configuration queries, and timing.
Decoder targets remain `circuit_<number>_current`, `_power`, `_ct_detected` and `_solar_configured`.
Each channel number may occur once; these fixed IDs support one CT16 meter per firmware.
