"""Assertions about resolved configuration, beyond schema validity."""

import json
from pathlib import Path
import sys

from esphome.config import read_config
from esphome.core import CORE


def entity(config, domain, name):
    return next(item for item in config.get(domain, []) if str(item.get("id")) == name)


def ids(config, domain):
    return {str(item["id"]) for item in config.get(domain, []) if "id" in item}


def enabled(value):
    return str(value).lower() == "true"


def check(config, contract, substitutions):
    if contract == "disabled-basic":
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
