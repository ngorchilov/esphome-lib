#include "bk72xx_ble_server.h"

#include <cstdio>
#include <cstring>
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
// c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001 (BLE uses LSB-first UUID bytes).
static const uint8_t SERVICE_UUID[16] = {
    0x01, 0xA0, 0x21, 0x8D, 0x0E, 0x5F, 0x6C, 0xA3, 0xB0, 0x4E, 0x4F, 0x9B, 0xA0, 0xD4, 0xB7, 0xC6};
static bk_attm_desc_t attributes[] = {
    {{0x00, 0x28}, BK_PERM_SET(RD, ENABLE), 0, 0},
    {{0x03, 0x28}, BK_PERM_SET(RD, ENABLE), 0, 0},
    {{0x02, 0xA0, 0x21, 0x8D, 0x0E, 0x5F, 0x6C, 0xA3, 0xB0, 0x4E, 0x4F, 0x9B, 0xA0, 0xD4, 0xB7, 0xC6},
     BK_PERM_SET(WRITE_REQ, ENABLE) | BK_PERM_SET(WRITE_COMMAND, ENABLE),
     BK_PERM_SET(RI, ENABLE) | BK_PERM_SET(UUID_LEN, UUID_128), 1},
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
      if (info->len == 1 && info->value != nullptr)
        server->enqueue(EventType::WRITE, info->value[0], info->conn_idx);
      else
        server->enqueue(EventType::REJECT, info->len, info->conn_idx);
      break;
    }
    case BLE_5_ATT_INFO_REQ: {
      // The SDK reads these fields immediately after returning from this callback.
      auto *info = static_cast<att_info_req_t *>(param);
      info->length = 1;
      info->status = (info->prf_id == PROFILE_ID && info->att_idx == VALUE_INDEX) ? 0 : 0x03;
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

void BLECommandServer::enqueue(EventType type, uint16_t a, uint16_t b) {
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
      if (event.a != 0) {
        ESP_LOGE(TAG, "GATT database creation failed: %u", event.a);
        this->mark_failed();
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
      this->connection_ = event.a;
      ESP_LOGI(TAG, "BLE client connected (connection %u)", event.a);
      if (this->connection_timeout_ != 0)
        this->set_timeout("connection_timeout", this->connection_timeout_, [this]() { this->disconnect_client_(); });
      break;
    case EventType::DISCONNECT:
      this->cancel_timeout("connection_timeout");
      this->connection_ = 0xFF;
      ESP_LOGI(TAG, "BLE client disconnected (reason 0x%02X)", event.b);
      if (this->phase_ == Phase::RUNNING)
        this->set_phase_(Phase::START_ADV);
      break;
    case EventType::WRITE:
      if (!this->suspended_) {
        ESP_LOGI(TAG, "BLE write #%lu: 0x%02X (connection %u)",
                 static_cast<unsigned long>(++this->write_count_), event.a, event.b);
        this->write_trigger_.trigger(static_cast<uint8_t>(event.a));
      }
      break;
    case EventType::REJECT:
      ESP_LOGW(TAG, "Ignored BLE write: expected one byte, received %u", event.a);
      break;
  }
}

void BLECommandServer::disconnect_client_() {
  if (this->connection_ == 0xFF)
    return;
  // BDK queues GAPC_DISCONNECT; normal disconnect handling resumes advertising.
  auto result = bk_ble_disconnect(this->connection_);
  if (result == ERR_SUCCESS) {
    ESP_LOGI(TAG, "BLE connection timeout; releasing connection %u", this->connection_);
  } else {
    ESP_LOGW(TAG, "BLE disconnect returned %d; retrying", result);
    this->set_timeout("connection_timeout", 1000, [this]() { this->disconnect_client_(); });
  }
}

void BLECommandServer::loop() {
  while (auto *event = this->events_.pop()) {
    this->handle_(*event);
    this->event_pool_.release(event);
  }
  if (auto dropped = this->events_.get_and_reset_dropped_count(); dropped != 0) {
    ESP_LOGE(TAG, "BLE event queue overflow (%u); stopping server", dropped);
    this->mark_failed();
  }
  if (!this->started_ || this->suspended_ || this->is_failed())
    return;
  if (this->phase_ == Phase::RUNNING) {
    if (this->connection_ == 0xFF && app_ble_actv_state_get(this->activity_) == ACTV_ADV_CREATED)
      this->set_phase_(Phase::START_ADV);
    return;
  }
  if (millis() - this->phase_since_ > 30000) {
    ESP_LOGE(TAG, "BLE startup timed out in phase %u; Wi-Fi remains available", static_cast<unsigned>(this->phase_));
    this->mark_failed();
    return;
  }
  if (!this->pending_ && static_cast<int32_t>(millis() - this->next_attempt_) >= 0 &&
      app_ble_env_state_get() == APP_BLE_READY)
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
      uint8_t data[21] = {2, 0x01, 0x06, 17, 0x07};
      memcpy(data + 5, SERVICE_UUID, sizeof(SERVICE_UUID));
      result = bk_ble_set_adv_data(this->activity_, data, sizeof(data), command_callback);
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
  }
}

void BLECommandServer::dump_config() {
  ESP_LOGCONFIG(TAG, "BK7231N BLE command server:");
  ESP_LOGCONFIG(TAG, "  Name: %s", this->name_.c_str());
  ESP_LOGCONFIG(TAG, "  Add MAC suffix: %s", YESNO(this->name_add_mac_suffix_));
  ESP_LOGCONFIG(TAG, "  Connection timeout: %lu ms (0 disables)", static_cast<unsigned long>(this->connection_timeout_));
  ESP_LOGCONFIG(TAG, "  Service: c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a001");
  ESP_LOGCONFIG(TAG, "  Write:   c6b7d4a0-9b4f-4eb0-a36c-5f0e8d21a002 (one byte, no pairing)");
  ESP_LOGCONFIG(TAG, "  Ready: %s; writes received: %lu", YESNO(this->phase_ == Phase::RUNNING && !this->is_failed()),
                static_cast<unsigned long>(this->write_count_));
}

}  // namespace esphome::bk72xx_ble_server
