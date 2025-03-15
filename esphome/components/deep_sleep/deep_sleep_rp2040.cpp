#include "deep_sleep_component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#ifdef USE_RP2040

#include <hardware/rtc.h>
#include <hardware/sync.h>
#include <hardware/clocks.h>
#include <pico/sleep.h>

namespace esphome {
namespace deep_sleep {

void DeepSleepComponent::dump_config_platform_() {
  ESP_LOGCONFIG(TAG, "  Platform: RP2040");
  if (this->sleep_duration_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Sleep Duration: %" PRIu32 "ms", *this->sleep_duration_);
  }
  if (this->wakeup_pin_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Wakeup Pin: GPIO%d", this->wakeup_pin_->get_pin());
  }
}

bool DeepSleepComponent::prepare_to_sleep_() {
  if (this->sleep_duration_.has_value()) {
    uint32_t sleep_ms = *this->sleep_duration_;
    datetime_t t;
    rtc_get_datetime(&t);
    
    // Calculate wake time
    uint32_t total_seconds = t.sec + (sleep_ms / 1000);
    t.sec = total_seconds % 60;
    uint32_t total_minutes = t.min + (total_seconds / 60);
    t.min = total_minutes % 60;
    t.hour = (t.hour + (total_minutes / 60)) % 24;
    
    rtc_set_alarm(&t, nullptr);
  }

  if (this->wakeup_pin_ != nullptr) {
    gpio_set_dormant_irq_enabled(this->wakeup_pin_->get_pin(), IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_EDGE_HIGH_BITS, true);
  }

  return true;
}

void DeepSleepComponent::deep_sleep_() {
  ESP_LOGD(TAG, "Entering deep sleep...");

  // Prepare clocks for sleep
  sleep_run_from_xosc();
  
  // Enter sleep mode
  sleep_goto_dormant_until_edge_high(this->wakeup_pin_ != nullptr ? this->wakeup_pin_->get_pin() : -1);

  // After wake up, restore clocks
  clocks_init();
}

}  // namespace deep_sleep
}  // namespace esphome

#endif  // USE_RP2040 