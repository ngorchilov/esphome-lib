# BK7231N BLE command server

A single-instance ESPHome external component for CBU/BK7231N controllers.
It advertises one writable GATT characteristic. Only an exact three-byte match
to the configured `command` reaches `on_write`, as a `uint32_t value` on ESPHome's
main task. Pairing and link encryption are disabled; the command is sent without
encryption.

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

Names of up to eight ASCII characters, including `Garage 7` and `Garage 8`, are
included in the primary advertisement alongside the complete service UUID.
The phone can learn both from one packet, without a scan-response exchange just
to obtain the name. The complete name remains in the scan response as well;
longer names (including names extended by the MAC suffix) use that response only.
The legacy advertisement stays within 31 bytes. This reduces discovery exchanges,
but does not increase radio power or improve the physical link through a metal door.

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

The garage reads its command from `!secret tuya_garage_opener_ble_command`.
Define that key in your local `devices/secrets.yaml` as an unsigned 24-bit
hexadecimal number (`0x` followed by six hex digits). This file is ignored by Git;
there is no public default command. The garage-specific package
`packages/modules/tuya-garage-opener-ble.yaml` loads the secret only when BLE is
enabled. With BLE disabled (the default), that key is not required. Enabling BLE
without the key fails configuration with a missing-secret error.
For ESPHome Dashboard, put this secret in the dashboard's own `secrets.yaml`,
beside the consuming device configuration. The optional module fetches the BLE
C++ component from this repository's `main` branch, so remote package consumers
do not need a separate local `components` directory.

The device exposes `ble_enabled`, `ble_name`, `ble_name_add_mac_suffix` and `ble_command`
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
No `-s ble_command` argument is needed when using the secret. For different
commands per controller, override the `ble_command` substitution with another
`!secret` in the consuming YAML. The CLI override remains supported, but puts the value in shell
history and process arguments. Keep each shortcut's six hexadecimal digits matched
to its controller's configured command.

| Field | Value |
|---|---|
| Service UUID | `c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001` |
| Writable characteristic UUID | `c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a002` |
| Garage command | Private `tuya_garage_opener_ble_command` secret, exactly three bytes |
| Properties | Write with response and write without response |
| Pairing / encryption | Disabled; command sent without encryption |

The command runs the same `cover.toggle: garage_door` action as the physical
button. The cover chooses the direction and updates its operation state and timer
before invoking the existing 0.2-second relay pulse. It ignores toggles while
already moving. Additional BLE requests are suppressed until the command script
returns and one second has elapsed. The component requires `command` as an unsigned
24-bit number, passed through the module's required `ble_pulse.command` input.
The enabled garage package defines `ble_command: !secret tuya_garage_opener_ble_command`
directly under `substitutions`. The `ble_command` CLI substitution can override it,
but the secret key must still exist when BLE is enabled because YAML resolves
`!secret` before applying CLI substitutions.
Neither the module nor the C++ server supplies a fallback token. Bytes are
sent most significant first, without a leading zero byte, `0x` prefix or text encoding.
This is a fixed command token to reduce accidental activation, not authentication
or replay protection. Controllers using the same secret share a token and have
distinct peripherals; each can use a different secret if its shortcut sends the
matching three bytes. Moving an existing value to `secrets.yaml` preserves it;
it does not rotate values already published in Git history.

### Wrong writes and migration

The old one-byte `01` is no longer accepted. Empty writes, partial commands,
wrong values, reversed bytes, ASCII hex text, trailing data, null buffers and
invalid connection indices are ignored. Writes to other profiles or attributes
cannot dispatch a command. Individual writes are never accumulated into a command.
The callback checks the length before accessing any payload bytes and copies only
the validated number; it never retains the SDK's temporary buffer.

Rejected payloads do not allocate an event or enter the component's event queue.
A single-producer counter reports their total at most once every five seconds on
the main task, without logging payload contents. Bad writes do not extend the
connection lifetime or disable advertising/recovery. Queue overflow still discards
the affected batch's valid commands, and OTA/pending recovery still suppress commands.

BDK 3.0.78 generates write acknowledgements independently of this application
callback and exposes no write-rejection status there. Therefore, a wrong value can still receive a successful
GATT acknowledgement even though no automation runs. The characteristic's maximum
length is three bytes; the callback also checks it defensively. The SDK does not
expose a write offset in `write_req_t`; these checks validate the payload delivered
by the stack rather than claiming to validate hidden ATT transaction details.
An acknowledged GATT write does not prove the command was accepted or the door moved.

Flash the new firmware before changing each live shortcut's **Write Value → Hex**
from `01` to the secret's six hex digits, keeping **Value Type → hex**. The new shortcut value will not
operate the old firmware. The UUIDs, discovery names and action wiring stay the same.

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
Advertising operations are sequenced and checked against SDK state. OTA suspends
commands and recovery reboots.

Include `wifi:` and leave `wifi.enable_on_boot` enabled. LibreTiny initializes
shared radio/network services through Wi-Fi startup. A successful Wi-Fi
connection is not required for BLE commands. Only one BLE client can connect at
a time. With an unlimited connection timeout, disconnect inspection apps when
finished so other clients can discover and connect to the controller.

## Offline recovery

The 2026-09-15 update disables BLE sleep with BDK 3.0.78's public
`ble_ps_enable_clear()` API, before starting the native BLE backend. BDK's BLE
sleep flag defaults to enabled independently of Wi-Fi power saving; its BLE
thread checks that flag before calling `rwip_sleep()`. Keeping this mains-powered
server awake avoids that sleep path and can increase power consumption. This is
a preventive measure, not a confirmed explanation of the field failures. No SDK
patch, pairing support, encryption, or Wi-Fi configuration change is included.
Reassess the sleep workaround after an SDK update and an offline hardware soak
test; the underlying implementation is
[`app_ble.c` in BDK 3.0.78](https://github.com/libretiny-eu/framework-beken-bdk/blob/3.0.78/beken378/driver/ble/ble_5_1/ble_pub/app/src/app_ble.c).

Recovery now works without a network or a person reading diagnostics:

- Reconcile the actual SDK connection table even if connect/disconnect notices
  are lost. After the configured connection limit, retry an incomplete disconnect
  once per second for up to 30 seconds. An SDK success return only means queued.
- Restart stopped advertising, or recreate a lost advertising activity and
  restore its UUID and name. When the SDK is ready, recover missing advertising
  callbacks from its state or retry idempotent data setters.
- Limit callback processing to 15 events per main-loop iteration. Queue overflow
  discards buffered writes in the affected batch and reconciles SDK state; it
  no longer permanently disables the server. Commands are never replayed by
  recovery. A caller may need to retry a dropped command.
- If a database operation, advertising operation, or connection release remains
  stuck for 30 seconds, request an ESPHome safe reboot. Do not reset the SDK's
  bookkeeping while an operation is in flight. The reboot waits until BLE startup
  is at least 120 seconds old, beyond the garage's default 60-second healthy-boot
  check. This allows Wi-Fi maintenance time and avoids accumulating rapid failed
  boots. With a persistent fault, recovery can repeat at that interval.
- The garage additionally waits for the relay pulse to finish and for the cover
  to become idle. The component's optional `reboot_condition` lambda supplies
  this guard; the module accepts its C++ boolean expression under
  `ble_pulse.reboot_condition`. Other consumers without a guard permit recovery
  after the startup grace. New BLE commands are ignored once a reboot is pending.
- OTA prevents recovery reboots throughout an upload. An aborted or failed upload
  resumes recovery with fresh operation deadlines.

A fallback reboot restarts the whole controller, including Wi-Fi, and briefly
interrupts control. Wi-Fi initialization, scanning, reconnect attempts and OTA
availability otherwise keep their existing behavior. Neither missing Wi-Fi nor
an absence of BLE clients is itself a reboot trigger.

These checks detect inconsistent or stalled SDK state. They cannot prove packets
are being transmitted if the SDK reports a healthy advertiser while the radio
itself is silent. Ordinary logs record recovery reasons for bench testing; there
is no new text sensor or persistent diagnostic history to retrieve months later.

### Recovery validation

`tests/test_recovery.py` compiles the production C++ server with the installed
ESPHome EventPool/LockFreeQueue, a simulated clock and SDK, and address/undefined
behavior sanitizers. Run it with the Python interpreter that has ESPHome installed:

```sh
python components/bk72xx_ble_server/tests/test_recovery.py
```

The tests cover 24 simulated idle hours, repeated connect/write/disconnect cycles,
missing callbacks, lost advertising activity, stuck SDK operations, queue bursts,
connection timeouts, millisecond-clock rollover, application reboot guards and OTA.
Advertisement checks cover short names, the eight-character boundary, longer names,
MAC suffixes, complete UUID retention and restoring both payloads after recovery.
Command checks cover every one-byte value, every single-byte mutation of the token,
malformed buffers, wrong attributes, exact value delivery and 10,000 rejected writes
without filling the event queue or flooding warning logs.
These are software fault-injection tests, not an RF or hardware soak test.

## Hardware validation

Tests on 2026-09-13 used ESPHome 2026.8.1, LibreTiny 1.13.0 and BDK 3.0.78 on
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

Subsequently, the user verified cold starts with the access point unavailable
(duration unmeasured) and initial operation in both garages. Both controllers
were undiscoverable when checked about six hours later; the actual failure time
and trigger are unknown.

On 2026-09-15, the user flashed the recovery update to both installed controllers
and moved them below the opener housings. Both work close up and with the doors
open; at about two metres with a closed door, the shortcut can fail during scan,
service discovery or writing. Captured logs show the configured five-second
connection releases followed by successful advertising restarts. The app's
"Characteristic is read only" error does not establish an ATT permission failure;
the firmware's command characteristic still supports both write modes. These
observations suggest a marginal RF link, without identifying the app's error path.
The subsequent primary-advertisement name and three-byte command changes passed
host tests and build checks. The user then flashed the ground-floor controller
with BLE name `Garage G` and verified door operation using the three-byte command
from an iPhone shortcut. Long-term offline operation of this version remains
unverified. The earlier bench tests above used the old `01` command.

## iPhone control

For free manual testing, [nRF Connect for Mobile](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403)
can connect to the configured BLE name and write the secret's three bytes in hex to
the characteristic above.
No Bluetooth Settings pairing is needed.

[Bluetooth Inspector Pro](https://apps.apple.com/us/app/bluetooth-inspector/id1509085044)
advertises a Shortcuts **Write Value** action. Each shortcut must select its own
peripheral because service and characteristic UUIDs are shared. No free generic
BLE-write Shortcuts provider has been verified; nRF Connect is a manual tester.

The developer's [Shortcuts example](https://is1-ssl.mzstatic.com/image/thumb/PurpleSource211/v4/6a/52/86/6a528656-dcf9-5f3a-74bf-11158c6daab6/iPhone-Large-6.png/1000x2000bb.jpg)
uses Scan, Filter, Interrogate and Enumerate. The garage shortcut connects them
as follows; names in parentheses identify action outputs, not literal text:

1. Scan Bluetooth discovery → **Peripherals**.
2. Filter **Peripherals** where Name is the configured BLE name; limit to one → **Files**.
3. Get First Item from **Files** → **Garage** (renamed output).
4. Interrogate **Files** → **Services**.
5. Enumerate **Services** characteristics for **Garage** → **Characteristics**.
6. Write **Characteristics** value to **Garage**, Value Type **hex**, Hex set to the
   secret's six hexadecimal digits without the `0x` prefix.

The current server has exactly one service and one characteristic. `Files` is
the generic Filter action's output label and is also used in the developer's
example; it does not mean selecting a file from disk. Disable **Show When Run**
on BT Inspector actions for unattended execution. iOS assigns its own peripheral
identifier, so use discovery outputs instead of copying the Mac's identifier.

The original working shortcut used Intensive discovery. Ordinary Scan is now
selected in both Mac shortcuts for a field comparison: Intensive continuously
updates signal strength and device information, which this name lookup does not
need. An improvement in execution time has not yet been measured. The remaining
action chain is retained: the installed app's Write Value action requires its
typed peripheral and characteristic objects, not text UUID parameters. A shorter
replacement has not been verified. Do not automatically retry the final toggle
write on timeout: it may have reached the controller even when the app reports failure.

Sources:

- [ESPHome 2026.8.0 Beken BLE backend](https://github.com/esphome/esphome/blob/2026.8.0/esphome/components/bk72xx_ble/__init__.py)
- [ESPHome Beken BLE documentation](https://esphome.io/components/bk72xx_ble/)
- [BDK 3.0.78 BLE API](https://github.com/libretiny-eu/framework-beken-bdk/blob/3.0.78/beken378/driver/include/ble_api_5_x.h)
