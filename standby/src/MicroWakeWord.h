// SPDX-License-Identifier: GPL-3.0-only
// Adapted from ESPHome's micro_wake_word component, whose C++ code is GPLv3 (ESPHome License); the rest of
// RETROTV is MIT.
#pragma once

// A microWakeWord streaming model (docs/WAKEWORD.md, «Hey Retro» engine), ported from ESPHome's
// micro_wake_word component: the TFLite micro frontend turns 16 kHz audio into 40 features
// every 10 ms, the int8 model runs once per stride of features, and the wake word is heard when the
// mean of the last few probabilities passes the cutoff. Feed it the same blocks as WakeNet.

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include <frontend_util.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>

class MicroWakeWord {
 public:
  struct Config {
    const uint8_t* model;      // TFLite flatbuffer
    uint8_t cutoff;            // manifest probability_cutoff * 255
    uint8_t window;            // manifest sliding_window_size
    size_t arena;              // manifest tensor_arena_size
  };

  bool begin(const Config& c);
  // True once when the wake word was heard in these samples; then it waits ~1 s before the next one.
  bool feed(const int16_t* samples, size_t n);
  uint8_t lastMean() const { return lastMean_; }
  uint8_t lastMax() const { return lastMax_; }
  size_t arenaUsed() const { return interpreter_ ? interpreter_->arena_used_bytes() : 0; }

 private:
  static constexpr int FEATURES = 40;
  static constexpr int16_t SLICES_BEFORE_DETECTION = 100;  // 1 s of features after start or a detection
  static constexpr int WINDOW_MAX = 10;

  bool infer(const int8_t* features);
  void reset();

  FrontendConfig frontend_{};
  FrontendState state_{};
  tflite::MicroMutableOpResolver<20> ops_;
  std::unique_ptr<tflite::MicroInterpreter> interpreter_;
  tflite::MicroResourceVariables* variables_ = nullptr;
  uint8_t* arena_ = nullptr;
  uint8_t* varArena_ = nullptr;
  uint8_t cutoff_ = 0;
  uint8_t window_ = 1;
  uint8_t probs_[WINDOW_MAX] = {};
  uint8_t next_ = 0;
  uint8_t stride_ = 1;
  uint8_t step_ = 0;
  int16_t ignore_ = -SLICES_BEFORE_DETECTION;
  uint8_t lastMean_ = 0;
  uint8_t lastMax_ = 0;
};
