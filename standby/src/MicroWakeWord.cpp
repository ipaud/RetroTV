// SPDX-License-Identifier: GPL-3.0-only
// Adapted from ESPHome's micro_wake_word component (C++ under GPLv3, ESPHome License). See MicroWakeWord.h.
#include "MicroWakeWord.h"

#include <string.h>

#include <tensorflow/lite/schema/schema_generated.h>

#include "esp_heap_caps.h"

namespace {

constexpr int SAMPLE_RATE = 16000;
constexpr size_t VARIABLE_ARENA = 1024;  // the model's state variables (ESPHome's size)

// Internal RAM is faster; PSRAM if it is short.
uint8_t* alloc(size_t n) {
  void* p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p == nullptr) p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<uint8_t*>(p);
}

}  // namespace

bool MicroWakeWord::begin(const Config& c) {
  cutoff_ = c.cutoff;
  window_ = c.window < 1 ? 1 : (c.window > WINDOW_MAX ? WINDOW_MAX : c.window);

  // The preprocessor settings every microWakeWord model was trained with (ESPHome preprocessor_settings.h).
  frontend_.window.size_ms = 30;
  frontend_.window.step_size_ms = 10;
  frontend_.filterbank.num_channels = FEATURES;
  frontend_.filterbank.lower_band_limit = 125.0f;
  frontend_.filterbank.upper_band_limit = 7500.0f;
  frontend_.noise_reduction.smoothing_bits = 10;
  frontend_.noise_reduction.even_smoothing = 0.025f;
  frontend_.noise_reduction.odd_smoothing = 0.06f;
  frontend_.noise_reduction.min_signal_remaining = 0.05f;
  frontend_.pcan_gain_control.enable_pcan = 1;
  frontend_.pcan_gain_control.strength = 0.95f;
  frontend_.pcan_gain_control.offset = 80.0f;
  frontend_.pcan_gain_control.gain_bits = 21;
  frontend_.log_scale.enable_log = 1;
  frontend_.log_scale.scale_shift = 6;
  if (!FrontendPopulateState(&frontend_, &state_, SAMPLE_RATE)) return false;

  // The 20 operations of the streaming models (ESPHome streaming_model.cpp).
  ops_.AddCallOnce();
  ops_.AddVarHandle();
  ops_.AddReshape();
  ops_.AddReadVariable();
  ops_.AddStridedSlice();
  ops_.AddConcatenation();
  ops_.AddAssignVariable();
  ops_.AddConv2D();
  ops_.AddMul();
  ops_.AddAdd();
  ops_.AddMean();
  ops_.AddFullyConnected();
  ops_.AddLogistic();
  ops_.AddQuantize();
  ops_.AddDepthwiseConv2D();
  ops_.AddAveragePool2D();
  ops_.AddMaxPool2D();
  ops_.AddPad();
  ops_.AddPack();
  ops_.AddSplitV();

  const tflite::Model* model = tflite::GetModel(c.model);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;
  varArena_ = alloc(VARIABLE_ARENA);
  // ponytail: the manifest's arena plus half; ESPHome probes the exact size, a fixed margin is enough here.
  const size_t arena = (c.arena * 3 / 2 + 15) & ~static_cast<size_t>(15);
  arena_ = alloc(arena);
  if (varArena_ == nullptr || arena_ == nullptr) return false;
  tflite::MicroAllocator* va = tflite::MicroAllocator::Create(varArena_, VARIABLE_ARENA);
  variables_ = tflite::MicroResourceVariables::Create(va, 20);
  interpreter_.reset(new tflite::MicroInterpreter(model, ops_, arena_, arena, variables_));
  if (interpreter_->AllocateTensors() != kTfLiteOk) return false;

  const TfLiteTensor* in = interpreter_->input(0);
  const TfLiteTensor* out = interpreter_->output(0);
  if (in->dims->size != 3 || in->dims->data[2] != FEATURES || in->type != kTfLiteInt8 ||
      out->type != kTfLiteUInt8) {
    return false;
  }
  stride_ = static_cast<uint8_t>(in->dims->data[1]);
  reset();
  return true;
}

void MicroWakeWord::reset() {
  memset(probs_, 0, sizeof(probs_));
  ignore_ = -SLICES_BEFORE_DETECTION;
}

bool MicroWakeWord::feed(const int16_t* samples, size_t n) {
  bool heard = false;
  while (n > 0) {
    size_t used = 0;
    const FrontendOutput f = FrontendProcessSamples(&state_, samples, n, &used);
    samples += used;
    n -= used;
    if (f.size != FEATURES) continue;
    int8_t features[FEATURES];
    for (int i = 0; i < FEATURES; ++i) {
      // The frontend gives ~0..670; the model wants ((x / 25.6) / 26) * 256 - 128 (ESPHome's comment).
      int32_t v = (static_cast<int32_t>(f.values[i]) * 256 + 333) / 666 - 128;
      features[i] = static_cast<int8_t>(v < -128 ? -128 : (v > 127 ? 127 : v));
    }
    heard = infer(features) || heard;
  }
  return heard;
}

bool MicroWakeWord::infer(const int8_t* features) {
  TfLiteTensor* in = interpreter_->input(0);
  memcpy(in->data.int8 + FEATURES * step_, features, FEATURES);
  if (++step_ < stride_) return false;
  step_ = 0;
  if (interpreter_->Invoke() != kTfLiteOk) return false;
  next_ = static_cast<uint8_t>((next_ + 1) % window_);
  probs_[next_] = interpreter_->output(0)->data.uint8[0];
  if (probs_[next_] < cutoff_ && ignore_ < 0) ignore_ = static_cast<int16_t>(ignore_ + stride_);
  if (ignore_ < 0) return false;

  uint32_t sum = 0;
  lastMax_ = 0;
  for (int i = 0; i < window_; ++i) {
    sum += probs_[i];
    if (probs_[i] > lastMax_) lastMax_ = probs_[i];
  }
  lastMean_ = static_cast<uint8_t>(sum / window_);
  if (sum <= static_cast<uint32_t>(cutoff_) * window_) return false;
  reset();  // one detection, then a cool-off
  return true;
}
