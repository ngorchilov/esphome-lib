#include "bk72xx_ble_server.h"

#include <cstdio>
#include <cstring>
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

extern "C" {
#include "ble_api.h"
#include "app_ble.h"
}

#if CFG_BLE_VERSION != BLE_VERSION_5_1
#error "bk72xx_ble_server requires the BK7231N BLE 5.1 SDK"
#endif

namespace esphome::bk72xx_ble_server {

static const char *const TAG = "bk72xx_ble_server";
static BLECommandServer *server;
static constexpr uint16_t PROFILE_ID = 0;
static constexpr uint16_t VALUE_INDEX = 2;
static constexpr uint16_t COMMAND_LENGTH = 3;
static constexpr uint32_t RECOVERY_TIMEOUT = 30000;
// Let the garage's normal 60-second healthy-boot check finish before recovery
// reboots, so a persistent BLE fault does not repeatedly enter safe mode.
static constexpr uint32_t MIN_REBOOT_UPTIME = 120000;
// c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001 (BLE uses LSB-first UUID bytes).
static const uint8_t SERVICE_UUID[16] = {
    0x01, 0xA0, 0x21, 0x8D, 0x0E, 0x5F, 0x6C, 0xA3, 0xB0, 0x4E, 0x4F, 0x9B, 0xA0, 0xD4, 0xB7, 0xC6};
static bk_attm_desc_t attributes[] = {
    {{0x00, 0x28}, BK_PERM_SET(RD, ENABLE), 0, 0},
    {{0x03, 0x28}, BK_PERM_SET(RD, ENABLE), 0, 0},
    {{0x02, 0xA0, 0x21, 0x8D, 0x0E, 0x5F, 0x6C, 0xA3, 0xB0, 0x4E, 0x4F, 0x9B, 0xA0, 0xD4, 0xB7, 0xC6},
     BK_PERM_SET(WRITE_REQ, ENABLE) | BK_PERM_SET(WRITE_COMMAND, ENABLE),
     BK_PERM_SET(RI, ENABLE) | BK_PERM_SET(UUID_LEN, UUID_128), COMMAND_LENGTH},
};

static void command_callback(ble_cmd_t command, ble_cmd_param_t *param) {
  if (server != nullptr && param != nullptr)
    server->enqueue(EventType::COMMAND, command, param->status);
}

static void notice_callback(ble_notice_t notice, void *param) {
  if (server == nullptr || param == nullptr)
    return;
  switch (notice) {
    case BLE_5_CREATE_DB: {
      auto *info = static_cast<create_db_t *>(param);
      if (info->prf_id == PROFILE_ID)
        server->enqueue(EventType::DATABASE, info->status, 0);
      break;
    }
    case BLE_5_WRITE_EVENT: {
      auto *info = static_cast<write_req_t *>(param);
      if (info->prf_id != PROFILE_ID || info->att_idx != VALUE_INDEX)
        break;
      server->receive_write(info->value, info->len, info->conn_idx);
      break;
    }
    case BLE_5_ATT_INFO_REQ: {
      // The SDK reads these fields immediately after returning from this callback.
      auto *info = static_cast<att_info_req_t *>(param);
      const bool writable = info->prf_id == PROFILE_ID && info->att_idx == VALUE_INDEX;
      info->length = writable ? COMMAND_LENGTH : 0;
      info->status = writable ? 0 : 0x03;
      break;
    }
    case BLE_5_READ_EVENT:
      static_cast<read_req_t *>(param)->length = 0;
      break;
    case BLE_5_CONNECT_EVENT:
      server->enqueue(EventType::CONNECT, static_cast<conn_ind_t *>(param)->conn_idx, 0);
      break;
    case BLE_5_DISCONNECT_EVENT: {
      auto *info = static_cast<discon_ind_t *>(param);
      server->enqueue(EventType::DISCONNECT, info->conn_idx, info->reason);
      break;
    }
    default:
      break;
  }
}

void BLECommandServer::receive_write(const uint8_t *data, uint16_t length, uint8_t connection) {
  if (length == COMMAND_LENGTH && data != nullptr && connection < BLE_CONNECTION_MAX) {
    const uint32_t value = (uint32_t(data[0]) << 16) | (uint32_t(data[1]) << 8) | data[2];
    if (value == this->command_) {
      this->enqueue(EventType::WRITE, value, connection);
      return;
    }
  }
  // Do not enqueue, allocate, log, or retain the SDK buffer for rejected input.
  this->rejected_writes_.store(this->rejected_writes_.load(std::memory_order_relaxed) + 1,
                               std::memory_order_relaxed);
}

void BLECommandServer::enqueue(EventType type, uint32_t a, uint16_t b) {
  BLEEvent *event = this->event_pool_.allocate();
  if (event == nullptr) {
    this->events_.increment_dropped_count();
    return;
  }
  *event = {type, a, b};
  this->events_.push(event);
}

void BLECommandServer::setup() {
  server = this;
  ota::get_global_ota_callback()->add_global_state_listener(this);
  // Give Wi-Fi bring-up a head start, but do not depend on having a network.
  this->set_timeout("ble_start", 3000, [this]() { this->begin_(); });
}

void BLECommandServer::begin_() {
  // BDK 3.0.78 enables BLE sleep independently of Wi-Fi power_save_mode.
  // This mains-powered command server keeps BLE awake using the public SDK API.
  ble_ps_enable_clear();
  this->parent_->enable();
  // ESPHome 2026.8 has no generic Beken notice listener. The server owns this
  // callback after stack initialization; config rejects tracker/proxy consumers.
  ble_set_notice_cb(notice_callback);
  if (this->name_add_mac_suffix_) {
    uint8_t mac[6];
    this->parent_->get_mac_lsb_first(mac);
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "-%02x%02x%02x", mac[2], mac[1], mac[0]);
    this->name_ += suffix;
  }
  ble_appm_set_dev_name(this->name_.size(), reinterpret_cast<uint8_t *>(this->name_.data()));
  this->started_ = true;
  this->started_at_ = millis();
  this->set_phase_(Phase::DATABASE);
  ESP_LOGI(TAG, "Starting '%s'", this->name_.c_str());
}

void BLECommandServer::set_phase_(Phase phase) {
  this->phase_ = phase;
  this->pending_ = false;
  this->phase_since_ = millis();
  this->next_attempt_ = millis() + 100;
}

void BLECommandServer::handle_(const BLEEvent &event) {
  switch (event.type) {
    case EventType::DATABASE:
      if (this->phase_ != Phase::DATABASE || !this->pending_)
        break;
      if (event.a != 0) {
        ESP_LOGE(TAG, "GATT database creation failed: %u", event.a);
        this->request_reboot_("GATT database creation failed");
      } else {
        ESP_LOGI(TAG, "GATT command service created");
        this->set_phase_(Phase::CREATE_ADV);
      }
      break;
    case EventType::COMMAND: {
      ESP_LOGD(TAG, "BLE operation %u completed: %u", event.a, event.b);
      if (!this->pending_)
        break;
      const uint8_t expected[] = {0, BLE_CREATE_ADV, BLE_SET_ADV_DATA, BLE_SET_RSP_DATA, BLE_START_ADV};
      auto phase = static_cast<uint8_t>(this->phase_);
      if (phase >= sizeof(expected) || event.a != expected[phase])
        break;
      this->pending_ = false;
      if (event.b != 0) {
        ESP_LOGW(TAG, "BLE operation failed: %u", event.b);
        this->next_attempt_ = millis() + 1000;
        break;
      }
      this->set_phase_(static_cast<Phase>(phase + 1));
      if (this->phase_ == Phase::RUNNING)
        ESP_LOGI(TAG, "Advertising '%s'; ready for BLE writes", this->name_.c_str());
      break;
    }
    case EventType::CONNECT:
      ESP_LOGI(TAG, "BLE client connected (connection %u)", event.a);
      break;
    case EventType::DISCONNECT:
      ESP_LOGI(TAG, "BLE client disconnected (reason 0x%02X)", event.b);
      break;
    case EventType::WRITE:
      if (!this->suspended_ && !this->reboot_requested_) {
        ESP_LOGI(TAG, "Accepted BLE command #%lu (connection %u)",
                 static_cast<unsigned long>(++this->write_count_), event.b);
        this->write_trigger_.trigger(event.a);
      }
      break;
  }
}

void BLECommandServer::disconnect_client_() {
  if (!this->disconnect_pending_) {
    this->disconnect_pending_ = true;
    this->disconnect_since_ = millis();
    ESP_LOGI(TAG, "BLE connection timeout; releasing connection %u", this->connection_);
  }
  this->disconnect_attempt_ = millis();
  // Success means queued, not disconnected. Verify the SDK connection table in
  // loop(), retry at most once per second, and bound the entire release attempt.
  auto result = bk_ble_disconnect(this->connection_);
  if (result != ERR_SUCCESS)
    ESP_LOGW(TAG, "BLE disconnect returned %d; retrying", result);
}

void BLECommandServer::reconcile_connection_() {
  uint8_t connection = 0xFF;
  // The SDK updates this table before dispatching notices. Read it even when a
  // notice was lost; never index app_ble_get_connhdl() with the 0xFF sentinel.
  for (uint8_t i = 0; i < BLE_CONNECTION_MAX; i++) {
    auto handle = app_ble_get_connhdl(i);
    if (handle != UNKNOW_CONN_HDL && handle != USED_CONN_HDL) {
      connection = i;
      break;
    }
  }
  if (connection != this->connection_) {
    this->connection_ = connection;
    this->connection_since_ = millis();
    this->disconnect_pending_ = false;
  }
  if (connection == 0xFF || this->connection_timeout_ == 0)
    return;
  if (this->disconnect_pending_ && millis() - this->disconnect_since_ >= RECOVERY_TIMEOUT) {
    this->request_reboot_("BLE disconnect did not complete");
    return;
  }
  if (millis() - this->connection_since_ >= this->connection_timeout_ &&
      (!this->disconnect_pending_ || millis() - this->disconnect_attempt_ >= 1000))
    this->disconnect_client_();
}

void BLECommandServer::request_reboot_(const char *reason) {
  if (!this->reboot_requested_)
    ESP_LOGE(TAG, "%s; scheduling recovery reboot when permitted", reason);
  this->reboot_requested_ = true;
}

void BLECommandServer::loop() {
  if (millis() - this->rejected_report_at_ >= 5000) {
    const uint32_t rejected = this->rejected_writes_.load(std::memory_order_relaxed);
    const uint32_t count = rejected - this->rejected_reported_;
    this->rejected_reported_ = rejected;
    this->rejected_report_at_ = millis();
    if (count != 0)
      ESP_LOGW(TAG, "Ignored %lu BLE writes with invalid length, value or connection", static_cast<unsigned long>(count));
  }
  auto dropped = this->events_.get_and_reset_dropped_count();
  // Bound work per loop even if a client floods the callback. When events were
  // lost, discard this batch's writes instead of replaying incomplete traffic.
  for (unsigned i = 0; i < 15; i++) {
    auto *event = this->events_.pop();
    if (event == nullptr)
      break;
    dropped += this->events_.get_and_reset_dropped_count();
    if (dropped == 0 || event->type != EventType::WRITE)
      this->handle_(*event);
    this->event_pool_.release(event);
  }
  if (dropped != 0)
    ESP_LOGW(TAG, "BLE event queue overflow (%u); reconciling SDK state", dropped);
  if (!this->started_ || this->suspended_)
    return;
  if (this->reboot_requested_) {
    if (millis() - this->started_at_ >= MIN_REBOOT_UPTIME &&
        (!this->reboot_condition_ || this->reboot_condition_()))
      App.safe_reboot();
    return;
  }
  this->reconcile_connection_();
  if (this->reboot_requested_)
    return;
  auto now = millis();
  bool ready = app_ble_env_state_get() == APP_BLE_READY;
  auto activity_state = this->activity_ == 0xFF ? ACTV_IDLE : app_ble_actv_state_get(this->activity_);
  if (this->phase_ == Phase::RUNNING) {
    if (this->connection_ != 0xFF) {
      this->phase_since_ = now;
      return;
    }
    if (ready && activity_state == ACTV_ADV_CREATED) {
      ESP_LOGI(TAG, "Resuming BLE advertising");
      this->set_phase_(Phase::START_ADV);
    } else if (ready && activity_state == ACTV_IDLE) {
      ESP_LOGW(TAG, "BLE advertising activity lost; recreating");
      this->activity_ = 0xFF;
      this->set_phase_(Phase::CREATE_ADV);
    } else if (ready && activity_state == ACTV_ADV_STARTED) {
      this->phase_since_ = now;
    } else if (now - this->phase_since_ >= RECOVERY_TIMEOUT) {
      this->request_reboot_("BLE advertising state remained unavailable");
    }
    return;
  }
  if (now - this->phase_since_ >= RECOVERY_TIMEOUT) {
    ESP_LOGE(TAG, "BLE operation timed out in phase %u", static_cast<unsigned>(this->phase_));
    this->request_reboot_("BLE operation did not complete");
    return;
  }
  if (!ready)
    return;
  // Reconcile completion without its callback only once the SDK is idle. Never
  // force app_ble_reset(): it clears bookkeeping, not an in-flight operation.
  if (this->phase_ == Phase::CREATE_ADV && activity_state == ACTV_ADV_CREATED) {
    this->set_phase_(Phase::ADV_DATA);
  } else if (this->phase_ == Phase::START_ADV &&
             (activity_state == ACTV_ADV_STARTED || this->connection_ != 0xFF)) {
    this->set_phase_(Phase::RUNNING);
  } else if (this->phase_ != Phase::DATABASE && this->phase_ != Phase::CREATE_ADV &&
             activity_state == ACTV_IDLE) {
    this->activity_ = 0xFF;
    this->set_phase_(Phase::CREATE_ADV);
  } else if (this->pending_ && this->phase_ != Phase::DATABASE && now - this->command_since_ >= 1000) {
    // Data setters are idempotent. A completed operation with a missing callback
    // can be retried; a missing DB completion cannot safely recreate the DB.
    this->pending_ = false;
  }
  if (!this->pending_ && this->connection_ == 0xFF && static_cast<int32_t>(now - this->next_attempt_) >= 0)
    this->advance_();
}

void BLECommandServer::advance_() {
  ble_err_t result = ERR_SUCCESS;
  switch (this->phase_) {
    case Phase::DATABASE: {
      bk_ble_db_cfg db{};
      db.prf_task_id = PROFILE_ID;
      memcpy(db.uuid, SERVICE_UUID, sizeof(SERVICE_UUID));
      db.att_db_nb = 3;
      db.att_db = attributes;
      db.svc_perm = BK_PERM_SET(SVC_UUID_LEN, UUID_128);
      result = bk_ble_create_db(&db);
      break;
    }
    case Phase::CREATE_ADV:
      if (this->activity_ == 0xFF)
        this->activity_ = app_ble_get_idle_actv_idx_handle(ADV_ACTV);
      if (this->activity_ == 0xFF)
        result = ERR_UNKNOW_IDX;
      else
        result = bk_ble_create_advertising(this->activity_, 7, 160, 240, command_callback);
      break;
    case Phase::ADV_DATA: {
      uint8_t data[31] = {2, 0x01, 0x06, 17, 0x07};
      memcpy(data + 5, SERVICE_UUID, sizeof(SERVICE_UUID));
      uint8_t length = 5 + sizeof(SERVICE_UUID);
      // Short names fit beside the UUID, avoiding a scan-response exchange just
      // to identify the garage. Keep longer names complete in the scan response.
      if (length + 2 + this->name_.size() <= sizeof(data)) {
        data[length++] = this->name_.size() + 1;
        data[length++] = 0x09;
        memcpy(data + length, this->name_.data(), this->name_.size());
        length += this->name_.size();
      }
      result = bk_ble_set_adv_data(this->activity_, data, length, command_callback);
      break;
    }
    case Phase::SCAN_RESPONSE: {
      uint8_t data[31] = {};
      data[0] = this->name_.size() + 1;
      data[1] = 0x09;
      memcpy(data + 2, this->name_.data(), this->name_.size());
      result = bk_ble_set_scan_rsp_data(this->activity_, data, this->name_.size() + 2, command_callback);
      break;
    }
    case Phase::START_ADV:
      result = bk_ble_start_advertising(this->activity_, 0, command_callback);
      break;
    case Phase::RUNNING:
      return;
  }
  this->pending_ = result == ERR_SUCCESS;
  this->command_since_ = millis();
  if (result != ERR_SUCCESS) {
    ESP_LOGW(TAG, "BLE phase %u returned %d; retrying", static_cast<unsigned>(this->phase_), result);
    this->next_attempt_ = millis() + 1000;
  }
}

void BLECommandServer::on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent *) {
  if (state == ota::OTA_STARTED) {
    this->suspended_ = true;
    if (this->started_ && this->activity_ != 0xFF &&
        app_ble_actv_state_get(this->activity_) == ACTV_ADV_STARTED && app_ble_env_state_get() == APP_BLE_READY) {
      bk_ble_stop_advertising(this->activity_, nullptr);
      uint32_t start = millis();
      while (app_ble_actv_state_get(this->activity_) == ACTV_ADV_STARTED && millis() - start < 100)
        delay(10);
    }
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    this->suspended_ = false;
    // Time spent uploading is not evidence of a BLE operation timing out.
    this->phase_since_ = millis();
    this->connection_since_ = millis();
    this->disconnect_pending_ = false;
  }
}

void BLECommandServer::dump_config() {
  ESP_LOGCONFIG(TAG, "BK7231N BLE command server:");
  ESP_LOGCONFIG(TAG, "  Name: %s", this->name_.c_str());
  ESP_LOGCONFIG(TAG, "  Add MAC suffix: %s", YESNO(this->name_add_mac_suffix_));
  ESP_LOGCONFIG(TAG, "  Name in primary advertisement: %s", YESNO(this->name_.size() <= 8));
  ESP_LOGCONFIG(TAG, "  Connection timeout: %lu ms (0 disables)", static_cast<unsigned long>(this->connection_timeout_));
  ESP_LOGCONFIG(TAG, "  BLE sleep: disabled; stalled-operation recovery: 30s");
  ESP_LOGCONFIG(TAG, "  Service: c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001");
  ESP_LOGCONFIG(TAG, "  Write:   c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a002 (three-byte command, no pairing)");
  ESP_LOGCONFIG(TAG, "  Ready: %s; writes received: %lu", YESNO(this->phase_ == Phase::RUNNING && !this->reboot_requested_),
                static_cast<unsigned long>(this->write_count_));
}

}  // namespace esphome::bk72xx_ble_server
