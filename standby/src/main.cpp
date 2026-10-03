// RETROTV standby app (docs/WAKEWORD.md): the experimental «Hola ESP» wake word for STANDBY VOZ.
//
// The TV firmware (app0, Arduino 2.0.17 / ESP-IDF 4.4) cannot link ESP-SR 2.5.5, which needs ESP-IDF 5.
// So STANDBY VOZ in the voice_ww builds boots this small app instead (app1, ESP-IDF 5.4.1). It makes
// app0 the boot partition again before anything else, so any reset, crash or watchdog brings the TV
// back. Screen, backlight, speaker and LED stay off; the Wi-Fi is never started. It listens at 16 kHz,
// 16 bits, mono: WakeNet «Hola ESP» and the firmware's own clap detector (three claps with quiet
// around them) get the same blocks. A wake word, three claps, a key or serial W restart into the TV;
// a flat battery ends in deep sleep, as in the firmware. Audio is never stored: each block is
// analysed and overwritten by the next one.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board_config.h"
#include "config.h"
#include "es8311.h"
#include "power/Battery.h"
#include "voice/ClapDetector.h"
#include "voice/MicMeter.h"
#include "voice/VoiceStandby.h"

#define SLOG(fmt, ...) printf("[STANDBY] " fmt "\n", ##__VA_ARGS__)

namespace {

constexpr uint32_t RATE = 16000;            // WakeNet's rate; MCLK 4.096 MHz is a row of the ES8311 table
constexpr i2s_mclk_multiple_t MCLK_MULTIPLE = I2S_MCLK_MULTIPLE_256;
constexpr size_t MAX_CHUNK = 1024;          // samples per WakeNet chunk (WakeNet10: 512)
constexpr const char* MODEL_PARTITION = "spiffs";  // srmodels.bin lives in the unused spiffs partition
constexpr const char* WAKE_WORD = "holaesp";
constexpr uint32_t MAX_MODELS = 16;         // a sane srmodels.bin header (ours has 1)
constexpr det_mode_t WAKE_MODE = DET_MODE_90;      // DET_MODE_95 hears more, and more false triggers
constexpr uint32_t WAKE_ARM_MS = 1500;      // detections this soon after start are ignored
constexpr uint32_t STATS_MS = 10000;
// WakeNet per 32 ms block (2026-10-03): 2.95 ms at 240 MHz, 3.55 at 160, 5.4 at 80 (16 % load): 80 MHz,
// as the firmware's STANDBY VOZ. Serial C cycles them; not F: in the TV that is the flat-battery sleep.
constexpr int CPU_STEPS_MHZ[] = {80, 160, 240};
constexpr gpio_num_t KEY_PINS[] = {GPIO_NUM_0, static_cast<gpio_num_t>(PIN_KEY_CH_DOWN),
                                   static_cast<gpio_num_t>(PIN_KEY_CH_UP),
                                   static_cast<gpio_num_t>(PIN_KEY_VOL_DOWN),
                                   static_cast<gpio_num_t>(PIN_KEY_VOL_UP)};
constexpr int BATTERY_SAMPLES = 8;

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Tenths of dB -> "-24.3" (as AppVoice.cpp).
void db10Text(int16_t db10, char* out, size_t len) {
  const int a = db10 < 0 ? -db10 : db10;
  snprintf(out, len, "%s%d.%d", db10 < 0 && a != 0 ? "-" : "", a / 10, a % 10);
}

struct Settings {
  bool clapOn = true;
  uint8_t clapSensitivity = CLAP_SENSITIVITY_DEFAULT;
  bool listenLed = true;
  bool keys = true;  // written by the firmware before the handover (PAUTV_HAS_KEYS)
};

// The firmware's NVS settings, read only: this app never writes them and never erases NVS.
Settings readSettings() {
  Settings s;
  const esp_err_t init = nvs_flash_init();
  nvs_handle_t h;
  if (init != ESP_OK || nvs_open("pautv", NVS_READONLY, &h) != ESP_OK) {
    SLOG("settings not readable (%s): defaults", esp_err_to_name(init));
    return s;
  }
  uint8_t v = 0;
  if (nvs_get_u8(h, "clap_on", &v) == ESP_OK) s.clapOn = v != 0;
  if (nvs_get_u8(h, "clap_sens", &v) == ESP_OK) s.clapSensitivity = v > 100 ? 100 : v;
  if (nvs_get_u8(h, "listen_led", &v) == ESP_OK) s.listenLed = v != 0;
  if (nvs_get_u8(h, "ww_keys", &v) == ESP_OK) s.keys = v != 0;
  nvs_close(h);
  return s;
}

void setOutput(int pin, int level) {
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
  gpio_reset_pin(static_cast<gpio_num_t>(pin));
  gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

// Any reset from now on boots the TV: app0 is the boot partition again. Without this the board would
// boot back into this app for ever, so it comes first and is checked again before every restart.
constexpr int BOOT_TV_TRIES = 3;

esp_err_t bootTvNext() {
  const esp_partition_t* tv = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (tv == nullptr) return ESP_ERR_NOT_FOUND;
  const esp_partition_t* next = esp_ota_get_boot_partition();
  if (next != nullptr && next->address == tv->address) return ESP_OK;
  esp_err_t err = ESP_FAIL;
  for (int i = 0; i < BOOT_TV_TRIES && err != ESP_OK; ++i) err = esp_ota_set_boot_partition(tv);
  return err;
}

void initKeys() {
  gpio_config_t c = {};
  for (const gpio_num_t pin : KEY_PINS) c.pin_bit_mask |= 1ull << pin;
  c.mode = GPIO_MODE_INPUT;
  c.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&c);
}

bool anyKeyDown() {
  for (const gpio_num_t pin : KEY_PINS) {
    if (gpio_get_level(pin) == 0) return true;
  }
  return false;
}

// ---- battery (the firmware's Diagnostics::readBatteryMv with the ESP-IDF 5 ADC driver) ----
adc_oneshot_unit_handle_t adc = nullptr;
adc_cali_handle_t adcCali = nullptr;
adc_channel_t adcChannel;

bool initBattery() {
  adc_unit_t unit;
  if (adc_oneshot_io_to_channel(PIN_BAT_ADC, &unit, &adcChannel) != ESP_OK) return false;
  adc_oneshot_unit_init_cfg_t u = {};
  u.unit_id = unit;
  adc_oneshot_chan_cfg_t ch = {};
  ch.atten = ADC_ATTEN_DB_12;
  ch.bitwidth = ADC_BITWIDTH_DEFAULT;
  adc_cali_curve_fitting_config_t k = {};
  k.unit_id = unit;
  k.chan = adcChannel;
  k.atten = ADC_ATTEN_DB_12;
  k.bitwidth = ADC_BITWIDTH_DEFAULT;
  return adc_oneshot_new_unit(&u, &adc) == ESP_OK && adc_oneshot_config_channel(adc, adcChannel, &ch) == ESP_OK &&
         adc_cali_create_scheme_curve_fitting(&k, &adcCali) == ESP_OK;
}

// 0 when no sample could be read: a failed read must never look like a flat battery.
uint32_t readBatteryMv() {
  uint32_t sum = 0;
  int good = 0;
  for (int i = 0; i < BATTERY_SAMPLES; ++i) {
    int raw = 0, mv = 0;
    if (adc_oneshot_read(adc, adcChannel, &raw) != ESP_OK || adc_cali_raw_to_voltage(adcCali, raw, &mv) != ESP_OK) continue;
    sum += static_cast<uint32_t>(mv);
    ++good;
  }
  return good > 0 ? sum / good * BAT_ADC_DIVIDER : 0;
}

// The firmware's App::sleepUntilWoken: a key wakes; without keys a timer checks whether it charges.
// app0 is already the boot partition, so the wake is the firmware's usual one.
[[noreturn]] void deepSleep(bool keys) {
  gpio_set_level(static_cast<gpio_num_t>(PIN_LED_FRONT), 0);  // a clap flash may be on: never latch it
  gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_BL), 0);
  gpio_set_level(static_cast<gpio_num_t>(PIN_AMP_EN), 1);
  for (const int pin : {PIN_LCD_BL, PIN_AMP_EN, PIN_LED_FRONT}) gpio_hold_en(static_cast<gpio_num_t>(pin));
  gpio_deep_sleep_hold_en();
  uint64_t mask = 0;
  for (const gpio_num_t pin : KEY_PINS) {
    mask |= 1ull << pin;
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (!keys) esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(FLAT_CHECK_S) * 1000000ull);
  SLOG("battery flat: deep sleep (%s)", keys ? "a key wakes it" : "checking the battery every few minutes");
  fflush(stdout);
  esp_deep_sleep_start();
}

// ---- audio: ES8311 microphone + I2S RX at 16 kHz ----
i2s_chan_handle_t rx = nullptr;

bool initI2s() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_std_config_t std = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = static_cast<gpio_num_t>(PIN_I2S_MCLK),
                   .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
                   .ws = static_cast<gpio_num_t>(PIN_I2S_WS),
                   .dout = I2S_GPIO_UNUSED,
                   .din = static_cast<gpio_num_t>(PIN_I2S_DIN),
                   .invert_flags = {}},
  };
  std.clk_cfg.mclk_multiple = MCLK_MULTIPLE;
  return i2s_new_channel(&chan, nullptr, &rx) == ESP_OK && i2s_channel_init_std_mode(rx, &std) == ESP_OK &&
         i2s_channel_enable(rx) == ESP_OK;  // MCLK runs before the codec is told to use it
}

// AudioManager::initCodec + initMic of the firmware, at 16 kHz, playback muted.
bool initCodec() {
  i2c_config_t i2c = {};
  i2c.mode = I2C_MODE_MASTER;
  i2c.sda_io_num = PIN_I2C_SDA;
  i2c.scl_io_num = PIN_I2C_SCL;
  i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
  i2c.scl_pullup_en = GPIO_PULLUP_ENABLE;
  i2c.master.clk_speed = I2C_FREQ_HZ;
  if (i2c_param_config(I2C_NUM_0, &i2c) != ESP_OK || i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
    return false;
  }
  es8311_handle_t codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  es8311_clock_config_t clk = {};
  clk.mclk_from_mclk_pin = true;
  clk.mclk_frequency = static_cast<int>(RATE) * 256;
  clk.sample_frequency = static_cast<int>(RATE);
  if (codec == nullptr || es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_microphone_config(codec, false) != ESP_OK ||
      es8311_microphone_gain_set(codec, static_cast<es8311_mic_gain_t>(MIC_ADC_SCALE)) != ESP_OK) {
    return false;
  }
  es8311_voice_mute(codec, true);
  const uint8_t regs[][2] = {{0x17, MIC_ADC_VOLUME_REG17}, {0x14, MIC_PGA_REG14}};
  for (const auto& r : regs) {
    if (i2c_master_write_to_device(I2C_NUM_0, I2C_ADDR_ES8311, r, 2, pdMS_TO_TICKS(100)) != ESP_OK) return false;
  }
  return true;
}

// ---- WakeNet ----
const esp_wn_iface_t* wn = nullptr;
model_iface_data_t* wnData = nullptr;

void logMemory(const char* when) {
  SLOG("memory %s: internal %u KB free (largest block %u KB), psram %u KB free", when,
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
       static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// srmodels.bin starts with its model count. ESP-SR trusts it: an erased partition (0xFFFFFFFF) would
// crash the loader, so look first.
bool modelPresent() {
  const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, MODEL_PARTITION);
  uint32_t count = 0;
  return part != nullptr && esp_partition_read(part, 0, &count, sizeof(count)) == ESP_OK && count >= 1 &&
         count <= MAX_MODELS;
}

// Claps keep working without it: a missing model only logs.
size_t initWakeNet() {
  logMemory("before WakeNet");
  const uint32_t t0 = nowMs();
  srmodel_list_t* models = modelPresent() ? esp_srmodel_init(MODEL_PARTITION) : nullptr;
  char* name = models != nullptr ? esp_srmodel_filter(models, ESP_WN_PREFIX, WAKE_WORD) : nullptr;
  if (name == nullptr) {
    SLOG("WakeNet: no \"%s\" model in partition \"%s\" (flash srmodels.bin): claps only", WAKE_WORD, MODEL_PARTITION);
    return 0;
  }
  wn = esp_wn_handle_from_name(name);
  wnData = wn != nullptr ? wn->create(name, WAKE_MODE) : nullptr;
  if (wnData == nullptr) {
    SLOG("WakeNet: model %s did not start: claps only", name);
    return 0;
  }
  const int chunk = wn->get_samp_chunksize(wnData);
  const int rate = wn->get_samp_rate(wnData);
  if (chunk <= 0 || static_cast<size_t>(chunk) > MAX_CHUNK || rate != static_cast<int>(RATE)) {
    SLOG("WakeNet: unexpected chunk %d / rate %d: claps only", chunk, rate);
    wn->destroy(wnData);
    wnData = nullptr;
    return 0;
  }
  SLOG("WakeNet: %s ready in %" PRIu32 " ms (chunk %d samples = %d ms, mode %s)", name, nowMs() - t0, chunk,
       chunk * 1000 / rate, WAKE_MODE == DET_MODE_90 ? "90" : "95");
  logMemory("with WakeNet");
  return static_cast<size_t>(chunk);
}

void setCpuMhz(int mhz) {
  esp_pm_config_t pm = {};
  pm.max_freq_mhz = mhz;
  pm.min_freq_mhz = mhz;
  const esp_err_t err = esp_pm_configure(&pm);
  SLOG("CPU %d MHz (%s)", mhz, esp_err_to_name(err));
}

int serialRead() {
  uint8_t c = 0;
  return usb_serial_jtag_read_bytes(&c, 1, 0) == 1 ? c : -1;
}

struct Stats {
  uint32_t chunks = 0;
  uint64_t detectUs = 0;
  uint32_t detectMaxUs = 0;
  uint32_t readErrors = 0;
  uint32_t ignored = 0;  // detections while arming
  uint32_t detections = 0;
  int16_t peak[2] = {0, 0};  // both I2S slots: the microphone should be in the first
};

// Restarts into the TV. Returns only if app0 cannot be made the boot partition: restarting would come
// back here, so it keeps listening (and logs; app0 needs reflashing).
void wake(const char* cause, uint32_t startMs) {
  const esp_err_t err = bootTvNext();
  if (err != ESP_OK) {
    SLOG("wake: %s, but the TV cannot be booted (%s): staying here", cause, esp_err_to_name(err));
    return;
  }
  SLOG("wake: %s, %" PRIu32 " s in standby: switching the TV on", cause, (nowMs() - startMs) / 1000);
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(50));  // let the USB serial send the line
  esp_restart();
}

}  // namespace

extern "C" void app_main() {
  const uint32_t startMs = nowMs();
  const esp_err_t bootTv = bootTvNext();  // before anything that could crash
  // Dark and silent: backlight off, amplifier shut down, LED off.
  setOutput(PIN_LCD_BL, 0);
  setOutput(PIN_AMP_EN, 1);
  setOutput(PIN_LED_FRONT, 0);
  usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  usb_serial_jtag_driver_install(&usb);  // input only (W, C); output goes through the secondary console
  const esp_app_desc_t* app = esp_app_get_description();
  SLOG("RETROTV standby app %s (ESP-IDF %s): «Hola ESP» and claps", app->version, app->idf_ver);
  SLOG("next boot: the TV (%s)", esp_err_to_name(bootTv));

  const Settings settings = readSettings();
  SLOG("settings: claps %s (sensitivity %u), LED %s, keys %s", settings.clapOn ? "on" : "off",
       settings.clapSensitivity, settings.listenLed ? "on" : "off", settings.keys ? "yes" : "no");
  initKeys();
  const bool batteryOk = initBattery();
  if (!batteryOk) SLOG("battery ADC failed: the flat-battery sleep is off");
  setCpuMhz(CPU_STEPS_MHZ[0]);

  if (!initI2s() || !initCodec()) {
    SLOG("audio failed (I2S or ES8311): back to the TV");
    wake("audio failure", startMs);
  }
  SLOG("audio: I2S %u Hz, 16 bits, MCLK %u Hz; microphone = left slot", static_cast<unsigned>(RATE),
       static_cast<unsigned>(RATE * 256));
  size_t chunk = initWakeNet();
  const bool wakeWord = chunk > 0;
  if (!wakeWord) chunk = 512;  // claps alone: 32 ms blocks

  ClapDetector clap(RATE);
  clap.setSensitivity(settings.clapSensitivity);
  clap.setMinDb10(CLAP_STANDBY_MIN_DB10);  // only a clap near the TV switches it on
  VoiceStandby standby;
  BatteryMonitor battery;

  static int16_t stereo[MAX_CHUNK * 2];
  static int16_t mono[MAX_CHUNK];
  Stats st;
  uint32_t statsMs = nowMs();
  uint32_t batteryMs = nowMs();
  uint32_t ledOffAtMs = 0;
  bool keysArmed = false;  // a key still down from switching off must be let go first
  size_t cpuStep = 0;
  bool countOnly = false;
  ClapDetector::Stats clapSeen;  // serial T: «Hola ESP» is logged and counted, but does not wake
  SLOG("listening (%s)", wakeWord ? "«Hola ESP» + three claps" : "three claps");

  for (;;) {
    size_t got = 0;
    const esp_err_t err = i2s_channel_read(rx, stereo, chunk * 2 * sizeof(int16_t), &got, pdMS_TO_TICKS(500));
    const uint32_t now = nowMs();
    const bool audio = err == ESP_OK && got == chunk * 2 * sizeof(int16_t);
    if (!audio && (++st.readErrors <= 5 || st.readErrors % 100 == 0)) {  // keys and battery still work
      SLOG("audio read error %" PRIu32 ": %s, %u bytes", st.readErrors, esp_err_to_name(err), static_cast<unsigned>(got));
    }
    for (size_t i = 0; audio && i < chunk; ++i) {
      mono[i] = stereo[2 * i];
      for (int s = 0; s < 2; ++s) {
        const int16_t v = stereo[2 * i + s];
        const int16_t a = v == INT16_MIN ? INT16_MAX : static_cast<int16_t>(v < 0 ? -v : v);
        if (a > st.peak[s]) st.peak[s] = a;
      }
    }
    st.chunks += audio ? 1 : 0;

    if (audio && wakeWord) {
      const int64_t t0 = esp_timer_get_time();
      const wakenet_state_t r = wn->detect(wnData, mono);
      const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
      st.detectUs += us;
      if (us > st.detectMaxUs) st.detectMaxUs = us;
      if (r == WAKENET_DETECTED) {
        if (now - startMs < WAKE_ARM_MS) {
          ++st.ignored;
          SLOG("«Hola ESP» %" PRIu32 " ms after start: ignored", now - startMs);
        } else {
          char peak[8];
          db10Text(micDb10(static_cast<float>(st.peak[0]) / 32768.0f), peak, sizeof(peak));
          SLOG("«Hola ESP» #%" PRIu32 " detected (detect %" PRIu32 " us, %" PRIu32 " ms after start, loudest %s dB)%s",
               ++st.detections, us, now - startMs, peak, countOnly ? ": counting only" : "");
          if (!countOnly) wake("«Hola ESP»", startMs);
        }
      }
    }

    if (audio && settings.clapOn) {
      clap.feed(mono, chunk, now);
      const uint8_t c = clap.takeClap();
      const uint8_t sequence = clap.takeSequence();
      StandbyAction a = standby.onClap(c);
      if (c != 0) {
        char peak[8];
        db10Text(clap.lastPeakDb10(), peak, sizeof(peak));
        SLOG("clap %u: peak %s dB, bright %u%%", c, peak, static_cast<unsigned>(clap.lastHfShare() * 100));
      }
      const ClapDetector::Stats& cs = clap.stats();  // what was dropped, as the firmware logs it
      if (cs.dull != clapSeen.dull) SLOG("clap too dull (bright %u%%)", static_cast<unsigned>(clap.lastHfShare() * 100));
      if (cs.sustained != clapSeen.sustained) SLOG("loud but too long: not a clap");
      if (cs.badSequences != clapSeen.badSequences) SLOG("not a clap sequence (%s)", clap.lastRejected());
      clapSeen = cs;
      if (sequence != 0) {
        const uint32_t quiet = clap.lastQuietBeforeMs();
        SLOG("sequence of %u (quiet before %ld ms)", sequence, quiet == UINT32_MAX ? -1L : static_cast<long>(quiet));
        standby.onSequence(sequence, quiet, clap.bangs(), now);
      }
      const bool pending = standby.pending();
      if (standby.update(clap.bangs(), now) == StandbyAction::Wake) {
        wake("three claps", startMs);
      } else if (pending && !standby.pending()) {
        SLOG("a bang right after: not switching on");
      }
      if (a == StandbyAction::Blink && settings.listenLed) {
        gpio_set_level(static_cast<gpio_num_t>(PIN_LED_FRONT), 1);
        ledOffAtMs = (now + LISTEN_LED_FLASH_MS) | 1;
      }
    }
    if (ledOffAtMs != 0 && static_cast<int32_t>(now - ledOffAtMs) >= 0) {
      gpio_set_level(static_cast<gpio_num_t>(PIN_LED_FRONT), 0);
      ledOffAtMs = 0;
    }

    const bool keyDown = anyKeyDown();
    if (keysArmed && keyDown) wake("a key", startMs);
    keysArmed = keysArmed || !keyDown;
    for (int c = serialRead(); c >= 0; c = serialRead()) {
      if (c == 'W') wake("serial W", startMs);
      if (c == 'T') {  // tests: hit rate per distance, false positives over hours
        countOnly = !countOnly;
        SLOG("«Hola ESP» %s", countOnly ? "counts only (T again: it wakes)" : "wakes the TV again");
      }
      if (c == 'C') {
        cpuStep = (cpuStep + 1) % (sizeof(CPU_STEPS_MHZ) / sizeof(CPU_STEPS_MHZ[0]));
        setCpuMhz(CPU_STEPS_MHZ[cpuStep]);
        st.detectUs = st.detectMaxUs = st.chunks = 0;  // the load is per speed
      }
    }

    if (batteryOk && now - batteryMs >= BATTERY_READ_MS) {
      batteryMs = now;
      const uint32_t mv = readBatteryMv();
      if (mv > 0) battery.update(mv);
      if (battery.empty()) {
        SLOG("battery flat (%" PRIu32 " mV)", battery.millivolts());
        deepSleep(settings.keys);
      }
    }
    if (now - statsMs >= STATS_MS) {
      statsMs = now;
      const uint32_t chunkUs = static_cast<uint32_t>(chunk * 1000000ull / RATE);
      const uint32_t avgUs = st.chunks > 0 ? static_cast<uint32_t>(st.detectUs / st.chunks) : 0;
      char floor[8], threshold[8];
      db10Text(clap.floorDb10(), floor, sizeof(floor));
      db10Text(clap.thresholdDb10(), threshold, sizeof(threshold));
      SLOG("stats: %d MHz, WakeNet %" PRIu32 " us avg / %" PRIu32 " us max per %" PRIu32 " us chunk (load %" PRIu32
           "%%), read errors %" PRIu32 ", floor %s dB, clap threshold %s dB, peaks L %d R %d, battery %" PRIu32 " mV, detections %" PRIu32,
           CPU_STEPS_MHZ[cpuStep], avgUs, st.detectMaxUs, chunkUs, chunkUs > 0 ? avgUs * 100 / chunkUs : 0,
           st.readErrors, floor, threshold, st.peak[0], st.peak[1], battery.millivolts(), st.detections);
      logMemory("now");
      st.peak[0] = st.peak[1] = 0;
    }
  }
}
