# Tuya Ceiling Light — WL2H Firmware Analysis

This is a reconstruction from executable code, not an extracted `user_param_key` object.
It applies to the inspected WL2H-U/LN882H revision. The existing CBU device YAML and its
`user_param_key` JSON describe a different revision.

## Source

- Analysis date: 2026-09-16.
- Stock firmware version reported in Tuya storage: `1.6.2`.
- Full flash dump size: 2,097,152 bytes.
- SHA-256: `5d34e8185372e00c27889f9421413dcbe63c1254e526e5f9f4e00ae1f5003929`.
- Architecture: ARM Cortex-M4 Thumb; executable addresses are `0x10000000 + file offset`.

The dump remains outside this repository. Do not commit firmware dumps, credentials, device
identifiers, decrypted storage contents, or raw extraction logs.

The protected block at `0x1EB000`, key block at `0x1EC000`, and all 19 encrypted data blocks from
`0x1ED000` to `0x200000` were inspected. Decryption and data-block checksums succeed, but no
`user_param_key` entry or embedded hardware-profile JSON was found. The SDK's parameter writer
references that string at file offset `0xECF38`; its presence does not establish a stored profile.
See the [storage findings](../user-param-key/README.md#profiles-not-found).

## Recovered Outputs

The owner reports only the bottom module pads are soldered. The five signal pads match the
[Tuya WL2H-U datasheet](https://developer.tuya.com/en/docs/iot/WL2H-U-Module-Datasheet?id=Kbohlj8eg19u5)
and the firmware's pin translation exactly:

| Module pad | MCU pin | Stock logical pin | Recovered role | Evidence |
| --- | --- | --- | --- | --- |
| 8 | PA07 | 8 | Red | Hue 0 drives this output only |
| 9 | PA10 | 7 | Green | Hue 120 drives this output only |
| 10 | PA11 | 6 | Blue | Hue 240 drives this output only |
| 11 | PA12 | 26 | White, temperature endpoint 1000 | White mixer at 1000 drives this output only |
| 12 | PB03 | 24 | White, temperature endpoint 0 | White mixer at 0 drives this output only |

PA12 is provisionally cold white and PB03 warm white, assuming increasing mixer temperature
means increasing color temperature. The numerical endpoints are verified; the emitted white
colors and their Kelvin limits have not been measured. The mixer was tested directly, so this
does not verify every transformation between cloud commands and its input.

All five channels initialize at **4,000 Hz**, with a duty range of **0–10,000** and initial duty
zero. The stock driver's endpoint paths use low for zero and high for full duty, establishing
active-high MCU outputs. This does not require copying the CBU configuration's PWM aliases,
1,500 Hz frequency, or green/blue power limits.

An additional code path at `0xCA4FE–0xCA512` changes the white channels to 800 Hz (logical 26)
and 300 Hz (logical 24). Its triggering mode has not been identified; 4,000 Hz is the verified
initialization frequency, not a claim about every stock operating mode.

## Code Evidence

All addresses below are file offsets unless explicitly described as peripheral addresses.
Function names describe inferred behavior; these are not recovered debug symbols.

| Offset | Behavior |
| --- | --- |
| `0xD74D0` | Translates logical pins 6, 7, 8, 24, 26 to LN882H GPIO port/mask pairs |
| `0xC95C4–0xC95D0` | Initializes logical 26/24 white pair through `0xD72D0`, frequency 4000, scale 10000 |
| `0xC95F2–0xC961C` | Initializes logical 8, 7, 6 RGB outputs through `0xD7278`, same frequency and scale |
| `0xD7140` | Allocates a PWM slot and fills its driver descriptor using the pin translator |
| `0xCC6BC` | Converts HSV to three RGB duty values |
| `0xCABB8` | Sends R, G, B to logical 8, 7, 6 through `0xD7190` |
| `0xCC8D4` | Computes the white pair from temperature and brightness |
| `0xC9304` | Sends the first white value to logical 26 and the second to 24 through `0xD7354` |
| `0xB818` | PWM start logic, including static GPIO handling at zero/full duty |
| `0x1945C`, `0x19460` | GPIO set/reset helpers used by the full/zero duty paths |

The pin translator returns GPIOA base `0x4000C000` with masks `0x0080`, `0x0400`, `0x0800`,
and `0x1000` for PA07, PA10, PA11, and PA12 respectively. PB03 uses GPIOB base `0x4000C400`
and mask `0x0008`. These match the LN882H SDK's GPIO register definitions.

## Offline Verification

Selected stock functions were executed using Unicorn 2.1.4 in Cortex-M4 Thumb mode with flash,
RAM, and floating-point registers initialized. PWM writes were intercepted at the software
wrappers; no serial port or physical peripheral was accessed. These are isolated function
probes, not a complete emulated boot or physical light test.

RGB probes use saturation/value 1000 and output scale 10000. The RGB wrapper's aggregate limit
was populated with 280, matching the inspected default; other application initialization was
not emulated. Full single-color results:

| HSV hue | PA07 / red | PA10 / green | PA11 / blue |
| --- | --- | --- | --- |
| 0 | 10000 | 0 | 0 |
| 120 | 0 | 10000 | 0 |
| 240 | 0 | 0 | 10000 |

White probes call the mixer with mode 0, combined white power 100, brightness 1000,
input scale 1000, and output scale 10000:

| Mixer temperature | PA12 | PB03 |
| --- | --- | --- |
| 0 | 0 | 10000 |
| 500 | 5000 | 5000 |
| 1000 | 10000 | 0 |

Static disassembly independently confirms the initialization constants, call order, GPIO
translation, and output polarity. The original dump's hash was unchanged after analysis.

## Replacement Firmware Considerations

- Preserve RGB plus independently mixed white channels as the functional target.
- Verify the physical white endpoints before treating the provisional cold/warm labels as final.
- Do not infer optical calibration, minimum brightness, Kelvin limits, or all mode behavior from
  the isolated probes. The CBU device's 2700–6500 K range remains a comparison, not a measurement
  of this revision.
- The inspected LibreTiny 1.13.0 LN882H core does not provide an `analogWrite` implementation;
  its WL2H variant advertises zero analog outputs. The successful board-only kickstart build did
  not exercise PWM. The new
  [`ln882h_pwm` component](../../../components/ln882h_pwm/README.md) integrates the SDK's
  advanced timers without modifying LibreTiny.

The replacement configuration is
[`devices/tuya-ceiling-light-wl2h.yaml`](../../../devices/tuya-ceiling-light-wl2h.yaml).
It uses the recovered 4 kHz pin mapping and provisional white assignments, with the CBU variant's
RGBWW entity behavior and 2700–6500 K UI range. It does not copy the CBU's green/blue power limits.
White mixing uses `constant_brightness: true` to keep combined white duty within one channel's
full output, consistent with the recovered mixer's 50/50 midpoint.
Validation is limited to `esphome config`; compilation and hardware testing are owner-run steps.

No replacement application firmware has been flashed as part of this analysis.
