import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor, uart
from esphome.const import CONF_ID, CONF_UART_ID, ENTITY_CATEGORY_DIAGNOSTIC

from .. import TuyaProductComponent

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["json"]

CONF_PRODUCT_ID = "product_id"
CONF_MCU_VERSION = "mcu_version"
CONF_PAIRING_MODE = "pairing_mode"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TuyaProductComponent),
            cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_PRODUCT_ID): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_MCU_VERSION): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_PAIRING_MODE): text_sensor.text_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart_parent(parent))

    for key in [CONF_PRODUCT_ID, CONF_MCU_VERSION, CONF_PAIRING_MODE]:
        if conf := config.get(key):
            sensor = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, f"set_{key}_sensor")(sensor))

    cg.add_define("USE_UART_DEBUGGER")
