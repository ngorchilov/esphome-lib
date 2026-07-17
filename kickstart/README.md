# Kickstart Firmware

These minimal configurations are for the first UART flash of a supported board or module. After
the device connects to Wi-Fi, replace the kickstart firmware with its application configuration
over OTA.

Each file:

- includes the matching board package from `packages/boards/`
- uses the library's standard API, OTA, logging, networking, and diagnostics
- explicitly enables the fallback Wi-Fi AP and captive portal for first-boot recovery
- contains no application-specific components or pin assignments

Run configuration checks and UART uploads from this directory, for example:

```bash
esphome config kickstart-cb2s.yaml
esphome run kickstart-cb2s.yaml
```
