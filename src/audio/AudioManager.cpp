#include "audio/AudioManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>

#include "app_types.h"
#include "board_config.h"
#include "config.h"
#include "es8311.h"
#include "voice/MicMeter.h"

namespace {

constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
constexpr size_t BLOCK_FRAMES = 256;
constexpr uint32_t SYNTH_TASK_STACK = 4096;
constexpr uint8_t AMP_ON = LOW;  // SC8002B SHUTDOWN is active high
constexpr uint8_t AMP_OFF = HIGH;

#if PAUTV_MIC_ENABLED
// One codec register over Wire (the driver keeps its own writer private).
bool i2cWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(I2C_ADDR_ES8311);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
#endif

}  // namespace

bool AudioManager::begin(uint8_t userVolume) {
  // Amplifier off while the codec is configured, so power-up clicks never reach the speaker.
  pinMode(PIN_AMP_EN, OUTPUT);
  digitalWrite(PIN_AMP_EN, AMP_OFF);

  i2sOk_ = initI2s();  // MCLK must be running before the codec is told to use it
  codecOk_ = i2sOk_ && initCodec();
  if (codecOk_) {
    setVolume(userVolume);
    // The ES8311 keeps its registers across an ESP32 reset (it stays powered, and es8311_init's
    // reset only restarts its state machines): a TV restarted while muted would come back silent
    // while it believes it has sound. The app always starts unmuted, so the codec does too.
    setMuted(false);
  }
  micOk_ = codecOk_ && initMic();  // a microphone that fails leaves playback as it was

  requests_ = xQueueCreate(1, sizeof(SoundRequest));
  i2sLock_ = xSemaphoreCreateMutex();
  ready_ = codecOk_ && requests_ != nullptr && i2sLock_ != nullptr &&
           xTaskCreatePinnedToCore(taskEntry, "audio-fx", SYNTH_TASK_STACK, this, AUDIO_TASK_PRIO,
                                   nullptr, AUDIO_TASK_CORE) == pdPASS;
  if (ready_) digitalWrite(PIN_AMP_EN, AMP_ON);

  PLOG("AUDIO", "%s (I2S %u Hz, MCLK x%d = %u Hz)", status(), AUDIO_SAMPLE_RATE,
       AUDIO_MCLK_MULTIPLE, AUDIO_SAMPLE_RATE * AUDIO_MCLK_MULTIPLE);
  return ready_;
}

const char* AudioManager::status() const {
  if (!i2sOk_) return "I2S FAIL";
  if (!codecOk_) return "CODEC FAIL";
  return ready_ ? "OK" : "TASK FAIL";
}

bool AudioManager::initI2s() {
  i2s_config_t cfg = {};
  // With the microphone built in, RX runs next to TX on the same port: both share BCLK, WS and
  // MCLK, so the playback format, DMA ring and latency stay exactly as they were.
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | (PAUTV_MIC_ENABLED ? I2S_MODE_RX : 0));
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = AUDIO_DMA_BUF_COUNT;
  cfg.dma_buf_len = AUDIO_DMA_BUF_LEN;
  cfg.use_apll = false;            // the S3 has no APLL
  cfg.tx_desc_auto_clear = true;   // an underrun plays silence, not the last buffer again
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    PLOG("AUDIO", "i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_I2S_MCLK;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = PAUTV_MIC_ENABLED ? PIN_I2S_DIN : I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    PLOG("AUDIO", "i2s_set_pin failed");
    return false;
  }
  return true;
}

// es8311_create + es8311_init directly: Freenove's es8311_codec_init() configures 16 kHz x384
// and aborts via ESP_ERROR_CHECK on failure (docs/HARDWARE.md, conflict 4).
bool AudioManager::initCodec() {
  codec_ = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);  // I2C_NUM_0 = Wire
  if (codec_ == nullptr) return false;

  es8311_clock_config_t clk = {};
  clk.mclk_inverted = false;
  clk.sclk_inverted = false;
  clk.mclk_from_mclk_pin = true;
  clk.mclk_frequency = static_cast<int>(AUDIO_SAMPLE_RATE) * AUDIO_MCLK_MULTIPLE;
  clk.sample_frequency = static_cast<int>(AUDIO_SAMPLE_RATE);
  const esp_err_t err = es8311_init(codec_, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  if (err != ESP_OK) {
    PLOG("AUDIO", "es8311_init failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

// The analogue MEMS microphone on MIC1 (Freenove's echo sketch: es8311_microphone_config), with
// our gains. es8311_init already powered the PGA and the ADC modulator.
bool AudioManager::initMic() {
#if PAUTV_MIC_ENABLED
  if (es8311_microphone_config(codec_, false) != ESP_OK ||
      es8311_microphone_gain_set(codec_, static_cast<es8311_mic_gain_t>(MIC_ADC_SCALE)) != ESP_OK) {
    PLOG("MIC", "codec microphone setup failed: playback only");
    return false;
  }
  // es8311_microphone_config writes REG17 = 0xC8 and REG14 = 0x1A; ours may differ.
  if (!i2cWrite(0x17, MIC_ADC_VOLUME_REG17) || !i2cWrite(0x14, MIC_PGA_REG14)) {
    PLOG("MIC", "codec microphone gain write failed: playback only");
    return false;
  }
  PLOG("MIC", "ready: I2S RX on GPIO%d, ES8311 MIC1, PGA reg14=0x%02X, scale reg16=%u, ADC vol reg17=0x%02X",
       PIN_I2S_DIN, MIC_PGA_REG14, MIC_ADC_SCALE, MIC_ADC_VOLUME_REG17);
  return true;
#else
  return false;
#endif
}

size_t AudioManager::readMic(int16_t* stereo, size_t frames, uint32_t timeoutMs) {
  if (!micOk_) return 0;
  size_t read = 0;
  i2s_read(I2S_PORT, stereo, frames * 2 * sizeof(int16_t), &read, pdMS_TO_TICKS(timeoutMs));
  return read / (2 * sizeof(int16_t));
}

void AudioManager::setVolume(uint8_t userVolume) {
  if (!codecOk_) return;
#if PAUTV_MIC_ENABLED
  fxUntilMs_.store(millis());
#endif
  const int codec = codecVolumeFor(userVolume);
  if (es8311_voice_volume_set(codec_, codec, nullptr) != ESP_OK) {
    PLOG("AUDIO", "volume write failed");
    return;
  }
  PLOG("AUDIO", "volume %u (codec %d)", userVolume, codec);
}

void AudioManager::setMuted(bool muted) {
  if (!codecOk_) return;
#if PAUTV_MIC_ENABLED
  fxUntilMs_.store(millis());  // the speaker pops as the DAC mutes: not a clap
  muted_.store(muted);
#endif
  if (es8311_voice_mute(codec_, muted) != ESP_OK) PLOG("AUDIO", "mute write failed");
}

void AudioManager::beep() { tone(BEEP_HZ, BEEP_MS, BEEP_LEVEL_PCT); }

void AudioManager::tone(uint16_t hz, uint32_t ms, uint8_t levelPct) {
  play(SoundRequest{SoundKind::Tone, hz, ms, levelPct});
}

void AudioManager::noise(uint32_t ms, uint8_t levelPct) {
  play(SoundRequest{SoundKind::Noise, 0, ms, levelPct});
}

void AudioManager::stopSound() { play(SoundRequest{SoundKind::Noise, 0, 0, 0}); }

void AudioManager::play(const SoundRequest& r) {
  if (!ready_) return;
#if PAUTV_MIC_ENABLED
  fxUntilMs_.store(millis() + r.durationMs);
#endif
  xQueueOverwrite(requests_, &r);
}

size_t AudioManager::writeStereo(const int16_t* stereo, size_t frames) {
  if (xSemaphoreTake(i2sLock_, pdMS_TO_TICKS(AUDIO_WRITE_TIMEOUT_MS)) != pdTRUE) return 0;
  size_t written = 0;
  i2s_write(I2S_PORT, stereo, frames * 2 * sizeof(int16_t), &written,
            pdMS_TO_TICKS(AUDIO_WRITE_TIMEOUT_MS));
  xSemaphoreGive(i2sLock_);
  return written / (2 * sizeof(int16_t));
}

size_t AudioManager::writePcm(const int16_t* pcm, size_t frames, uint8_t channels) {
  if (!ready_) return 0;
  int16_t stereo[BLOCK_FRAMES * 2];
  size_t done = 0;
  while (done < frames) {
    const size_t n = frames - done < BLOCK_FRAMES ? frames - done : BLOCK_FRAMES;
    for (size_t i = 0; i < n; ++i) {
      const int16_t* src = pcm + (done + i) * channels;
      stereo[2 * i] = src[0];
      stereo[2 * i + 1] = channels > 1 ? src[1] : src[0];  // mono feeds both sides
    }
    const size_t written = writeStereo(stereo, n);
#if PAUTV_MIC_ENABLED
    // Heard once the DMA ring ahead of it drains. Muted, the speaker plays nothing.
    if (written > 0) {
      float sum = 0.0f;
      for (size_t i = 0; i < written; ++i) {
        const float v = playbackHighPass_.step(static_cast<float>(stereo[2 * i]));
        sum += v * v;
      }
      const int16_t db10 = muted_.load() ? MIC_FLOOR_DB10 : micDb10(sqrtf(sum / written) / 32768.0f);
      playback_.push(millis() + latencyFrames() * 1000 / AUDIO_SAMPLE_RATE, db10);
    }
#endif
    done += written;
    if (written < n) break;  // DAC stalled: report what went through
  }
  return done;
}

void AudioManager::taskEntry(void* self) { static_cast<AudioManager*>(self)->run(); }

// Sleeps on the queue while idle; while a sound plays it renders one block at a time and
// blocks in i2s_write, which paces it to the DAC. Stereo frames carry the mono sample twice.
void AudioManager::run() {
  Synth synth;
  int16_t mono[BLOCK_FRAMES];
  int16_t stereo[BLOCK_FRAMES * 2];
  SoundRequest request;

  for (;;) {
    const TickType_t wait = synth.active() ? 0 : portMAX_DELAY;
    if (xQueueReceive(requests_, &request, wait) == pdTRUE) synth.start(request, AUDIO_SAMPLE_RATE);
    const size_t frames = synth.render(mono, BLOCK_FRAMES);
    if (frames == 0) continue;

    for (size_t i = 0; i < frames; ++i) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    writeStereo(stereo, frames);
  }
}
