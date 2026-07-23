from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor, tuya, uart
from esphome.components.tuya import CONF_TUYA_ID
from esphome.const import CONF_ID, CONF_UART_ID, ENTITY_CATEGORY_DIAGNOSTIC

from .. import TuyaProductComponent

DEPENDENCIES = ["tuya", "uart"]
AUTO_LOAD = ["json"]

CONF_ON_INITIALIZED = "on_initialized"
CONF_PRODUCT_ID = "product_id"
CONF_MCU_VERSION = "mcu_version"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TuyaProductComponent),
            cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.GenerateID(CONF_TUYA_ID): cv.use_id(tuya.Tuya),
            cv.Optional(CONF_ON_INITIALIZED): automation.validate_automation({}),
            cv.Optional(CONF_PRODUCT_ID): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MCU_VERSION): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_INITIALIZED, "add_on_initialized_callback"
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart_parent(parent))

    tuya_parent = await cg.get_variable(config[CONF_TUYA_ID])
    cg.add(var.set_tuya_parent(tuya_parent))

    for key in [CONF_PRODUCT_ID, CONF_MCU_VERSION]:
        if conf := config.get(key):
            sensor = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, f"set_{key}_sensor")(sensor))

    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)

    cg.add_define("USE_UART_DEBUGGER")
