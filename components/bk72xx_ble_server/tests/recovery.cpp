// Tests execute the production server. Only the clock, scheduler, hardware SDK,
// OTA listener and automation boundary are doubled; ESPHome queues are real.
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>
#include "bk72xx_ble_server.h"
#include "esphome/core/application.h"
extern "C" {
#include "ble_api.h"
#include "app_ble.h"
}

uint32_t fake_now = 0;
int activity, ready, starts, creates, disconnects, db_status;
uint8_t connection_handle;
bool sleep_enabled, complete_disconnect, disconnect_notice, lose_db_notice;
bool stuck_command, omit_callback;
notice_t notice;
std::vector<std::function<void()>> completions;
std::vector<uint8_t> advertisement, scan_response;
unsigned rejection_reports = 0;
void test_warning(const char *format) {
  if (std::strstr(format, "Ignored %lu BLE writes") != nullptr)
    rejection_reports++;
}

void operation(ble_cmd_t cmd, ble_cmd_cb_t callback, std::function<void()> effect = [] {}) {
  assert(ready == APP_BLE_READY);  // No overlapping SDK operations allowed.
  ready = 0;
  if (stuck_command)
    return;
  completions.push_back([=] {
    effect();
    ready = APP_BLE_READY;
    if (!omit_callback && callback != nullptr) {
      ble_cmd_param_t result{0};
      callback(cmd, &result);
    }
  });
}

extern "C" {
void ble_ps_enable_clear() { sleep_enabled = false; }
void ble_set_notice_cb(notice_t cb) { notice = cb; }
void ble_appm_set_dev_name(uint8_t, uint8_t *) {}
int app_ble_env_state_get() { return ready; }
int app_ble_actv_state_get(uint8_t index) { assert(index == 0); return activity; }
uint8_t app_ble_get_idle_actv_idx_handle(int) { return 0; }
uint8_t app_ble_get_connhdl(int index) { assert(index == 0); return connection_handle; }
ble_err_t bk_ble_create_db(bk_ble_db_cfg *db) {
  assert(db->att_db[2].len == 3);
  if (!lose_db_notice)
    completions.push_back([] {
      create_db_t event{0, static_cast<uint8_t>(db_status)};
      notice(BLE_5_CREATE_DB, &event);
    });
  return ERR_SUCCESS;
}
ble_err_t bk_ble_create_advertising(uint8_t, uint8_t, uint16_t, uint16_t, ble_cmd_cb_t cb) {
  assert(activity == ACTV_IDLE);
  creates++;
  operation(BLE_CREATE_ADV, cb, [] { activity = ACTV_ADV_CREATED; });
  return ERR_SUCCESS;
}
ble_err_t bk_ble_set_adv_data(uint8_t, uint8_t *data, uint8_t length, ble_cmd_cb_t cb) {
  assert(length <= 31);
  advertisement.assign(data, data + length);
  operation(BLE_SET_ADV_DATA, cb);
  return ERR_SUCCESS;
}
ble_err_t bk_ble_set_scan_rsp_data(uint8_t, uint8_t *data, uint8_t length, ble_cmd_cb_t cb) {
  assert(length <= 31);
  scan_response.assign(data, data + length);
  operation(BLE_SET_RSP_DATA, cb);
  return ERR_SUCCESS;
}
ble_err_t bk_ble_start_advertising(uint8_t, uint16_t duration, ble_cmd_cb_t cb) {
  assert(duration == 0);
  assert(connection_handle == UNKNOW_CONN_HDL);
  assert(activity == ACTV_ADV_CREATED);
  starts++;
  operation(BLE_START_ADV, cb, [] { activity = ACTV_ADV_STARTED; });
  return ERR_SUCCESS;
}
ble_err_t bk_ble_stop_advertising(uint8_t, ble_cmd_cb_t) {
  activity = ACTV_ADV_CREATED;
  return ERR_SUCCESS;
}
ble_err_t bk_ble_disconnect(uint8_t index) {
  assert(index == 0 && connection_handle != UNKNOW_CONN_HDL);
  disconnects++;
  if (complete_disconnect)
    completions.push_back([] {
      connection_handle = UNKNOW_CONN_HDL;
      if (disconnect_notice) {
        discon_ind_t event{0, 0x16};
        notice(BLE_5_DISCONNECT_EVENT, &event);
      }
    });
  return ERR_SUCCESS;
}
}

using namespace esphome::bk72xx_ble_server;
struct TestServer : BLECommandServer {
  bool rebooted = false;
  Phase phase() { return this->phase_; }
  uint8_t connection() { return this->connection_; }
  size_t queued() { return this->events_.size(); }
  uint32_t rejected() { return this->rejected_writes_.load(); }
};

void tick(TestServer &server, uint32_t ms = 100) {
  fake_now += ms;
  if (server.rebooted)
    return;
  server.run_timers();
  auto pending = std::move(completions);
  completions.clear();
  for (auto &f : pending)
    f();
  try {
    server.loop();
  } catch (esphome::RebootRequested &) {
    server.rebooted = true;
  }
  assert(!server.is_failed());  // Recovery never permanently disables the loop.
}
void run(TestServer &server, unsigned ticks) {
  for (unsigned i = 0; i < ticks; i++)
    tick(server);
}
void reset(uint32_t now = 0) {
  fake_now = now;
  activity = ACTV_IDLE;
  ready = APP_BLE_READY;
  connection_handle = UNKNOW_CONN_HDL;
  starts = creates = disconnects = db_status = 0;
  sleep_enabled = complete_disconnect = disconnect_notice = true;
  lose_db_notice = stuck_command = omit_callback = false;
  completions.clear();
  advertisement.clear();
  scan_response.clear();
  rejection_reports = 0;
}
void setup(TestServer &server, const std::string &name = "Garage 7") {
  static esphome::bk72xx_ble::BK72xxBLE parent;
  server.set_parent(&parent);
  server.set_name(name);
  server.set_command(0xA1B2C3);  // Test fixture only; unrelated to deployed commands.
  server.set_connection_timeout(5000);
  server.setup();
}
void boot(TestServer &server) {
  setup(server);
  run(server, 60);
  assert(!server.rebooted && !sleep_enabled);
  assert(server.phase() == Phase::RUNNING && starts == 1);
}
void connect(TestServer &server, bool send_notice = true) {
  activity = ACTV_ADV_CREATED;
  connection_handle = 0;
  if (send_notice) {
    conn_ind_t event{0};
    notice(BLE_5_CONNECT_EVENT, &event);
  }
  tick(server);
  assert(server.connection() == 0);
}
void raw_write(const uint8_t *data, uint16_t length, uint16_t profile = 0, uint16_t attribute = 2,
               uint8_t connection = 0) {
  write_req_t event{profile, attribute, length, const_cast<uint8_t *>(data), connection};
  notice(BLE_5_WRITE_EVENT, &event);
}
void write(uint32_t value = 0xA1B2C3, uint16_t length = 3) {
  const uint8_t data[] = {uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
  raw_write(data, length);
}
void check(bool result, const char *message) {
  if (!result) {
    std::cerr << "FAIL: " << message << '\n';
    std::abort();
  }
  std::cout << "PASS: " << message << '\n';
}

std::vector<uint8_t> ad_field(const std::vector<uint8_t> &data, uint8_t type) {
  std::vector<uint8_t> result;
  for (size_t offset = 0; offset < data.size();) {
    size_t length = data[offset];
    assert(length >= 1 && offset + 1 + length <= data.size());
    if (data[offset + 1] == type) {
      assert(result.empty());
      result.assign(data.begin() + offset + 2, data.begin() + offset + 1 + length);
    }
    offset += 1 + length;
  }
  return result;
}

int main() {
  { reset(); TestServer s; boot(s);
    const std::vector<std::vector<uint8_t>> invalid{
      {}, {0x01}, {0xa1}, {0xa1, 0xb2}, {0xb2, 0xc3}, {0xc3, 0xb2, 0xa1},
      {0xa1, 0xb2, 0xc3, 0x00}, {0x00, 0xa1, 0xb2, 0xc3},
      {'a','1','b','2','c','3'}, {'0','x','a','1','b','2','c','3'}, {0, 0, 1}};
    for (const auto &data : invalid) raw_write(data.data(), data.size());
    raw_write(nullptr, 3);
    const uint8_t tiny = 0xa1;
    raw_write(&tiny, UINT16_MAX);  // Length must be checked before reading any bytes.
    const uint8_t valid[] = {0xa1, 0xb2, 0xc3};
    raw_write(valid, 3, 0, 2, 0xff);
    check(s.queued() == 0 && s.rejected() == invalid.size() + 3,
          "empty, old, partial, oversized, text, reversed, null and invalid-connection writes stay out of queue");
    for (unsigned byte = 0; byte < 256; byte++) {
      const uint8_t one = byte;
      raw_write(&one, 1);
      for (unsigned index = 0; index < 3; index++) {
        uint8_t changed[] = {0xa1, 0xb2, 0xc3};
        if (changed[index] != byte) { changed[index] = byte; raw_write(changed, 3); }
      }
    }
    check(s.queued() == 0 && s.get_write_trigger()->calls == 0,
          "all one-byte values and all single-byte mutations of the test token are rejected");
    raw_write(valid, 3, 1, 2); raw_write(valid, 3, 0, 1);
    tick(s);
    check(s.get_write_trigger()->calls == 0, "correct bytes on another profile or attribute cannot trigger command");
    uint8_t transient[] = {0xa1, 0xb2, 0xc3};
    raw_write(transient, 3); std::memset(transient, 0, 3); tick(s);
    check(s.get_write_trigger()->calls == 1 && s.get_write_trigger()->last == 0xA1B2C3,
          "exact command copied before SDK buffer expires and delivered once as uint32");
  }
  { reset(); TestServer s; boot(s);
    for (int cycle = 0; cycle < 100; cycle++) {
      for (int i = 0; i < 100; i++) write(0xA1B2C4);
      tick(s, 100);
    }
    check(s.rejected() == 10000 && s.queued() == 0 && rejection_reports == 2 && !s.rebooted,
          "10,000 wrong commands use no event slots and produce at most one rejection report per five seconds");
    write(); tick(s);
    check(s.get_write_trigger()->calls == 1, "valid command still works immediately after wrong-write flood");
  }
  { reset(); TestServer s; boot(s); connect(s);
    for (int i = 0; i < 70; i++) { write(0xA1B2C4); tick(s); }
    check(disconnects == 1 && starts == 2 && !s.rebooted && s.get_write_trigger()->calls == 0,
          "wrong-write traffic cannot extend the five-second connection lifetime or prevent re-advertising");
  }
  { reset(); TestServer s; boot(s); s.set_command(0x123456);
    write(); write(0x123456); tick(s);
    check(s.get_write_trigger()->calls == 1 && s.get_write_trigger()->last == 0x123456,
          "configured command replaces the test fixture token without accepting a second token");
    att_info_req_t good{0, 2, 0, 255}, wrong{0, 1, 99, 0};
    notice(BLE_5_ATT_INFO_REQ, &good); notice(BLE_5_ATT_INFO_REQ, &wrong);
    read_req_t read{99}; notice(BLE_5_READ_EVENT, &read);
    check(good.length == 3 && good.status == 0 && wrong.length == 0 && wrong.status == 0x03 && read.length == 0,
          "ATT info exposes three bytes only on command attribute and reads expose no value");
  }
  for (const std::string name : {"G", "Garage 7", "Garage 8", "Garage 12", "12345678901234567"}) {
    reset(); TestServer s; setup(s, name); run(s, 60);
    const std::vector<uint8_t> expected_name(name.begin(), name.end());
    const std::vector<uint8_t> expected_uuid{
      0x01, 0xa0, 0x21, 0x8d, 0x0e, 0x5f, 0x6c, 0xa3,
      0xb0, 0x4e, 0x4f, 0x9b, 0xa0, 0xd4, 0xb7, 0xc6};
    check(ad_field(advertisement, 0x01) == std::vector<uint8_t>{0x06} &&
          ad_field(advertisement, 0x07) == expected_uuid &&
          ad_field(advertisement, 0x09) == (name.size() <= 8 ? expected_name : std::vector<uint8_t>{}) &&
          ad_field(scan_response, 0x09) == expected_name &&
          advertisement.size() == (name.size() <= 8 ? 23 + name.size() : 21),
          ("valid advertisement and complete name: " + name).c_str());
    auto original = advertisement;
    activity = ACTV_IDLE; run(s, 20);
    check(advertisement == original && ad_field(scan_response, 0x09) == expected_name,
          "advertising recovery preserves both payloads");
  }
  { reset(); TestServer s; s.set_name_add_mac_suffix(true); setup(s, "Garage 7"); run(s, 60);
    const std::string name = "Garage 7-020100";
    check(ad_field(advertisement, 0x09).empty() &&
          ad_field(scan_response, 0x09) == std::vector<uint8_t>(name.begin(), name.end()),
          "MAC suffix retains the full name in the scan response");
  }
  { reset(); TestServer s; boot(s);
    for (int i = 0; i < 1440; i++) tick(s, 60000);
    check(!s.rebooted && starts == 1, "24 simulated idle hours: no arbitrary reboot or advertising expiry");
  }
  { reset(); TestServer s; boot(s);
    for (int i = 0; i < 100; i++) { connect(s); write(); run(s, 70); }
    check(!s.rebooted && disconnects == 100 && starts == 101 && s.get_write_trigger()->calls == 100,
          "100 connect/write/timeout/disconnect cycles: each write delivered once");
  }
  { reset(); TestServer s; boot(s); activity = ACTV_ADV_CREATED; run(s, 20);
    check(starts == 2 && !s.rebooted, "stopped advertising resumes");
    activity = ACTV_IDLE; run(s, 20);
    check(creates == 2 && starts == 3 && !s.rebooted, "lost activity is recreated with data and name");
  }
  { reset(); TestServer s; boot(s); omit_callback = true; activity = ACTV_ADV_CREATED; run(s, 20);
    check(s.phase() == Phase::RUNNING && starts == 2, "lost start callback reconciled from SDK state");
    activity = ACTV_IDLE; run(s, 20); omit_callback = false; run(s, 30);
    check(s.phase() == Phase::RUNNING && !s.rebooted, "lost create/data callbacks recover once delivery resumes");
  }
  { reset(); TestServer s; boot(s); disconnect_notice = false; connect(s, false); run(s, 70);
    check(s.connection() == 0xff && starts == 2 && !s.rebooted, "missing connection notices recovered from SDK table");
  }
  { reset(); TestServer s; boot(s); complete_disconnect = false; connect(s); run(s, 90);
    check(disconnects >= 4 && disconnects <= 5, "accepted but incomplete disconnect retried once per second");
    complete_disconnect = true; run(s, 20);
    check(starts == 2 && !s.rebooted, "disconnect retry recovers without reboot");
  }
  { reset(); TestServer s; boot(s); complete_disconnect = false; connect(s); run(s, 400);
    check(!s.rebooted, "recovery respects startup grace");
    run(s, 850);
    check(s.rebooted && disconnects <= 30, "persistently stuck connection triggers bounded reboot fallback");
  }
  { reset(); TestServer s; boot(s); tick(s, 120000); activity = ACTV_ADV_CREATED; stuck_command = true;
    run(s, 320);
    check(s.rebooted, "stuck SDK command reboots after 30 seconds");
  }
  { reset(); TestServer s; boot(s); tick(s, 120000); ready = 0; run(s, 310);
    check(s.rebooted, "SDK unexpectedly busy while idle triggers recovery");
  }
  { reset(); TestServer s; boot(s);
    for (int i = 0; i < 16; i++) write();
    tick(s);
    check(s.get_write_trigger()->calls == 0 && !s.rebooted, "overflow discards buffered commands without disabling BLE");
    for (int cycle = 0; cycle < 100; cycle++) {
      for (int i = 0; i < 32; i++) write(1, 2);
      tick(s);
    }
    write(); tick(s);
    check(s.get_write_trigger()->calls == 1 && !s.rebooted, "repeated invalid-write bursts do not exhaust the event pool");
  }
  { reset(); TestServer s; boot(s); s.set_connection_timeout(0); connect(s); tick(s, 86400000);
    check(disconnects == 0 && !s.rebooted, "explicit unlimited connection setting preserved");
  }
  { reset(UINT32_MAX - 20000); TestServer s; boot(s); tick(s, 12000); connect(s); run(s, 80);
    check(disconnects == 1 && starts == 2 && !s.rebooted, "connection and advertising deadlines survive millis rollover");
  }
  { reset(); TestServer s; boot(s); tick(s, 120000); bool idle = false;
    s.set_reboot_condition([&] { return idle; });
    stuck_command = true; activity = ACTV_ADV_CREATED; run(s, 400);
    write(); tick(s);
    check(!s.rebooted && s.get_write_trigger()->calls == 0, "pending recovery waits for application idle and suppresses new writes");
    idle = true; tick(s);
    check(s.rebooted, "recovery proceeds once application permits reboot");
  }
  { reset(); TestServer s; boot(s);
    s.on_ota_global_state(esphome::ota::OTA_STARTED, 0, 0, nullptr);
    write(); tick(s, 300000);
    check(!s.rebooted && s.get_write_trigger()->calls == 0, "OTA suppresses writes and recovery reboots");
    s.on_ota_global_state(esphome::ota::OTA_ABORT, 0, 0, nullptr); run(s, 20);
    check(activity == ACTV_ADV_STARTED && !s.rebooted, "aborted OTA resumes advertising with fresh deadlines");
  }
  { reset(); TestServer s; boot(s); tick(s, 120000);
    bool idle = false;
    s.set_reboot_condition([&] { return idle; });
    stuck_command = true; activity = ACTV_ADV_CREATED; run(s, 305);
    s.on_ota_global_state(esphome::ota::OTA_STARTED, 0, 0, nullptr);
    idle = true; tick(s, 300000);
    check(!s.rebooted, "OTA defers an already pending recovery reboot");
    s.on_ota_global_state(esphome::ota::OTA_ERROR, 0, 0, nullptr); tick(s);
    check(s.rebooted, "pending recovery resumes after OTA error");
  }
  { reset(); lose_db_notice = true; TestServer s; setup(s); run(s, 1300);
    check(s.rebooted, "missing database completion triggers recovery without recreating database in place");
  }
  { reset(); db_status = 1; TestServer s; setup(s); run(s, 1300);
    check(s.rebooted, "database error triggers recovery instead of permanent component failure");
  }
}
