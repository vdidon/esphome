#include "deep_sleep_component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"

#ifdef USE_RP2040

#include <hardware/rtc.h>
#include <hardware/sync.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <hardware/xosc.h>
#include <hardware/pll.h>
#include <hardware/resets.h>
#include <hardware/structs/scb.h>
#include <hardware/structs/rosc.h>

namespace esphome {
namespace deep_sleep {

static const char *const TAG = "deep_sleep.rp2040";
static const uint32_t SCRATCH_SLEEP_TIME = 0;  // Index pour sauvegarder le temps de sleep
static const uint32_t SCRATCH_FLAGS = 1;       // Index pour sauvegarder les flags
static const uint32_t SCRATCH_MAGIC = 0xC0DEBEEF;  // Magic number pour détecter le réveil

void RP2040DeepSleepComponent::deep_sleep_() {
  if (this->sleep_duration_.has_value()) {
    ESP_LOGD(TAG, "Setting up sleep duration: %u ms", *this->sleep_duration_);
    
    // Initialiser le RTC si pas déjà fait
    rtc_init();
    sleep_us(64);  // Attendre que le RTC soit stable
    
    // Sauvegarder la durée de sleep dans le registre SCRATCH
    uint32_t sleep_ms = *this->sleep_duration_ / 1000;  // Convertir en millisecondes
    watchdog_hw->scratch[SCRATCH_SLEEP_TIME] = sleep_ms;
    watchdog_hw->scratch[SCRATCH_FLAGS] = SCRATCH_MAGIC;  // Marquer comme réveil de deep sleep
  }

  if (this->wakeup_pin_ != nullptr) {
    ESP_LOGD(TAG, "Setting up wakeup pin: GPIO%d", this->wakeup_pin_->get_pin());
    
    // Configurer le GPIO pour le réveil
    gpio_init(this->wakeup_pin_->get_pin());
    gpio_set_dir(this->wakeup_pin_->get_pin(), GPIO_IN);
    gpio_pull_up(this->wakeup_pin_->get_pin());
    
    // Configurer l'interruption GPIO pour le réveil en mode dormant
    gpio_set_dormant_irq_enabled(this->wakeup_pin_->get_pin(), GPIO_IRQ_LEVEL_HIGH, true);
  }

  ESP_LOGD(TAG, "Entering deep sleep...");

  // 1. Désactiver tous les périphériques sauf RTC et GPIO
  uint32_t peripherals = 
    RESETS_RESET_UART0_BITS |
    RESETS_RESET_UART1_BITS |
    RESETS_RESET_USBCTRL_BITS |
    RESETS_RESET_I2C0_BITS |
    RESETS_RESET_I2C1_BITS |
    RESETS_RESET_SPI0_BITS |
    RESETS_RESET_SPI1_BITS |
    RESETS_RESET_ADC_BITS |
    RESETS_RESET_PWM_BITS;
  
  reset_block(peripherals);

  // 2. Configurer l'horloge pour utiliser le crystal XOSC en mode basse consommation
  xosc_init();
  
  // Désactiver le PLL
  pll_deinit(pll_sys);
  pll_deinit(pll_usb);
  
  // Configurer l'oscillateur en anneau (ROSC) en mode basse puissance
  rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_DISABLE;
  
  // 3. Configurer le RTC pour le réveil si nécessaire
  if (this->sleep_duration_.has_value()) {
    datetime_t t;
    rtc_get_datetime(&t);
    
    // Calculer le temps de réveil
    uint32_t sleep_ms = *this->sleep_duration_ / 1000;
    uint32_t total_seconds = t.sec + (sleep_ms / 1000);
    t.sec = total_seconds % 60;
    uint32_t total_minutes = t.min + (total_seconds / 60);
    t.min = total_minutes % 60;
    t.hour = (t.hour + (total_minutes / 60)) % 24;
    
    // Configurer l'alarme RTC avec callback
    rtc_set_alarm(&t, [](void) {
      // Cette fonction sera appelée au réveil
      watchdog_reboot(0, 0, 0);
    });
  }
  
  // 4. Entrer en mode dormant profond
  scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;  // Activer le mode deep sleep
  
  // Désactiver toutes les interruptions sauf RTC et GPIO
  irq_set_mask_enabled(0xFFFFFFFF, false);
  if (this->wakeup_pin_ != nullptr) {
    irq_set_enabled(IO_IRQ_BANK0, true);
  }
  irq_set_enabled(RTC_IRQ, true);
  
  // Entrer en mode dormant profond
  __dsb();
  __wfi();
  
  // Le code suivant ne sera jamais exécuté car le watchdog rebootera
  // le système au réveil via le callback RTC
}

bool RP2040DeepSleepComponent::prepare_to_sleep_() {
  return true;  // Toujours prêt à dormir sur RP2040
}

void RP2040DeepSleepComponent::dump_config_platform_() {
  ESP_LOGCONFIG(TAG, "  Platform: RP2040");
}

void RP2040DeepSleepComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up deep sleep...");
  
  // Vérifier si on se réveille d'un deep sleep
  if (watchdog_hw->scratch[SCRATCH_FLAGS] == SCRATCH_MAGIC) {
    ESP_LOGD(TAG, "Waking up from deep sleep");
    watchdog_hw->scratch[SCRATCH_FLAGS] = 0;  // Effacer le magic number
    
    // Restaurer la durée de sleep si nécessaire
    uint32_t saved_sleep_ms = watchdog_hw->scratch[SCRATCH_SLEEP_TIME];
    if (saved_sleep_ms > 0) {
      this->sleep_duration_ = saved_sleep_ms * 1000ULL;  // Convertir en microsecondes
    }
  }
  
  DeepSleepComponent::setup();  // Appeler la configuration de base
}

// Créer une instance globale de la classe dérivée
static RP2040DeepSleepComponent deep_sleep_component;

}  // namespace deep_sleep
}  // namespace esphome

#endif  // USE_RP2040 