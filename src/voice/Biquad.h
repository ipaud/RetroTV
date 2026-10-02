#pragma once

// Second-order filters (RBJ audio EQ cookbook, Q = 0.707: Butterworth), pure C++.

#include <math.h>

class Biquad {
 public:
  static Biquad lowPass(float cutoffHz, float sampleRate) { return make(cutoffHz, sampleRate, false); }
  static Biquad highPass(float cutoffHz, float sampleRate) { return make(cutoffHz, sampleRate, true); }

  float step(float x) {
    const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
  }

 private:
  static Biquad make(float cutoffHz, float sampleRate, bool high) {
    const float w = 6.2831853f * cutoffHz / sampleRate;
    const float alpha = sinf(w) / (2.0f * 0.7071f);
    const float c = cosf(w);
    const float a0 = 1.0f + alpha;
    Biquad f;
    f.b0_ = (high ? (1.0f + c) : (1.0f - c)) / 2.0f / a0;
    f.b1_ = (high ? -(1.0f + c) : (1.0f - c)) / a0;
    f.b2_ = f.b0_;
    f.a1_ = -2.0f * c / a0;
    f.a2_ = (1.0f - alpha) / a0;
    return f;
  }

  float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
  float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};
