#pragma once

#include <atomic>
#include <functional>
#include <utility>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/event_pool.h"
#include "esphome/core/lock_free_queue.h"
#include "esphome/components/bk72xx_ble/bk72xx_ble.h"
#include "esphome/components/ota/ota_backend.h"

namespace esphome::bk72xx_ble_server {

enum class EventType : uint8_t { DATABASE, COMMAND, CONNECT, DISCONNECT, WRITE };
struct BLEEvent {
  EventType type;
  uint32_t a;
  uint16_t b;
  void release() {}
};

enum class Phase : uint8_t { DATABASE, CREATE_ADV, ADV_DATA, SCAN_RESPONSE, START_ADV, RUNNING };

class BLECommandServer : public Component, public ota::OTAGlobalStateListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI - 1; }
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) override;
  void set_parent(bk72xx_ble::BK72xxBLE *parent) { this->parent_ = parent; }
  void set_name(const std::string &name) { this->name_ = name; }
  void set_name_add_mac_suffix(bool enabled) { this->name_add_mac_suffix_ = enabled; }
  void set_connection_timeout(uint32_t timeout) { this->connection_timeout_ = timeout; }
  void set_command(uint32_t command) { this->command_ = command; }
  void set_reboot_condition(std::function<bool()> condition) { this->reboot_condition_ = std::move(condition); }
  Trigger<uint32_t> *get_write_trigger() { return &this->write_trigger_; }
  // Called only from the BDK BLE task. Automations run later in loop().
  void enqueue(EventType type, uint32_t a, uint16_t b);
  void receive_write(const uint8_t *data, uint16_t length, uint8_t connection);

 protected:
  void begin_();
  void advance_();
  void handle_(const BLEEvent &event);
  void disconnect_client_();
  void reconcile_connection_();
  void request_reboot_(const char *reason);
  void set_phase_(Phase phase);
  bk72xx_ble::BK72xxBLE *parent_{};
  std::string name_;
  Trigger<uint32_t> write_trigger_;
  std::function<bool()> reboot_condition_;
  EventPool<BLEEvent, 15> event_pool_;
  LockFreeQueue<BLEEvent, 16> events_;
  uint32_t phase_since_{0};
  uint32_t next_attempt_{0};
  uint32_t write_count_{0};
  uint32_t command_{0};
  // Only the BLE task writes this counter. Use load/store, not atomic RMW
  // operations, which BK7231N's ARMv5 core does not support.
  std::atomic<uint32_t> rejected_writes_{0};
  uint32_t rejected_reported_{0};
  uint32_t rejected_report_at_{0};
  uint32_t connection_timeout_{0};
  uint32_t started_at_{0};
  uint32_t command_since_{0};
  uint32_t connection_since_{0};
  uint32_t disconnect_since_{0};
  uint32_t disconnect_attempt_{0};
  uint8_t activity_{0xFF};
  uint8_t connection_{0xFF};
  Phase phase_{Phase::DATABASE};
  bool started_{false};
  bool name_add_mac_suffix_{false};
  bool pending_{false};
  bool suspended_{false};
  bool disconnect_pending_{false};
  bool reboot_requested_{false};
};

}  // namespace esphome::bk72xx_ble_server
