# Tuya Things Data Models

This directory stores decoded Things Data Model snapshots obtained from the Tuya Cloud API. They
are reference metadata for implementing and reviewing ESPHome configurations that communicate with
Tuya MCU devices.

## File Convention

- Match each filename to the corresponding device configuration under `devices/`.
- Store the canonical decoded model object with `modelId` and `services` at the top level.
- Use `"TODO"` as the `modelId` when the ID was not captured with the model.
- Replace the snapshot when a newer model is retrieved; Git history preserves earlier revisions.
- Keep the JSON pretty-printed with two-space indentation and a final newline.

For example:

```text
devices/tuya-ct-clamp-3em-63a.yaml
data-models/tuya/things-data-model/tuya-ct-clamp-3em-63a.json
```

## Security

Commit only the decoded model definition. Do not store the full API response, request URL, device ID,
UUID, local key, access token, account identifier, MAC address, or other device-specific data.

The cloud model describes advertised datapoints and their types. Hardware behavior remains
authoritative, especially for writable datapoints that may act as commands instead of persistent
state.
