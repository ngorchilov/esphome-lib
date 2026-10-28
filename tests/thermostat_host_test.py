"""Execute the Model 716 YAML lambdas with MCU/climate doubles and sanitizers."""

import os
from pathlib import Path
import subprocess
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[1]


def main():
    config = yaml.load((ROOT / "devices/tuya-thermostat-716.yaml").read_text(), Loader=yaml.BaseLoader)
    climate = config["climate"][0]
    assert climate["optimistic"] == "false" and climate["restore_mode"] == "NO_RESTORE"
    callbacks = {int(item["sensor_datapoint"]): item["then"]
                 for item in config["tuya"]["on_datapoint_update"]}
    battery = next(item for item in config["sensor"] if item.get("sensor_datapoint") == "107")
    action = next(item for item in config["script"] if item["id"] == "thermostat_update_action")
    bodies = {
        "UPDATE_ACTION": action["then"][0]["lambda"],
        "POWER_FEEDBACK": callbacks[1][0]["lambda"],
        "PRESET_FEEDBACK": callbacks[2][0]["lambda"],
        "VALVE_FEEDBACK": callbacks[36][0]["lambda"],
        "FAULT_FEEDBACK": callbacks[45][0]["lambda"],
        "TARGET_FEEDBACK": callbacks[16][0]["climate.template.publish"]["target_temperature"],
        "FEATURE_FEEDBACK": callbacks[101][0]["text_sensor.template.publish"]["state"],
        "MODE_COMMAND": climate["set_mode_action"][0]["lambda"],
        "PRESET_COMMAND": climate["set_custom_preset_action"][0]["lambda"],
        "TARGET_COMMAND": climate["set_target_temperature_action"][0]["lambda"],
        "BATTERY_FILTER": battery["filters"][0]["lambda"],
    }
    source = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace climate {
enum ClimateMode { CLIMATE_MODE_OFF, CLIMATE_MODE_HEAT, CLIMATE_MODE_COOL };
enum ClimateAction { CLIMATE_ACTION_OFF, CLIMATE_ACTION_HEATING, CLIMATE_ACTION_IDLE };
}
using namespace climate;
#define id(value) value

struct Climate {
  ClimateMode mode = CLIMATE_MODE_OFF;
  ClimateAction action = CLIMATE_ACTION_OFF;
  std::string preset;
  void set_mode(ClimateMode value) { mode = value; }
  void set_custom_preset(const char *value) { preset = value; }
  void publish_state() {}
} thermostat;

struct BinarySensor {
  bool state = false, known = false;
  void publish_state(bool value) { state = value; known = true; }
  void invalidate_state() { known = false; }
  bool has_state() const { return known; }
} thermostat_valve_open, thermostat_internal_sensor_fault,
  thermostat_external_sensor_fault, thermostat_low_battery;

struct MCU {
  std::vector<std::tuple<char, int, int>> writes;
  void set_boolean_datapoint_value(int dp, bool value) { writes.emplace_back('b', dp, value); }
  void set_enum_datapoint_value(int dp, int value) { writes.emplace_back('e', dp, value); }
  void set_integer_datapoint_value(int dp, int value) { writes.emplace_back('i', dp, value); }
} thermostat_mcu;

void update_action() { UPDATE_ACTION }
void power_feedback(bool x) { [&]() { POWER_FEEDBACK }(); update_action(); }
void preset_feedback(uint8_t x) { PRESET_FEEDBACK }
void valve_feedback(uint8_t x) { [&]() { VALVE_FEEDBACK }(); update_action(); }
void fault_feedback(uint32_t x) { FAULT_FEEDBACK }
float target_feedback(int x) { TARGET_FEEDBACK }
std::string feature_feedback(uint32_t x) { FEATURE_FEEDBACK }
void mode_command(ClimateMode x) { MODE_COMMAND }
void preset_command(std::string_view x) { PRESET_COMMAND }
void target_command(float x) { TARGET_COMMAND }
float battery_filter(float x) { BATTERY_FILTER }

int main() {
  // MCU feedback must never send a command or synthesize an unreported valve state.
  power_feedback(true);
  assert(!thermostat_valve_open.has_state());
  assert(thermostat.mode == CLIMATE_MODE_HEAT);
  valve_feedback(0);
  assert(thermostat.action == CLIMATE_ACTION_IDLE);
  valve_feedback(1);
  assert(thermostat.action == CLIMATE_ACTION_HEATING);
  power_feedback(false);
  assert(thermostat.action == CLIMATE_ACTION_HEATING); // MCU frost protection can still heat.
  valve_feedback(0);
  assert(thermostat.action == CLIMATE_ACTION_OFF);
  thermostat_valve_open = {};
  valve_feedback(1); // Reverse startup/report ordering.
  power_feedback(true);
  assert(thermostat.action == CLIMATE_ACTION_HEATING);
  valve_feedback(9);
  assert(!thermostat_valve_open.has_state());
  preset_feedback(0);
  assert(thermostat.preset == "Schedule");
  preset_feedback(1);
  preset_feedback(9);
  assert(thermostat.preset == "Manual");
  for (uint32_t mask = 0; mask < 8; ++mask) {
    fault_feedback(mask);
    assert(thermostat_internal_sensor_fault.state == bool(mask & 1));
    assert(thermostat_external_sensor_fault.state == bool(mask & 2));
    assert(thermostat_low_battery.state == bool(mask & 4));
  }
  assert(thermostat_mcu.writes.empty());

  // Commands target only writable datapoints and do not fake confirmation.
  mode_command(CLIMATE_MODE_OFF);
  assert(thermostat.mode == CLIMATE_MODE_HEAT);
  mode_command(CLIMATE_MODE_HEAT);
  mode_command(CLIMATE_MODE_COOL);
  preset_command("Schedule");
  assert(thermostat.preset == "Manual");
  preset_command("Manual");
  preset_command("Unknown");
  target_command(5);
  target_command(21.5f);
  target_command(21.26f);
  target_command(300);
  target_command(4.9f);
  target_command(300.1f);
  target_command(NAN);
  target_command(INFINITY);
  const std::vector<std::tuple<char, int, int>> expected{
    {'b', 1, 0}, {'b', 1, 1}, {'e', 2, 0}, {'e', 2, 1},
    {'i', 16, 50}, {'i', 16, 215}, {'i', 16, 215}, {'i', 16, 3000}
  };
  assert(thermostat_mcu.writes == expected);
  assert(std::fabs(target_feedback(215) - 21.5f) < 0.001f);
  assert(std::fabs(target_feedback(3000) - 300.0f) < 0.001f);
  for (int value = 0; value <= 100; ++value) assert(battery_filter(value) == value);
  assert(std::isnan(battery_filter(101)) && std::isnan(battery_filter(-1)));
  assert(feature_feedback(2147483647) == "2147483647"); // No scale or float precision loss.
  std::cout << "PASS thermostat commands, feedback ordering, faults, scaling and battery sentinel\n";
}
'''
    for marker, body in bodies.items():
        assert source.count(marker) == 1
        source = source.replace(marker, body)
    with tempfile.TemporaryDirectory(prefix="esphome-thermostat-host-") as temporary:
        binary = Path(temporary) / "thermostat-test"
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-x", "c++", "-", "-o", str(binary)],
                       input=source, text=True, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
