#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/event_pool.h"
#include "esphome/core/lock_free_queue.h"
#include "esphome/components/bk72xx_ble/bk72xx_ble.h"
#include "esphome/components/ota/ota_backend.h"

namespace esphome::bk72xx_ble_server {

enum class EventType : uint8_t { DATABASE, COMMAND, CONNECT, DISCONNECT, WRITE, REJECT };
struct BLEEvent {
  EventType type;
  uint16_t a;
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
  Trigger<uint8_t> *get_write_trigger() { return &this->write_trigger_; }
  // Called only from the BDK BLE task. Automations run later in loop().
  void enqueue(EventType type, uint16_t a, uint16_t b);

 protected:
  void begin_();
  void advance_();
  void handle_(const BLEEvent &event);
  void disconnect_client_();
  void set_phase_(Phase phase);
  bk72xx_ble::BK72xxBLE *parent_{};
  std::string name_;
  Trigger<uint8_t> write_trigger_;
  EventPool<BLEEvent, 15> event_pool_;
  LockFreeQueue<BLEEvent, 16> events_;
  uint32_t phase_since_{0};
  uint32_t next_attempt_{0};
  uint32_t write_count_{0};
  uint32_t connection_timeout_{0};
  uint8_t activity_{0xFF};
  uint8_t connection_{0xFF};
  Phase phase_{Phase::DATABASE};
  bool started_{false};
  bool name_add_mac_suffix_{false};
  bool pending_{false};
  bool suspended_{false};
};

}  // namespace esphome::bk72xx_ble_server
