# Tuya `user_param_key` Profiles

This directory stores decoded `user_param_key` hardware profiles extracted from stock Tuya
firmware. These profiles describe devices where the Tuya module directly controls peripherals such
as buttons, LEDs, relays, ADC inputs, and power-monitoring chips. They are distinct from Tuya Cloud
Things Data Models used by devices that communicate with a separate Tuya MCU.

For Beken firmware, the profile can be extracted as JSON from a full dump using `bk7231tools`.
Its Tuya storage decoder can also be used on other chips when their storage format matches, but
the storage location and length must be verified for that platform. A direct-control Tuya device
does not necessarily contain a `user_param_key` hardware profile. Field meanings,
active levels, relay types, power-monitoring settings, and pin decoding follow the
[LibreTiny Tuya Pinout Config reference](https://docs.libretiny.eu/docs/resources/tuya-pin-config/).
Some firmware versions contain undocumented keys or values; preserve them verbatim and verify their
behavior against the physical device.

## File Convention

- Match each filename to the corresponding configuration under `devices/`.
- Store only the decoded `user_param_key` object, without an API or extraction-tool wrapper.
- Preserve original key names and numeric values.
- Keep JSON pretty-printed with two-space indentation and a final newline.
- An empty object (`{}`) is a placeholder awaiting a captured profile.

For example:

```text
devices/earu-breaker.yaml
data-models/tuya/user-param-key/earu-breaker.json
```

## Profiles Not Found

The WL2H-U/LN882H revision of the Tuya ceiling light was inspected on 2026-09-16. Its stock
firmware reports version `1.6.2`. The full dump is 2,097,152 bytes with SHA-256
`5d34e8185372e00c27889f9421413dcbe63c1254e526e5f9f4e00ae1f5003929`.

The protected block at `0x1EB000` and the complete Tuya KV region from `0x1EC000` to `0x200000`
(exclusive) decrypt successfully with valid checksums. Neither contains `user_param_key`, including
a search of all decrypted data blocks. No embedded profile JSON was found in the application.
The application contains the SDK's parameter-writing routine and custom lighting code; the key
name appearing in that routine is not evidence of a stored hardware profile. The installed
`bk7231tools` storage locator returns only `0x8000` bytes, so inspecting that slice alone is
insufficient for this dump.

No WL2H profile JSON has been created. The recovered PWM mapping, supporting code offsets, and
remaining verification work are saved in the
[WL2H firmware analysis](../firmware-analysis/tuya-ceiling-light-wl2h.md).
`tuya-ceiling-light-cbu.json` describes the older CBU revision and must not be treated as this
revision's extracted configuration.

## Reconstructed Profiles

`tuya-socket-1pm-v1-cb2s.json` is partially reconstructed because only the V2 stock profile remains.
Its button, network LED, and relay pins and active levels follow the corresponding device YAML; the
other settings come from the surviving V2 profile. The `crc` field is omitted because it cannot be
reconstructed after changing those values. Replace this profile with an original extraction if one
becomes available.

## Security

Do not commit full firmware dumps, Wi-Fi credentials, device identifiers, encryption keys, cloud
credentials, local keys, or extraction logs. Commit only the decoded hardware profile needed to
understand and reproduce the device configuration.
