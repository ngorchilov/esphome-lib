# Tuya Reference Data

This directory stores Tuya device metadata and hardware analysis:

- [`things-data-model/`](things-data-model/README.md) contains decoded Tuya Cloud API Things Data
  Models for devices that communicate with a separate Tuya MCU.
- [`user-param-key/`](user-param-key/README.md) contains decoded stock-firmware hardware profiles for
  devices where the Tuya module directly controls peripherals.
- [`firmware-analysis/tuya-ceiling-light-v2-wl2h.md`](firmware-analysis/tuya-ceiling-light-v2-wl2h.md)
  records hardware behavior recovered from the WL2H ceiling light's executable code, which has no
  stored `user_param_key` profile.
- [`firmware-analysis/tuya-ceiling-light-v2-cbu.md`](firmware-analysis/tuya-ceiling-light-v2-cbu.md)
  records the newer CBU ceiling light's compiled PWM mapping and white mixing, compared with WL2H
  and the existing CBU configuration. This revision also has no stored `user_param_key`.

Each subdirectory documents its own source, file convention, validation expectations, and security
requirements.
