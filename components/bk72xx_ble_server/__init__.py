"""Single-characteristic BLE command server for BK7231N / BDK 3.0.78."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.components import bk72xx_ble, libretiny, ota
from esphome.components.libretiny.const import FAMILY_BK7231N
from esphome.const import CONF_ID, CONF_NAME, CONF_NAME_ADD_MAC_SUFFIX

CONF_ON_WRITE = "on_write"
CONF_CONNECTION_TIMEOUT = "connection_timeout"

AUTO_LOAD = ["bk72xx_ble"]
DEPENDENCIES = ["bk72xx", "wifi"]
CONFLICTS_WITH = ["bk72xx_ble_tracker", "bluetooth_proxy"]
CODEOWNERS = ["@ngorchilov"]

ns = cg.esphome_ns.namespace("bk72xx_ble_server")
BLECommandServer = ns.class_("BLECommandServer", cg.Component)


def validate_name(value):
    value = cv.string_strict(value)
    # BDK 3.0.78 accepts names shorter than 18 bytes.
    if not value.isascii() or not 1 <= len(value) <= 17:
        raise cv.Invalid("name must contain 1–17 ASCII characters (BDK device-name limit)")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BLECommandServer),
        cv.GenerateID("bk72xx_ble_id"): cv.use_id(bk72xx_ble.BK72xxBLE),
        cv.Optional(CONF_NAME, default="BLE Command"): validate_name,
        cv.Optional(CONF_NAME_ADD_MAC_SUFFIX, default=False): cv.boolean,
        cv.Optional(CONF_CONNECTION_TIMEOUT, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_WRITE): automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


def final_validate(config):
    if config[CONF_NAME_ADD_MAC_SUFFIX] and len(config[CONF_NAME]) > 10:
        raise cv.Invalid(
            "name must contain at most 10 ASCII characters when name_add_mac_suffix is true "
            "(17-byte BDK limit includes the seven-byte MAC suffix)",
            path=[CONF_NAME],
        )
    if libretiny.get_libretiny_family() != FAMILY_BK7231N:
        raise cv.Invalid("bk72xx_ble_server currently supports BK7231N only")
    if not fv.full_config.get()["wifi"]["enable_on_boot"]:
        raise cv.Invalid(
            "Leave wifi.enable_on_boot enabled: LibreTiny initializes shared radio/network "
            "services through Wi-Fi startup. A Wi-Fi connection is not required."
        )
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config["bk72xx_ble_id"])
    cg.add(var.set_parent(parent))
    cg.add(var.set_name(config[CONF_NAME]))
    cg.add(var.set_name_add_mac_suffix(config[CONF_NAME_ADD_MAC_SUFFIX]))
    cg.add(var.set_connection_timeout(config[CONF_CONNECTION_TIMEOUT].total_milliseconds))
    ota.request_ota_state_listeners()
    if conf := config.get(CONF_ON_WRITE):
        await automation.build_automation(var.get_write_trigger(), [(cg.uint8, "value")], conf)
