import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.components.const import CONF_DATA_BITS, CONF_PARITY, CONF_STOP_BITS
from esphome.const import CONF_BAUD_RATE, CONF_ID, CONF_RX_PIN, CONF_TX_PIN

DEPENDENCIES = ["rtl87xx"]
AUTO_LOAD = ["uart"]

rtl8720c_uart_ns = cg.esphome_ns.namespace("rtl8720c_uart")
RTL8720CUARTComponent = rtl8720c_uart_ns.class_(
    "RTL8720CUARTComponent", uart.UARTComponent, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RTL8720CUARTComponent),
        cv.Required(CONF_BAUD_RATE): cv.int_range(min=1),
        cv.Required(CONF_TX_PIN): pins.internal_gpio_output_pin_schema,
        cv.Required(CONF_RX_PIN): uart.validate_rx_pin,
        cv.Optional(CONF_STOP_BITS, default=1): cv.one_of(1, 2, int=True),
        cv.Optional(CONF_DATA_BITS, default=8): cv.int_range(min=5, max=8),
        cv.Optional(CONF_PARITY, default="NONE"): cv.enum(
            uart.UART_PARITY_OPTIONS, upper=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_baud_rate(config[CONF_BAUD_RATE]))
    cg.add(var.set_data_bits(config[CONF_DATA_BITS]))
    cg.add(var.set_parity(config[CONF_PARITY]))
    cg.add(var.set_stop_bits(config[CONF_STOP_BITS]))

    tx_pin = await cg.gpio_pin_expression(config[CONF_TX_PIN])
    cg.add(var.set_tx_pin(tx_pin))
    rx_pin = await cg.gpio_pin_expression(config[CONF_RX_PIN])
    cg.add(var.set_rx_pin(rx_pin))
