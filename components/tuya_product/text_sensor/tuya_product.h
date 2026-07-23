#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/tuya/tuya.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/core/component.h"

namespace esphome::tuya_product {

class TuyaProductComponent final : public Component {
 public:
  void set_uart_parent(uart::UARTComponent *parent) { this->parent_ = parent; }
  void set_tuya_parent(tuya::Tuya *parent) { this->tuya_parent_ = parent; }
  void set_product_id_sensor(text_sensor::TextSensor *sensor) { this->product_id_sensor_ = sensor; }
  void set_mcu_version_sensor(text_sensor::TextSensor *sensor) { this->mcu_version_sensor_ = sensor; }
  template<typename F> void add_on_initialized_callback(F &&callback) {
    this->initialized_callback_.add(std::forward<F>(callback));
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  enum class ParserState : uint8_t {
    HEADER_FIRST,
    HEADER_SECOND,
    VERSION,
    COMMAND,
    LENGTH_HIGH,
    LENGTH_LOW,
    PAYLOAD,
    CHECKSUM,
  };

  static constexpr uint8_t PRODUCT_QUERY = 0x01;
  static constexpr uint16_t MAX_PRODUCT_LENGTH = 512;

  void process_byte_(uint8_t byte);
  void publish_product_();
  void start_or_reset_(uint8_t byte);

  uart::UARTComponent *parent_{nullptr};
  tuya::Tuya *tuya_parent_{nullptr};
  text_sensor::TextSensor *product_id_sensor_{nullptr};
  text_sensor::TextSensor *mcu_version_sensor_{nullptr};
  CallbackManager<void()> initialized_callback_;
  ParserState parser_state_{ParserState::HEADER_FIRST};
  uint8_t command_{0};
  uint8_t checksum_{0};
  uint16_t payload_length_{0};
  uint16_t payload_index_{0};
  std::string product_{};
  bool captured_{false};
};

}  // namespace esphome::tuya_product
