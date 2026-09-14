# BK7231N BLE command server

A single-instance ESPHome external component for CBU/BK7231N controllers.
It advertises one writable GATT characteristic; `on_write` receives a single
`uint8_t value` on ESPHome's main task. Pairing is not required.

## Garage configuration

`devices/tuya-garage-opener.yaml` is the only garage firmware entrypoint.
BLE is disabled by default. Enable it and choose a name through include vars:

```yaml
packages:
  - !include
      file: tuya-garage-opener.yaml
      vars:
        ble:
          enabled: true
          name: Garage One
```

`ble.enabled: false` excludes the BLE component, SDK selection, command handler,
and pulse wrapper. `packages/modules/bk72xx-ble-pulse.yaml` owns the optional
feature; the device supplies its shared `toggle_door` script through
`ble_pulse.command_script_id`. The former `pulse_script_id` field remains an alias
for other callers. The module uses a `ble_pulse` vars object and also
accepts a script-id prefix under `ble_pulse.id`.

The BLE name defaults to `friendly_name`. `ble.name` or the `ble_name` CLI
substitution can override it. `ble.name_add_mac_suffix` (CLI:
`ble_name_add_mac_suffix`) defaults to `false`, so `Garage 7` advertises as
`Garage 7`. Set it to `true` to append a hyphen and the final six lowercase
hexadecimal digits of the BLE MAC, following ESPHome's hostname suffix format:
`Garage 7-90bad0`. It controls only the BLE name and uses the BLE MAC, which may
differ from the Wi-Fi MAC used for ESPHome's hostname suffix.

BDK 3.0.78 limits the complete name to 17 ASCII characters. An inherited friendly
name is shortened to 17 characters without the suffix, or 10 with it; trailing
whitespace is removed. Explicit BLE names must fit those limits and are not
shortened. The external component also exposes `name_add_mac_suffix` directly,
and the pulse module accepts it under `ble_pulse.name_add_mac_suffix`.

The garage defaults `ble.connection_timeout` (CLI: `ble_connection_timeout`) to
`5s`. This limits each BLE connection's lifetime, then resumes advertising.
Set it to `0s` for unlimited connections, for example during manual inspection.
The standalone component and pulse module default to `0s`; the module exposes
the setting under `ble_pulse.connection_timeout`.

This is a workaround for Bluetooth Inspector's separate Shortcuts actions.
On the tested Mac app (1.7.1), Enumerate creates a new CoreBluetooth manager and
looks for the peripheral in that manager's discovery cache. A connection held
by the preceding action stops this controller advertising, so Enumerate waits;
Write can encounter the same problem. Releasing the previous connection let
each action advance during diagnosis. The server uses BDK 3.0.78's
`bk_ble_disconnect()` to release the connection without a reboot or any change
to Wi-Fi. No dependency is patched. Recheck this workaround after an app update;
set the timeout to `0s` when the full shortcut works without forced disconnects.

### Command-line builds

The device exposes `ble_enabled`, `ble_name`, and `ble_name_add_mac_suffix`
substitutions for ESPHome's `-s` CLI option, alongside `name` and `friendly_name`.
CLI values override the include vars above. Both BLE flags accept `true` or
`false` and default to false.
From the repository root:

```sh
cd devices
esphome -s name garage-1 -s friendly_name "Garage 1" \
  -s ble_enabled true \
  config tuya-garage-opener.yaml
esphome -s name garage-1 -s friendly_name "Garage 1" \
  -s ble_enabled true \
  compile tuya-garage-opener.yaml
```

Use `garage-2` and `Garage 2` for the second controller. Add `-s ble_name "Garage One"`
only when a different BLE name is wanted. Each
`name` gets its own `.esphome/build/<name>/.pioenvs/<name>/firmware.uf2` output
under `devices/`. Keep the same `-s` arguments when replacing `compile` with
`upload` and adding `--device <controller-IP>` so ESPHome selects the right build.
Add `-s ble_name_add_mac_suffix true` to opt into the MAC suffix.
Add `-s ble_connection_timeout 0s` to disable the connection limit.

| Field | Value |
|---|---|
| Service UUID | `c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001` |
| Writable characteristic UUID | `c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a002` |
| Garage command | Hex `01`, exactly one byte |
| Properties | Write with response and write without response |
| Pairing / encryption | Not required |

The command runs the same `cover.toggle: garage_door` action as the physical
button. The cover chooses the direction and updates its operation state and timer
before invoking the existing 0.2-second relay pulse. It ignores toggles while
already moving. Additional BLE requests are suppressed until the command script
returns and one second has elapsed. Other command values are ignored. An acknowledged
GATT write means the stack received it; it does not prove the door moved.

## Version and SDK requirements

The optional BLE package requires **ESPHome 2026.8.0** because it uses the native
`bk72xx_ble` backend introduced in that release ([upstream PR #17775](https://github.com/esphome/esphome/pull/17775),
[release notes](https://esphome.io/changelog/2026.8.0/#new-components)). This
requirement concerns the Beken backend, rather than BLE support in general.
Its Beken controller interfaces, EventPool,
LockFreeQueue, and OTA listener interfaces used here match those in 2026.8.1.
The 2026.8.1 backend changes concern BK7238; this server accepts only BK7231N.
Build and hardware validation used 2026.8.1; 2026.8.0 compatibility was checked
against those tagged source interfaces, without a separate 2026.8.0 build.
When BLE is disabled, the garage retains the shared board's minimum version.

Native `bk72xx_ble` selects Beken BDK **3.0.78**. The bundled 3.0.33 has an older
BLE layout that can crash during startup. No SDK or ESPHome source is patched.
The GATT implementation uses BDK 3.0.78's BLE 5.1 interfaces and needs revalidation
if that SDK changes.

The server takes ownership of Beken's single `ble_set_notice_cb` callback after
native stack initialization. Consequently `bk72xx_ble_tracker` and
`bluetooth_proxy` are incompatible. SDK callbacks copy events into ESPHome's
EventPool/LockFreeQueue; logging and automations execute on the main task.
Advertising operations are sequenced with a startup timeout. OTA suspends
commands, and a BLE failure does not reboot the controller.

Include `wifi:` and leave `wifi.enable_on_boot` enabled. LibreTiny initializes
shared radio/network services through Wi-Fi startup. A successful Wi-Fi
connection is not required for BLE commands. Only one BLE client can connect at
a time. With an unlimited connection timeout, disconnect inspection apps when
finished so other clients can discover and connect to the controller.

## Hardware validation

Tests on 2026-09-13 used ESPHome 2026.8.1, LibreTiny 1.13.0 and BDK 3.0.78 on the
a power-only bench CBU. Discovery, both write modes, invalid writes,
burst suppression and reconnection passed. With Wi-Fi disabled after startup:

| Requested outage | Measured interval | Command writes | Completed pulse scripts |
|---|---:|---:|---:|
| 1 minute | 60.034 seconds | 6 | 4 |
| 5 minutes | 300.024 seconds | 6 | 4 |

Both runs automatically restored Wi-Fi and returned the stored result with no
BLE errors or unexpected Wi-Fi availability. The user heard relay clicks.
The final main image also passed a single-pulse check and ESPHome's healthy-boot
check. Temporary firmware variants and test harnesses were removed afterward.
Cold boot without an access point, iPhone Shortcuts execution, and basement
range have not been verified.

On 2026-09-14, the 5-second connection limit passed configuration checks with BLE
enabled, disabled, and with an unlimited timeout. The enabled firmware compiled
and was deployed by OTA to the same controller. Bluetooth Inspector 1.7.1's
six-action Garage 7 shortcut completed in the Mac Shortcuts editor, returned
`01`, and produced exactly one logged BLE write and completed relay pulse.
An earlier CLI invocation also produced one pulse in about 22 seconds, but the
`shortcuts run` process did not exit within 45 seconds and was stopped; CLI
completion is not validated. The successful editor run needed no manual
disconnect, restart, or intervention. iPhone execution remains to be tested.

Later on 2026-09-14, BLE was changed to use the physical button's shared cover
toggle script instead of calling the relay directly. BLE-enabled and disabled
configuration checks passed; the enabled image compiled and was deployed by OTA.
On the same bench controller, a BLE `01` changed the API cover operation from
IDLE to IS_CLOSING. A second `01`, sent after the BLE rate limit expired, reached
the cover and was ignored because it was already moving. This verifies command
routing and reported operation state, not physical door movement.

## iPhone control

For free manual testing, [nRF Connect for Mobile](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403)
can connect to the configured BLE name and write hex `01` to the characteristic above.
No Bluetooth Settings pairing is needed.

[Bluetooth Inspector Pro](https://apps.apple.com/us/app/bluetooth-inspector/id1509085044)
advertises a Shortcuts **Write Value** action. Each shortcut must select its own
peripheral because service and characteristic UUIDs are shared. No free generic
BLE-write Shortcuts provider has been verified; nRF Connect is a manual tester.

The developer's [Shortcuts example](https://is1-ssl.mzstatic.com/image/thumb/PurpleSource211/v4/6a/52/86/6a528656-dcf9-5f3a-74bf-11158c6daab6/iPhone-Large-6.png/1000x2000bb.jpg)
uses Scan, Filter, Interrogate and Enumerate. The garage shortcut connects them
as follows; names in parentheses identify action outputs, not literal text:

1. Intensive Bluetooth discovery → **Peripherals**.
2. Filter **Peripherals** where Name is the configured BLE name; limit to one → **Files**.
3. Get First Item from **Files** → **Garage** (renamed output).
4. Interrogate **Files** → **Services**.
5. Enumerate **Services** characteristics for **Garage** → **Characteristics**.
6. Write **Characteristics** value to **Garage**, Value Type **hex**, Hex **01**.

The current server has exactly one service and one characteristic. `Files` is
the generic Filter action's output label and is also used in the developer's
example; it does not mean selecting a file from disk. Disable **Show When Run**
on BT Inspector actions for unattended execution. iOS assigns its own peripheral
identifier, so use discovery outputs instead of copying the Mac's identifier.

Sources:

- [ESPHome 2026.8.0 Beken BLE backend](https://github.com/esphome/esphome/blob/2026.8.0/esphome/components/bk72xx_ble/__init__.py)
- [ESPHome Beken BLE documentation](https://esphome.io/components/bk72xx_ble/)
- [BDK 3.0.78 BLE API](https://github.com/libretiny-eu/framework-beken-bdk/blob/3.0.78/beken378/driver/include/ble_api_5_x.h)
