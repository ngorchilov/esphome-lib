# Tuya Things Data Models

This directory stores decoded Things Data Model snapshots obtained from the Tuya Cloud API. They
are reference metadata for implementing and reviewing ESPHome configurations that communicate with
Tuya MCU devices.

## File Convention

- Match each filename to the corresponding device configuration under `devices/`.
- When one product has distinct hardware/cloud variants, append a short module suffix such as
  `-cb2s` or `-t1m` and keep one snapshot per variant.
- Store the canonical decoded model object with `modelId` and `services` at the top level.
- Use `"TODO"` as the `modelId` when the ID was not captured with the model.
- Replace a snapshot only when a newer model for the same variant is retrieved; Git history
  preserves earlier revisions.
- Keep the JSON pretty-printed with two-space indentation and a final newline.

For example:

```text
devices/tuya-ct-clamp-2em-80a-cb2s.yaml
devices/tuya-ct-clamp-2em-80a-t1m.yaml
data-models/tuya/things-data-model/tuya-ct-clamp-2em-80a-cb2s.json
data-models/tuya/things-data-model/tuya-ct-clamp-2em-80a-t1m.json
```

## Cloud Response Normalization

Tuya commonly returns the model as a JSON-encoded string in `result.model`. Decode that string once:

```bash
jq '.result.model | fromjson' tuya-cloud-response.json
```

Normalize the decoded output as follows:

- Keep only the decoded model object. Discard the outer `result`, `success`, `t`, `tid`, and any
  other response or device metadata.
- Keep `modelId`, `services`, property order, and all protocol-significant fields exactly as
  returned. This includes `abilityId`, `accessMode`, `code`, `typeSpec`, ranges, scale, step, unit,
  enum/bitmap values, extensions, unusual casing, and source typos.
- Translate only human-readable `name` and `description` text to concise English. Do not translate
  or correct machine-readable codes and values, and do not invent descriptions for empty fields.
- Use `Default Service` when the response contains one unnamed service.
- Pretty-print the result with two-space indentation and a final newline.

The normalized snapshot is a faithful protocol reference, not a corrected specification. Record
hardware-verified behavior separately when it differs from the cloud metadata.

## Security

Commit only the decoded model definition. Do not store the full API response, request URL, device ID,
UUID, local key, access token, account identifier, MAC address, or other device-specific data.

The cloud model describes advertised datapoints and their types. Hardware behavior remains
authoritative, especially for writable datapoints that may act as commands instead of persistent
state.
