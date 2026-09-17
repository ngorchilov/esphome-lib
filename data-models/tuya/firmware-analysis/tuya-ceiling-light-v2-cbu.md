# Tuya Ceiling Light V2 (CBU) — Firmware Analysis

The newly supplied CBU/BK7231N unit uses a different LED mapping from the existing
`tuya-ceiling-light-v1-cbu.yaml`. Its channel order and white mixing match the recovered WL2H
behavior, but its PWM initialization frequency is 5 kHz rather than 4 kHz. This establishes
similarity of the LED interface, not identical application firmware or optical calibration.

## Source

- Analysis date: 2026-09-17.
- Source: owner's `Tuya Ceiling Ligth (cbu)/new.bin` and `new/` dissection.
- Full dump: 2,097,152 bytes.
- SHA-256: `713eb48447e6d4bd5c237199383fbafa2dae03bcc648b9b444f00114d48d8f11`.
- Application: `QC0294_P1_QMCBU_CEILING5`, version `1.0.3`.
- Embedded build timestamp: `Nov 22 2023 19:28:47`.
- Decrypted application SHA-256:
  `ac1bffce4ece5502a24ded3b15f44d9d0f75af83c43b203e87a1977f60012c60`.

The supplied extracted encrypted application matches the dump after removing Beken's interleaved
CRC bytes, except for the extractor's final 16 padding bytes. The code inspected here lies before
that padding. Both CBU dissections contain an identical bootloader; their application images differ.

The protected block at `0x1ED000`, key block at `0x1EE000`, and all 17 data blocks from
`0x1EF000` to `0x200000` were inspected. All validate successfully. Neither protected storage nor
the complete KV region contains `user_param_key`. No embedded hardware-profile JSON was found.
The application does contain the SDK's key-name string, which alone is not an extracted profile.
No synthetic `user_param_key` JSON has been created for this unit.

Firmware, decrypted storage, identifiers, and credentials remain outside this repository.

## LED Outputs

| Channel | New CBU GPIO | LibreTiny alias | WL2H counterpart | Existing CBU YAML GPIO |
| --- | --- | --- | --- | --- |
| Red | P8 | PWM2 | PA07 | P26 |
| Green | P7 | PWM1 | PA10 | P24 |
| Blue | P6 | PWM0 | PA11 | P6 |
| Cold white / temperature 1000 | P26 | PWM5 | PA12 | P8 |
| Warm white / temperature 0 | P24 | PWM4 | PB03 | P7 |

Aliases follow [LibreTiny's CBU pin definitions](https://docs.libretiny.eu/boards/cbu/).
WL2H mapping and stock probes are recorded in the [WL2H analysis](tuya-ceiling-light-v2-wl2h.md).
The owner has confirmed the WL2H replacement's cold/warm assignments on hardware. The new CBU's
white labels follow its `rgbcw` ordering and temperature endpoints; physical confirmation on this
particular unit remains outstanding. Kelvin limits have not been measured.

All five new CBU outputs use the same **5,000 Hz** initialization frequency. The driver receives
a 200,000 ns period and initially zero duty. All five application-level active flags are `1`;
the output wrapper sends increasing values as increasing PWM duty without inversion. The SDK
initialization selects a low initial level for zero duty and high for positive duty.

## Code Evidence

Addresses in this section are CPU addresses in the decrypted application. Subtract `0x10000`
to obtain offsets in `new_app_1.00_decrypted.bin`; these are not raw flash offsets.

| Address | Evidence |
| --- | --- |
| `0xD3674` | Five 32-bit frequency values, each 5000, followed by `rgbcw` |
| `0xF48F9` | Initialized pin records `{8,0,1}`, `{7,1,1}`, `{6,2,1}`, `{26,3,1}`, `{24,4,1}` |
| `0x1022C` | Startup copies initialized data from `0xF44F0` into RAM at `0x400100`; pin records land at `0x400509` |
| `0x581FC` | Creates five PWM outputs from those records and converts frequency to nanosecond period |
| `0x582A8` | Scales five channel values by 1/512 and forwards duties to the PWM devices |
| `0x58938` | Parses HSV color data and stores RGB values in RAM at `0x4082D8` |
| `0x584A8` | White mixer; writes temperature-scaled first white and complementary second white |
| `0x5A0C8–0x5A0D6` | Sets combined white budget to 100 and channel scale to 512 |
| `0x5F058` | SDK PWM initialization converts period to 26 MHz timer ticks |
| `0x54C1C` | Low-level PWM initialization selects initial level from duty |

## Offline Verification

Selected functions were executed in Unicorn 2.1.4 with an ARM926 model supporting the image's
ARM/Thumb code. The application's initialized RAM was copied from its actual startup data.
The channel scale and white budget were set to the values explicitly assigned by application init.
Device lookup, logging, and hardware PWM operations were intercepted. No serial port, physical
peripheral, full boot, compilation, or flashing was involved.

Six full-saturation/full-value HSV probes produced the expected red, yellow, green, cyan, blue,
and magenta channel combinations. Each participating RGB channel reached duty 1.0; there was no
green/blue reduction in the tested path. In particular:

| Probe | P8 | P7 | P6 | P26 | P24 |
| --- | --- | --- | --- | --- | --- |
| Red, hue 0 | 100% | 0% | 0% | 0% | 0% |
| Green, hue 120 | 0% | 100% | 0% | 0% | 0% |
| Blue, hue 240 | 0% | 0% | 100% | 0% | 0% |
| White, temperature 0 | 0% | 0% | 0% | 0% | 100% |
| White, temperature 500 | 0% | 0% | 0% | 50% | 50% |
| White, temperature 1000 | 0% | 0% | 0% | 100% | 0% |

At white brightness input 10/1000 and temperature 500, the mixer produces 25 and 26 out of 512,
approximately 10% combined output. This stock minimum-brightness remapping differs from simply
interpreting the Tuya brightness number as linear duty. These probes do not establish optical
gamma, every scene/remote mode, or complete equivalence to WL2H.

## Replacement Implications

A replacement for this unit needs the new pin map and can use the existing Beken
`libretiny_pwm` component at 5 kHz. The white midpoint supports `constant_brightness: true`.
The old CBU YAML's green/blue power limits should not be copied without a separate calibration
reason. Its mapping would route four channels incorrectly on this revision.

The existing CBU configuration and saved JSON are now named V1 (CBU), with their settings
unchanged. V1/V2 are owner-defined labels for the older/newer assemblies, not manufacturer
revision numbers.

The replacement configuration is
[`devices/tuya-ceiling-light-v2-cbu.yaml`](../../../devices/tuya-ceiling-light-v2-cbu.yaml).
Both V2 device files use the shared
[`tuya-ceiling-light-v2` appliance](../../../packages/appliances/tuya-ceiling-light-v2.yaml),
passing their `chip` substitution as `ceiling_light.chip`. The appliance selects `cbu` or `wl2h`
hardware and provides one shared RGBWW light. Unsupported module names fail configuration.
Configuration validation does not replace the outstanding CBU V2 hardware test.
