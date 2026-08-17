# Waveshare ESP32-S3-ETH-8DI-8RO

This appliance package targets the Waveshare `ESP32-S3-ETH-8DI-8RO` RS485 relay controller. It
owns the board's fixed wiring and exposes relays, isolated digital inputs, Ethernet, RTC, RS485,
RGB LED, buzzer output, and boot button. Applications own Modbus or other RS485 protocols and all
input-to-relay automations.

Do not use this profile for the similarly named `8DI-8DO`, `8DI-8RO-C`, or CAN variants without a
complete pin-map comparison.

## Hardware

- ESP32-S3-WROOM-1U-N16R8: 16 MB flash and 8 MB octal PSRAM
- W5500 SPI Ethernet controller
- TCA9554PWR 8-bit I/O expander at address `0x20`
- eight active-high relay outputs, each with one normally-open and one normally-closed contact
- eight optocoupler-isolated digital inputs for active or passive contacts
- isolated RS485 interface
- PCF85063ATL RTC at address `0x51`
- one WS2812 RGB LED and one PWM buzzer
- 7-36 V DC input or 5 V USB-C power

Waveshare rates the relay contacts at up to 10 A at 250 V AC or 10 A at 30 V DC. Installation and
load selection must follow the manufacturer's electrical-safety guidance.

## Pin Map

| Function | ESP32-S3 / expander pin |
| --- | --- |
| Digital inputs 1-8 | GPIO4-GPIO11 |
| W5500 interrupt | GPIO12 |
| W5500 MOSI | GPIO13 |
| W5500 MISO | GPIO14 |
| W5500 clock | GPIO15 |
| W5500 chip select | GPIO16 |
| W5500 reset | GPIO39 |
| RS485 TX / RX | GPIO17 / GPIO18 |
| RGB LED | GPIO38 |
| RTC interrupt | GPIO40, not consumed by this package |
| I2C SCL / SDA | GPIO41 / GPIO42 |
| Buzzer | GPIO46 |
| Boot button | GPIO0 |
| Relays 1-8 | TCA9554 pins 0-7 |

## Usage

The defaults expose all relay and input channels, use Ethernet, and configure RS485 as 9600-8-N-1.
Relay outputs use `ALWAYS_OFF` so every firmware boot starts with all loads off.

```yaml
packages:
  - !include
    file: ../packages/appliances/waveshare-esp32-s3-eth-8di-8ro.yaml
    vars:
      waveshare_8di8ro:
        networking:
          mode: ethernet # ethernet | wifi
        rs485:
          baud_rate: 19200
          parity: EVEN
        relays:
          relay1:
            name: Pump
            restore_mode: RESTORE_DEFAULT_OFF
          relay8:
            enabled: false
        inputs:
          input1:
            name: Pump Feedback
          input8:
            enabled: false
```

Each `relay1` through `relay8` object accepts `enabled`, `name`, `internal`,
`disabled_by_default`, and `restore_mode`. Each `input1` through `input8` object accepts `enabled`,
`name`, `internal`, `disabled_by_default`, and `use_interrupt`. Setting `enabled: false` removes the
channel rather than merely hiding it.

Network mode is a package variable and cannot be selected with an `esphome run` command-line
option. A local wrapper that selects Wi-Fi looks like this:

```yaml
substitutions:
  name: waveshare-8di8ro-test
  friendly_name: Waveshare 8DI 8RO Test

packages:
  - !include
    file: devices/waveshare-esp32-s3-eth-8di-8ro.yaml
    vars:
      waveshare_8di8ro:
        networking:
          mode: wifi
```

Run that wrapper normally with `esphome run <wrapper>.yaml`. Wi-Fi mode requires `wifi_ssid` and
`wifi_password` in the wrapper's `secrets.yaml`.

The isolated inputs are electrically active-low at the ESP32 and are inverted by the appliance so
an active input is reported as `ON`.

Stable infrastructure IDs are:

- `waveshare_8di8ro_relay_1` through `waveshare_8di8ro_relay_8`
- `waveshare_8di8ro_input_1` through `waveshare_8di8ro_input_8`
- `waveshare_8di8ro_rs485_uart`
- `waveshare_8di8ro_i2c` and `waveshare_8di8ro_rtc`
- `waveshare_8di8ro_rgb_led`
- `waveshare_8di8ro_buzzer` and `waveshare_8di8ro_buzzer_output`
- `waveshare_8di8ro_boot_button`

Sources:

- [Waveshare product wiki](https://www.waveshare.com/wiki/ESP32-S3-ETH-8DI-8RO)
- [ESPHome W5500 Ethernet](https://esphome.io/components/ethernet/)
- [ESPHome PCA9554/TCA9554 support](https://esphome.io/components/pca9554/)
- [ESPHome PCF85063 support](https://esphome.io/components/time/pcf85063/)
