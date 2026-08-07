import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.const import CONF_DURATION, CONF_ID, CONF_PIN, Framework


MULTI_CONF = True
CODEOWNERS = ["@ngorchilov"]

magic_switch_ns = cg.esphome_ns.namespace("magic_switch")
MagicSwitch = magic_switch_ns.class_("MagicSwitch", cg.Component)
MagicSwitchMaskAction = magic_switch_ns.class_(
    "MagicSwitchMaskAction", automation.Action
)

CONF_ADAPTIVE_MARGIN = "adaptive_margin"
CONF_ADAPTIVE_MIN_PULSE = "adaptive_min_pulse"
CONF_DEBOUNCE = "debounce"
CONF_IRAM_SAFE_INTERRUPT = "iram_safe_interrupt"
CONF_MAX_PULSE = "max_pulse"
CONF_MIN_PULSE = "min_pulse"
CONF_ON_SWITCH = "on_switch"
CONF_PHASE_TOLERANCE = "phase_tolerance"
CONF_RECOVERY_PULSES = "recovery_pulses"
CONF_RECOVERY_TIMEOUT = "recovery_timeout"
CONF_STARTUP_MASK = "startup_mask"


def _validate_timing(config):
    min_pulse = config[CONF_MIN_PULSE].total_microseconds
    max_pulse = config[CONF_MAX_PULSE].total_microseconds
    adaptive_min = config[CONF_ADAPTIVE_MIN_PULSE].total_microseconds

    if min_pulse >= max_pulse:
        raise cv.Invalid("min_pulse must be shorter than max_pulse")
    if adaptive_min > min_pulse:
        raise cv.Invalid("adaptive_min_pulse must not exceed min_pulse")
    if config[CONF_PHASE_TOLERANCE].total_microseconds >= 4000:
        raise cv.Invalid("phase_tolerance must be shorter than 4ms")
    return config


def _validate_iram_safe_interrupt(config):
    if not config[CONF_IRAM_SAFE_INTERRUPT]:
        return config

    cv.only_on_esp32(config)
    cv.only_with_framework(Framework.ESP_IDF)(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(MagicSwitch),
            cv.Required(CONF_PIN): pins.internal_gpio_input_pullup_pin_schema,
            cv.Optional(CONF_MIN_PULSE, default="1ms"): cv.positive_time_period_microseconds,
            cv.Optional(CONF_MAX_PULSE, default="120ms"): cv.positive_time_period_microseconds,
            cv.Optional(CONF_IRAM_SAFE_INTERRUPT, default=False): cv.boolean,
            cv.Optional(
                CONF_ADAPTIVE_MIN_PULSE, default="750us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_ADAPTIVE_MARGIN, default="200us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_PHASE_TOLERANCE, default="1ms"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_RECOVERY_PULSES, default=2
            ): cv.int_range(min=1, max=10),
            cv.Optional(
                CONF_RECOVERY_TIMEOUT, default="150ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_STARTUP_MASK, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_DEBOUNCE, default="250ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ON_SWITCH): automation.validate_automation(single=True),
        }
    ),
    _validate_timing,
    _validate_iram_safe_interrupt,
)


async def to_code(config):
    if config[CONF_IRAM_SAFE_INTERRUPT]:
        cg.add_build_flag("-DUSE_MAGIC_SWITCH_IRAM_SAFE_INTERRUPT")
        cg.add_build_flag("-Wl,--wrap=gpio_install_isr_service")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_min_pulse_us(config[CONF_MIN_PULSE]))
    cg.add(var.set_max_pulse_us(config[CONF_MAX_PULSE]))
    cg.add(var.set_adaptive_min_pulse_us(config[CONF_ADAPTIVE_MIN_PULSE]))
    cg.add(var.set_adaptive_margin_us(config[CONF_ADAPTIVE_MARGIN]))
    cg.add(var.set_phase_tolerance_us(config[CONF_PHASE_TOLERANCE]))
    cg.add(var.set_recovery_pulses(config[CONF_RECOVERY_PULSES]))
    cg.add(var.set_recovery_timeout_ms(config[CONF_RECOVERY_TIMEOUT]))
    cg.add(var.set_startup_mask_ms(config[CONF_STARTUP_MASK]))
    cg.add(var.set_debounce_ms(config[CONF_DEBOUNCE]))

    if conf := config.get(CONF_ON_SWITCH):
        await automation.build_automation(
            var.get_switch_trigger(), [(cg.uint32, "pulse_us")], conf
        )


MAGIC_SWITCH_MASK_ACTION_SCHEMA = automation.maybe_simple_id(
    {
        cv.Required(CONF_ID): cv.use_id(MagicSwitch),
        cv.Optional(CONF_DURATION, default="500ms"): cv.positive_time_period_milliseconds,
    }
)


@automation.register_action(
    "magic_switch.mask",
    MagicSwitchMaskAction,
    MAGIC_SWITCH_MASK_ACTION_SCHEMA,
    synchronous=True,
)
async def magic_switch_mask_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(var.set_duration(config[CONF_DURATION]))
    return var
