#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>

#define id(x) x
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

namespace esphome::cover {
enum { COVER_OPERATION_IDLE, COVER_OPERATION_OPENING, COVER_OPERATION_CLOSING };
constexpr float COVER_CLOSED = 0.0f, COVER_OPEN = 1.0f;
}
using namespace esphome::cover;

uint32_t clock_ms = 1000;
uint32_t millis() { return clock_ms; }
uint32_t op_start = 0;
bool garage_open_estimated = false;
bool garage_unknown_motion = false;
void open_command();
void close_command();

struct Call {
  bool opening = false;
  Call &set_command_open() { opening = true; return *this; }
  Call &set_command_close() { opening = false; return *this; }
  void perform() { opening ? open_command() : close_command(); }
};
struct Door {
  int current_operation = COVER_OPERATION_IDLE;
  float position = 0.5f;
  int last_operation = COVER_OPERATION_IDLE;
  float last_position = 0.5f;
  int publishes = 0;
  void publish_state(bool save = true) {
    ++publishes;
    last_operation = current_operation;
    last_position = position;
  }
  Call make_call() { return {}; }
} garage_door;
struct Pulse {
  int count = 0;
  uint32_t started = 0;
  void execute() { ++count; started = clock_ms; }
  bool is_running() { return count != 0 && uint32_t(clock_ms - started) < 200; }
} pulse_relay;
struct Number { float state = 30; } door_travel_time_sensor;
struct Contact {
  bool state = false;
  bool known = true;
  bool has_state() { return known; }
} magnetic_contact;

void open_command() { OPEN_BODY }
void close_command() { CLOSE_BODY }
void toggle_command() { TOGGLE_BODY }
std::optional<float> position_lambda() { POSITION_BODY }
bool ble_reboot_allowed() { return BLE_REBOOT_BODY; }

void loop() {
  auto position = position_lambda();
  // TemplateCover only publishes a returned position when it changes.
  if (position && *position != garage_door.position) {
    garage_door.position = *position;
    garage_door.publish_state();
  }
}
void reset(bool closed, bool known = true) {
  clock_ms = 1000;
  op_start = 0;
  garage_open_estimated = garage_unknown_motion = false;
  garage_door = {};
  pulse_relay = {};
  magnetic_contact = {closed, known};
  loop();
}
void finish_opening() {
  open_command();
  magnetic_contact.state = false;
  clock_ms += 30000;
  loop();
  assert(garage_open_estimated);
  assert(garage_door.last_operation == COVER_OPERATION_IDLE);
  assert(garage_door.last_position == COVER_OPEN);
}

int main() {
  reset(false, false);
  open_command(); close_command(); toggle_command();
  assert(pulse_relay.count == 0);
  puts("PASS: no command pulses before the contact has a state");

  reset(true);
  close_command(); close_command();
  assert(pulse_relay.count == 0);
  open_command(); open_command(); close_command(); toggle_command();
  assert(pulse_relay.count == 1);
  assert(!ble_reboot_allowed());
  puts("PASS: CLOSE at closed and commands during opening do not pulse again");

  reset(true);
  finish_opening();
  open_command(); open_command();
  assert(pulse_relay.count == 1);
  assert(ble_reboot_allowed());
  puts("PASS: opening completion publishes idle/open; redundant OPEN is ignored");

  close_command(); close_command(); open_command(); toggle_command();
  assert(pulse_relay.count == 2);
  assert(!garage_open_estimated);
  assert(garage_door.last_operation == COVER_OPERATION_CLOSING);
  magnetic_contact.state = true;
  clock_ms += 500;
  loop();
  assert(garage_door.last_operation == COVER_OPERATION_IDLE);
  assert(garage_door.last_position == COVER_CLOSED);
  close_command();
  assert(pulse_relay.count == 2);
  puts("PASS: only the contact confirms closing and redundant CLOSE is ignored");

  reset(true);
  finish_opening();
  close_command();
  const int before_timeout = garage_door.publishes;
  clock_ms += 30000;
  loop();
  assert(garage_door.publishes == before_timeout + 1);
  assert(garage_door.last_operation == COVER_OPERATION_IDLE);
  assert(garage_door.last_position == 0.5f);
  assert(!garage_open_estimated);
  open_command(); close_command();
  assert(pulse_relay.count == 2);
  puts("PASS: unconfirmed closing publishes idle/unknown even with unchanged position");

  reset(true);
  open_command();
  clock_ms += 30000;
  loop();
  assert(!garage_open_estimated);
  assert(garage_door.last_position == COVER_CLOSED);
  assert(garage_door.last_operation == COVER_OPERATION_IDLE);
  puts("PASS: a stuck closed contact never becomes an open estimate");

  reset(false);
  assert(garage_door.position == 0.5f);
  open_command(); close_command();
  assert(pulse_relay.count == 0);
  toggle_command();
  assert(pulse_relay.count == 1 && garage_unknown_motion);
  clock_ms += 500;
  assert(!ble_reboot_allowed());
  open_command(); close_command(); toggle_command();
  assert(pulse_relay.count == 1);
  clock_ms += 30000;
  loop();
  assert(!garage_unknown_motion && !garage_open_estimated);
  assert(garage_door.last_position == 0.5f);
  assert(ble_reboot_allowed());
  puts("PASS: unknown-position toggle is explicit, serialized and blocks BLE reboot");

  toggle_command();
  magnetic_contact.state = true;
  clock_ms += 500;
  loop();
  assert(!garage_unknown_motion && garage_door.last_position == COVER_CLOSED);
  toggle_command();
  assert(garage_door.current_operation == COVER_OPERATION_OPENING);
  puts("PASS: contact resolves unknown toggle; endpoint toggles keep their command path");

  reset(true);
  clock_ms = UINT32_MAX - 1000;
  open_command();
  magnetic_contact.state = false;
  ++clock_ms;
  loop();
  assert(garage_door.current_operation == COVER_OPERATION_OPENING);
  clock_ms += 29998;
  loop();
  assert(garage_door.current_operation == COVER_OPERATION_OPENING);
  ++clock_ms;
  loop();
  assert(garage_door.current_operation == COVER_OPERATION_IDLE && garage_open_estimated);
  puts("PASS: timeout arithmetic is correct across millis wraparound");

  magnetic_contact.state = true;
  loop();
  assert(!garage_open_estimated && garage_door.position == COVER_CLOSED);
  magnetic_contact.state = false;
  loop();
  assert(garage_door.position == 0.5f);
  close_command();
  assert(pulse_relay.count == 1);
  puts("PASS: a later manual contact cycle invalidates the old open estimate");
}
