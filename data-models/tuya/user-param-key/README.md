# Tuya `user_param_key` Profiles

This directory stores decoded `user_param_key` hardware profiles extracted from stock Tuya
firmware. These profiles describe devices where the Tuya module directly controls peripherals such
as buttons, LEDs, relays, ADC inputs, and power-monitoring chips. They are distinct from Tuya Cloud
Things Data Models used by devices that communicate with a separate Tuya MCU.

The profile can be extracted as JSON from a full firmware dump using `bk7231tools`. Field meanings,
active levels, relay types, power-monitoring settings, and pin decoding follow the
[LibreTiny Tuya Pinout Config reference](https://docs.libretiny.eu/docs/resources/tuya-pin-config/).
Some firmware versions contain undocumented keys or values; preserve them verbatim and verify their
behavior against the physical device.

## File Convention

- Match each filename to the corresponding configuration under `devices/`.
- Store only the decoded `user_param_key` object, without an API or extraction-tool wrapper.
- Preserve original key names and numeric values.
- Keep JSON pretty-printed with two-space indentation and a final newline.

For example:

```text
devices/earu-breaker.yaml
data-models/tuya/user-param-key/earu-breaker.json
```

## Security

Do not commit full firmware dumps, Wi-Fi credentials, device identifiers, encryption keys, cloud
credentials, local keys, or extraction logs. Commit only the decoded hardware profile needed to
understand and reproduce the device configuration.
