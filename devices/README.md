# Device Families

Include one device package in your ESPHome dashboard configuration and select the hardware profile
that matches the installed module. A profile selects real wiring and MCU capabilities, not just a
display name. Unsupported profiles fail configuration.

Set `name` and `friendly_name` in your dashboard. When migrating an installed device, keep its
existing values to preserve its network identity and Home Assistant presentation.

## Tuya CT Clamp 2EM 80A

One firmware definition supports CB2S and T1-M versions of this two-channel Tuya MCU meter.
Both use the same telemetry datapoints and scaling; only the T1-M profile exposes its additional
alarm settings, buzzer and diagnostic alarm entities. Each variant's cloud model remains a
separate JSON file under `data-models/tuya/things-data-model/`.

```yaml
substitutions:
  name: grid-monitor
  friendly_name: Grid Monitor

packages:
  - url: https://github.com/ngorchilov/esphome-lib
    ref: main
    refresh: 0d
    files:
      - path: devices/tuya-ct-clamp-2em-80a.yaml
        vars:
          ct_clamp:
            profile: t1m
            channels:
              a_name: Grid
              b_name: Solar
```

The `ct_clamp` include object accepts:

| Field | Default | Accepted Values |
| --- | --- | --- |
| `profile` | `cb2s` | `cb2s`, `t1m` |
| `channels.a_name` | `Channel A` | Channel A's label in entity names |
| `channels.b_name` | `Channel B` | Channel B's label in entity names |

These established dashboard substitutions also remain available:

| Substitution | Default | Meaning |
| --- | --- | --- |
| `uart_tx_pin`, `uart_rx_pin` | `GPIO11`, `GPIO10` | Tuya MCU UART pins |
| `uart_baud_rate` | `9600` | Tuya MCU UART baud rate |
| `channel_a_name`, `channel_b_name` | From `ct_clamp.channels` | Direct channel-label override |
| `power_scale` | `0.1` | Raw power to watts |
| `energy_scale` | `0.01` | Raw energy to kWh |
| `frequency_scale` | `0.01` | Raw frequency to Hz |
| `voltage_scale` | `0.1` | Raw voltage to volts |
| `current_scale` | `0.001` | Raw current to amperes |

The device uses the shared Home Assistant clock, `ha_time`, for Tuya MCU time. Disabling that clock
requires you to provide another time component with that ID; the device cannot simply drop its
time dependency. Its telemetry IDs and T1-M alarm IDs are unchanged across hardware profiles.

## Tuya Strip 4PM

The `cbu` and `t1u` profiles each provide four switched sockets, a switched USB supply and a BL0942
energy monitor. Their physical button switches all five relays off when any is on, or all on when
all are off. The status LED follows aggregate relay state. Each relay retains its power-cycle
button, and `Reboot All Relays` operates all five.

```yaml
substitutions:
  name: office-power-strip
  friendly_name: Office Power Strip

packages:
  - url: https://github.com/ngorchilov/esphome-lib
    ref: main
    refresh: 0d
    files:
      - path: devices/tuya-strip-4pm.yaml
        vars:
          power_strip:
            profile: t1u
```

The `power_strip` include object accepts `profile`, either `cbu` (default) or `t1u`.
Established dashboard substitutions remain available:

| Substitution | Default | Meaning |
| --- | --- | --- |
| `relay1_id` through `relay4_id` | `socket_1` through `socket_4` | Relay-control ID prefixes |
| `relay5_id` | `relay_usb` | USB relay-control ID prefix |
| `relay1_name` through `relay4_name` | `Socket 1` through `Socket 4` | Entity names |
| `relay5_name` | `Relay USB` | USB entity name |
| `relay1_pin` through `relay5_pin` | Profile wiring | Physical relay GPIOs |
| `relay1_inverted` through `relay5_inverted` | `false` | Relay polarity |
| `led_pin` | Profile wiring | Status LED GPIO |
| `led_inverted` | `false` | Status LED polarity |
| `button_pin` | `GPIO22` | Physical master button GPIO |
| `button_pullup`, `button_inverted` | `true`, `true` | Button electrical configuration |

The BL0942 module's `energy` include object is forwarded unchanged; its serial defaults are TX
GPIO11, RX GPIO10, 4800 baud and a 30-second update interval. It accepts `tx_pin`, `rx_pin`,
`baud_rate` and `update_interval`. Changing pin overrides requires matching physical wiring.

## Shared Settings

Both families inherit logger defaults. Set `substitutions.log_level` or your dashboard's `logger`
configuration to change verbosity. Neither device adds a release-version bump for a hardware
profile selection.

Pass the [networking object](../packages/modules/networking/README.md) under the device include's
`vars` for network/service settings. Additional minimum ESPHome requirements use the
[additive requirements list](../packages/README.md), not a direct `esphome.min_version` override.
