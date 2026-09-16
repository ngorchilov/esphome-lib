from esphome import pins
import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_FREQUENCY, CONF_ID, CONF_PIN, CONF_PLATFORM
import esphome.final_validate as fv

DEPENDENCIES = ["ln882x"]
CONF_TIMER = "timer"

ln882h_pwm_ns = cg.esphome_ns.namespace("ln882h_pwm")
LN882HPWM = ln882h_pwm_ns.class_("LN882HPWM", output.FloatOutput, cg.Component)

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(
    {
        cv.Required(CONF_ID): cv.declare_id(LN882HPWM),
        cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
        cv.Required(CONF_TIMER): cv.int_range(min=0, max=5),
        cv.Optional(CONF_FREQUENCY, default="4kHz"): cv.All(
            cv.frequency, cv.float_range(min=100, max=20000)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _validate_timer(config):
    for other in fv.full_config.get().get("output", []):
        if (
            other.get(CONF_PLATFORM) == "ln882h_pwm"
            and other[CONF_ID] != config[CONF_ID]
            and other[CONF_TIMER] == config[CONF_TIMER]
        ):
            raise cv.Invalid("Each ln882h_pwm output must use a different timer")
    return config


FINAL_VALIDATE_SCHEMA = _validate_timer


async def to_code(config):
    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    var = cg.new_Pvariable(config[CONF_ID], pin)
    await cg.register_component(var, config)
    await output.register_output(var, config)
    cg.add(var.set_timer(config[CONF_TIMER]))
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
