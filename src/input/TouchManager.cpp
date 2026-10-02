#include "input/TouchManager.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_config.h"
#include "config.h"

namespace {

constexpr uint8_t REG_TD_STATUS = 0x02;  // then P1_XH, P1_XL, P1_YH, P1_YL
constexpr size_t READ_LEN = 5;
constexpr uint8_t MAX_POINTS = 2;            // FT6336: more means a corrupt frame
constexpr uint8_t ERRORS_BEFORE_LOG = 10;    // rate-limits the log while the bus misbehaves

}  // namespace

void TouchManager::resetController() {
  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(TOUCH_RESET_PULSE_MS);
  digitalWrite(PIN_TOUCH_RST, HIGH);
}

void TouchManager::begin(bool present) {
  present_ = present;
  axes_ = axesForRotation(TouchAxes{TOUCH_SWAP_XY, TOUCH_INVERT_X, TOUCH_INVERT_Y}, PAUTV_ROTATION);
  PLOG("TOUCH", "%s", present_ ? "FT6336 ready, touch is the main control"
                               : "NOT FOUND, knobs and BOOT only");
}

InputEvent TouchManager::poll(uint32_t nowMs) {
  if (!present_ || nowMs - lastPollMs_ < TOUCH_POLL_MS) return InputEvent::None;
  lastPollMs_ = nowMs;

  bool down = false;
  int rawX = 0;
  int rawY = 0;
  if (!read(down, rawX, rawY)) {
    if (++readErrors_ == ERRORS_BEFORE_LOG) PLOG("TOUCH", "i2c reads failing");
    return InputEvent::None;
  }
  readErrors_ = 0;

  down_ = down;
  if (down) point_ = mapTouch(rawX, rawY, axes_, SCREEN_W, SCREEN_H);
  return gestureToEvent(gestures_.update(down, point_.x, point_.y, nowMs));
}

bool TouchManager::read(bool& down, int& rawX, int& rawY) {
  Wire.beginTransmission(I2C_ADDR_TOUCH);
  Wire.write(REG_TD_STATUS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint16_t>(I2C_ADDR_TOUCH), READ_LEN, true) != READ_LEN) {
    return false;
  }

  uint8_t b[READ_LEN];
  for (size_t i = 0; i < READ_LEN; ++i) b[i] = Wire.read();

  const uint8_t points = b[0] & 0x0F;
  down = points >= 1 && points <= MAX_POINTS;
  rawX = ((b[1] & 0x0F) << 8) | b[2];
  rawY = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}
