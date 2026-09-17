"""Assertions about resolved configuration, beyond schema validity."""

import json
from pathlib import Path
import sys

from esphome import pins
from esphome.config import path_context, read_config
from esphome.core import CORE


def entity(config, domain, name):
    return next(item for item in config.get(domain, []) if str(item.get("id")) == name)


def ids(config, domain):
    return {str(item["id"]) for item in config.get(domain, []) if "id" in item}


def enabled(value):
    return str(value).lower() == "true"


def check(config, contract, substitutions):
    if contract == "esp8266-relay-pin":
        pin = entity(config, "output", "channel1_power_output")["pin"]
        assert pin["number"] == 4 and pin["mode"]["output"]
        assert not pin["inverted"] and not pin["allow_other_uses"]
        assert not {"drive_strength", "ignore_strapping_warning", "ignore_pin_validation_error"} & pin.keys()
    elif contract == "pins":
        platform = config["substitutions"]["firmware_platform"]
        assert platform == CORE.target_platform
        esp32 = platform == "esp32"
        output = entity(config, "output", "pin_test_output")["pin"]
        input_pin = entity(config, "binary_sensor", "pin_test_input")["pin"]
        input_copy = entity(config, "binary_sensor", "pin_test_input_copy")["pin"]
        assert output["inverted"] and not output["allow_other_uses"]
        assert output["mode"]["output"] and output["mode"]["open_drain"]
        assert not output["mode"]["input"] and not output["mode"]["pullup"]
        assert input_pin["number"] == input_copy["number"]
        for pin in (input_pin, input_copy):
            assert pin["mode"]["input"] and pin["mode"]["pullup"]
            assert pin["allow_other_uses"] and not pin["inverted"]
            assert not pin["mode"]["output"] and not pin["mode"]["pulldown"]
        special = {"drive_strength", "ignore_strapping_warning", "ignore_pin_validation_error"}
        for pin in (output, input_pin, input_copy):
            assert (special & pin.keys()) == (special if esp32 else set())
        if esp32:
            assert output["drive_strength"] == 40
            assert input_pin["drive_strength"] == 20
            assert not output["ignore_pin_validation_error"]
            assert output["ignore_strapping_warning"] == enabled(substitutions.get("ignore_strapping", False))
        number = config["substitutions"]["number_pin"]
        assert set(number) == ({"number", "ignore_strapping_warning"} if esp32 else {"number"})
        if esp32:
            assert enabled(number["ignore_strapping_warning"]) == enabled(substitutions.get("ignore_strapping", False))
        uart = entity(config, "uart", "pin_test_uart")
        token = path_context.set(["substitutions", "number_pin"])
        try:
            assert pins.internal_gpio_output_pin_number(number) == uart["tx_pin"]["number"]
        finally:
            path_context.reset(token)
    elif contract == "disabled-basic":
        assert "sbr4_power_output" in ids(config, "output")
        assert "sbr4_power_state" not in ids(config, "binary_sensor")
        assert "sbr4_primary_toggle" not in ids(config, "script")
        assert "sbr4_magic_switch_dnd" not in ids(config, "switch")
        assert "magic_switch" not in config
    elif contract == "enabled-basic":
        assert "sbr4_primary_toggle" in ids(config, "script")
        assert "sbr4_magic_switch_dnd" in ids(config, "switch")
        magic = entity(config, "magic_switch", "sbr4_magic_switch")
        assert magic["pin"]["number"] == 5
        action = magic["on_switch"]["then"][0]["if"]
        assert str(action["condition"]["switch.is_off"]["id"]) == "sbr4_magic_switch_dnd"
        assert str(action["then"][0]["script.execute"]["id"]) == "sbr4_primary_toggle"
        hook = entity(config, "script", "sbr4_after_power_change")
        mask = next(item["magic_switch.mask"] for item in hook["then"] if "magic_switch.mask" in item)
        assert str(mask["id"]) == "sbr4_magic_switch"
        assert mask["duration"].total_milliseconds == 500
    elif contract == "two-relay":
        first = enabled(substitutions.get("first_enabled", False))
        second = enabled(substitutions.get("second_enabled", True))
        assert {"relay_1_power_output", "relay_2_power_output"} <= ids(config, "output")
        for index, active in ((1, first), (2, second)):
            prefix = f"relay_{index}"
            assert (f"{prefix}_power_state" in ids(config, "binary_sensor")) == active
            assert (f"{prefix}_power_on" in ids(config, "script")) == active
        assert ("master_switch" in ids(config, "binary_sensor")) == (first or second)
        if first or second:
            action = entity(config, "binary_sensor", "master_switch")["on_click"][0]["then"][0]["if"]
            prefixes = [f"relay_{i}" for i, active in ((1, first), (2, second)) if active]
            assert [str(item["binary_sensor.is_on"]["id"]) for item in action["condition"]["or"]] == [f"{p}_power_state" for p in prefixes]
            for branch, command in (("then", "off"), ("else", "on")):
                assert [str(item["script.execute"]["id"]) for item in action[branch]] == [f"{p}_power_{command}" for p in prefixes]
    elif contract == "radio-frequency":
        assert entity(config, "sx127x", "radio_transceiver")["frequency"] == 868350000
    elif contract == "earu":
        temperature = entity(config, "sensor", "temperature")
        assert temperature["update_interval"].total_milliseconds == 1000
        assert temperature["filters"][1]["throttle_average"].total_milliseconds == 10000
        meter = next(item for item in config["sensor"] if item["platform"] == "bl0942")
        assert meter["update_interval"].total_milliseconds == 1000
        assert meter["energy"]["unit_of_measurement"] == "kWh"
        assert config["uart"][0]["baud_rate"] == 4800
    elif contract.startswith("wifi-"):
        network = config["wifi"]["networks"][0]
        expected = {
            "wifi-explicit": ("test-network", "test-password"),
            "wifi-open": ("open-network", ""),
            "wifi-ssid": ("explicit-network", "fallback-password"),
            "wifi-password": ("fallback-network", "explicit-password"),
        }[contract]
        assert (network["ssid"], network["password"]) == expected
    elif contract == "inherited-logger":
        assert config["logger"]["level"] == "WARN"
        assert config["logger"]["baud_rate"] == 0
    elif contract.startswith("serial-logger-"):
        expected = {
            "serial-logger-esp07": (0, "UART0"),
            "serial-logger-esp12ef": (0, "UART0"),
            "serial-logger-beken": (115200, "DEFAULT"),
            "serial-logger-hri4853": (0, "UART0"),
        }[contract]
        assert (config["logger"]["baud_rate"], config["logger"]["hardware_uart"]) == expected
        if contract == "serial-logger-beken":
            uart = entity(config, "uart", "tuya_uart")
            assert uart["tx_pin"]["number"] == 11
            assert uart["rx_pin"]["number"] == 10
    else:
        raise AssertionError(f"Unknown contract: {contract}")


if __name__ == "__main__":
    CORE.config_path = Path(sys.argv[1]).resolve()
    substitutions = json.loads(sys.argv[3])
    config = read_config({key: str(value) for key, value in substitutions.items()})
    assert config is not None, "ESPHome configuration failed"
    check(config, sys.argv[2], substitutions)
    print(f"PASS resolved contract: {sys.argv[2]}")
