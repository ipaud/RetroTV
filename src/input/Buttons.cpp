#include "input/Buttons.h"

#include <Arduino.h>

#include "board_config.h"

namespace {

struct KeyPin {
  int pin;
  FrontKey key;
};
constexpr KeyPin KEYS[] = {{PIN_KEY_CH_DOWN, FrontKey::ChDown},
                           {PIN_KEY_CH_UP, FrontKey::ChUp},
                           {PIN_KEY_VOL_DOWN, FrontKey::VolDown},
                           {PIN_KEY_VOL_UP, FrontKey::VolUp}};

}  // namespace

void Buttons::begin() {
  for (int i = 0; i < KEY_COUNT; ++i) {
    pinMode(KEYS[i].pin, INPUT_PULLUP);  // expansion pins have no external pull-up
    heldAtBoot_[i] = digitalRead(KEYS[i].pin) == LOW;
  }
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  bootHeldAtBoot_ = digitalRead(PIN_BOOT_BTN) == LOW;
}

InputEvent Buttons::poll(uint32_t nowMs) {
  // Every detector is updated each call so none of them loses time.
  InputEvent first = InputEvent::None;
  for (int i = 0; i < KEY_COUNT; ++i) {
    const bool down = digitalRead(KEYS[i].pin) == LOW;
    if (heldAtBoot_[i]) {  // releasing the key that switched the TV on is not an order
      heldAtBoot_[i] = down;
      continue;
    }
    const InputEvent e = frontKeyEvent(KEYS[i].key, keys_[i].update(down, nowMs));
    if (first == InputEvent::None) first = e;
  }
  const bool bootDown = digitalRead(PIN_BOOT_BTN) == LOW;
  if (bootHeldAtBoot_) bootHeldAtBoot_ = bootDown;
  const InputEvent boot =
      bootHeldAtBoot_ ? InputEvent::None : channelButtonEvent(boot_.update(bootDown, nowMs));
  // ponytail: one event per 10 ms loop; two keys finishing a press in the same loop drop one.
  return first != InputEvent::None ? first : boot;
}

bool Buttons::anyKeyDown() const {
  for (const KeyPin& k : KEYS) {
    if (digitalRead(k.pin) == LOW) return true;
  }
  return digitalRead(PIN_BOOT_BTN) == LOW;
}

uint64_t Buttons::wakeMask() {
  uint64_t mask = 1ull << PIN_BOOT_BTN;
  for (const KeyPin& k : KEYS) mask |= 1ull << k.pin;
  return mask;
}
