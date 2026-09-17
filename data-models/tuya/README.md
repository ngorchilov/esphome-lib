# Tuya Reference Data

This directory stores Tuya device metadata and hardware profiles:

- [`things-data-model/`](things-data-model/README.md) contains decoded Tuya Cloud API Things Data
  Models for devices that communicate with a separate Tuya MCU.
- [`user-param-key/`](user-param-key/README.md) contains decoded stock-firmware hardware profiles for
  devices where the Tuya module directly controls peripherals.

Each subdirectory documents its own source, file convention, validation expectations, and security
requirements.
