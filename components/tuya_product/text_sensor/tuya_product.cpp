#include "tuya_product.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

namespace esphome::tuya_product {

static const char *const TAG = "tuya_product.text_sensor";

void TuyaProductComponent::setup() {
  this->parent_->add_debug_callback([this](uart::UARTDirection direction, uint8_t byte) {
    if (!this->captured_ && direction == uart::UART_DIRECTION_RX) {
      this->process_byte_(byte);
    }
  });
}

void TuyaProductComponent::dump_config() {
  if (this->product_id_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("  ", "Product ID", this->product_id_sensor_);
  }
  if (this->mcu_version_sensor_ != nullptr) {
    LOG_TEXT_SENSOR("  ", "MCU Version", this->mcu_version_sensor_);
  }
}

void TuyaProductComponent::start_or_reset_(uint8_t byte) {
  this->parser_state_ = ParserState::HEADER_FIRST;
  this->command_ = 0;
  this->checksum_ = 0;
  this->payload_length_ = 0;
  this->payload_index_ = 0;
  this->product_.clear();

  if (byte == 0x55) {
    this->checksum_ = byte;
    this->parser_state_ = ParserState::HEADER_SECOND;
  }
}

void TuyaProductComponent::process_byte_(uint8_t byte) {
  switch (this->parser_state_) {
    case ParserState::HEADER_FIRST:
      this->start_or_reset_(byte);
      return;

    case ParserState::HEADER_SECOND:
      if (byte != 0xAA) {
        this->start_or_reset_(byte);
        return;
      }
      this->checksum_ += byte;
      this->parser_state_ = ParserState::VERSION;
      return;

    case ParserState::VERSION:
      this->checksum_ += byte;
      this->parser_state_ = ParserState::COMMAND;
      return;

    case ParserState::COMMAND:
      this->command_ = byte;
      this->checksum_ += byte;
      this->parser_state_ = ParserState::LENGTH_HIGH;
      return;

    case ParserState::LENGTH_HIGH:
      this->payload_length_ = static_cast<uint16_t>(byte) << 8;
      this->checksum_ += byte;
      this->parser_state_ = ParserState::LENGTH_LOW;
      return;

    case ParserState::LENGTH_LOW:
      this->payload_length_ |= byte;
      this->checksum_ += byte;
      this->payload_index_ = 0;
      if (this->command_ == PRODUCT_QUERY && this->payload_length_ <= MAX_PRODUCT_LENGTH) {
        this->product_.reserve(this->payload_length_);
      }
      this->parser_state_ =
          this->payload_length_ == 0 ? ParserState::CHECKSUM : ParserState::PAYLOAD;
      return;

    case ParserState::PAYLOAD:
      this->checksum_ += byte;
      if (this->command_ == PRODUCT_QUERY && this->payload_length_ <= MAX_PRODUCT_LENGTH) {
        this->product_.push_back(static_cast<char>(byte));
      }
      if (++this->payload_index_ == this->payload_length_) {
        this->parser_state_ = ParserState::CHECKSUM;
      }
      return;

    case ParserState::CHECKSUM:
      if (byte == this->checksum_ && this->command_ == PRODUCT_QUERY &&
          this->payload_length_ > 0 && this->payload_length_ <= MAX_PRODUCT_LENGTH) {
        this->publish_product_();
        this->captured_ = true;
        std::string().swap(this->product_);
        return;
      }
      this->start_or_reset_(byte);
      return;
  }
}

void TuyaProductComponent::publish_product_() {
  bool parsed = json::parse_json(this->product_, [this](JsonObject root) -> bool {
    if (this->product_id_sensor_ != nullptr && root["p"].is<const char *>()) {
      this->product_id_sensor_->publish_state(root["p"].as<const char *>());
    }
    if (this->mcu_version_sensor_ != nullptr && root["v"].is<const char *>()) {
      this->mcu_version_sensor_->publish_state(root["v"].as<const char *>());
    }
    return true;
  });

  if (!parsed) {
    ESP_LOGW(TAG, "Product response contains invalid JSON");
  }
}

}  // namespace esphome::tuya_product
