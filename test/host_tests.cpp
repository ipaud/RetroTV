// Host-side tests for the pure C++ logic (no Arduino, no board).
// Run: tools/run_host_tests.sh

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "audio/Synth.h"
#include "check.h"
#include "input/ButtonLogic.h"
#include "input/GestureLogic.h"
#include "media/JpegFrameSplitter.h"
#include "media/PlaybackClock.h"
#include "storage/SdLayout.h"
#include "ui/PowerOff.h"


// ---------------------------------------------------------------------------------------------
// Buttons: a 10 ms loop, like the firmware.

constexpr uint32_t STEP_MS = 10;

struct ButtonSim {
  ClickDetector det;
  uint32_t now;
  std::vector<ClickEvent> events;

  explicit ButtonSim(uint32_t startMs, ClickTiming timing = ClickTiming{}) : det(timing), now(startMs) {}

  // Holds the raw level for `ms`, polling every STEP_MS.
  void hold(bool pressed, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
      const ClickEvent e = det.update(pressed, now);
      if (e != ClickEvent::None) events.push_back(e);
      now += STEP_MS;
    }
  }
  bool only(ClickEvent e) const { return events.size() == 1 && events[0] == e; }
};

static void testSingleClick() {
  ButtonSim b(1000);
  b.hold(false, 100);
  b.hold(true, 100);
  b.hold(false, 300);  // still inside the double-click window
  CHECK(b.events.empty());
  b.hold(false, 200);
  CHECK(b.only(ClickEvent::Single));
}

static void testDoubleClick() {
  ButtonSim b(1000);
  b.hold(true, 80);
  b.hold(false, 100);
  b.hold(true, 80);
  b.hold(false, 600);
  CHECK(b.only(ClickEvent::Double));
}

static void testLongPress() {
  ButtonSim b(1000);
  b.hold(true, 990);
  CHECK(b.events.empty());
  b.hold(true, 500);
  CHECK(b.only(ClickEvent::Long));
  b.hold(false, 600);  // releasing after a long press is not a click
  CHECK(b.only(ClickEvent::Long));
}

static void testBounceIgnored() {
  ButtonSim b(1000);
  for (int i = 0; i < 3; ++i) {  // contact chatter shorter than the debounce time
    b.hold(true, 10);
    b.hold(false, 10);
  }
  b.hold(false, 600);
  CHECK(b.events.empty());

  ButtonSim c(1000);  // chatter, then a real press
  c.hold(true, 10);
  c.hold(false, 10);
  c.hold(true, 120);
  c.hold(false, 600);
  CHECK(c.only(ClickEvent::Single));
}

static void testShortGlitchIgnored() {
  ButtonSim b(1000);
  b.hold(true, 20);
  b.hold(false, 600);
  CHECK(b.events.empty());
}

static void testMillisOverflow() {
  ButtonSim b(0xFFFFFFFFu - 150);  // press straddles the 49.7-day wrap
  b.hold(true, 100);
  b.hold(false, 500);
  CHECK(b.only(ClickEvent::Single));

  ButtonSim c(0xFFFFFFFFu - 500);
  c.hold(true, 1200);
  c.hold(false, 100);
  CHECK(c.only(ClickEvent::Long));

  ButtonSim d(0xFFFFFFFFu - 200);  // double click across the wrap
  d.hold(true, 80);
  d.hold(false, 100);
  d.hold(true, 80);
  d.hold(false, 500);
  CHECK(d.only(ClickEvent::Double));
}

static void testClickThenHoldIsLong() {
  ButtonSim b(1000);
  b.hold(true, 80);
  b.hold(false, 100);
  b.hold(true, 1200);
  b.hold(false, 500);
  CHECK(b.only(ClickEvent::Long));
}

static void testButtonMapping() {
  CHECK(channelButtonEvent(ClickEvent::Single) == InputEvent::ChNext);
  CHECK(channelButtonEvent(ClickEvent::Double) == InputEvent::ChPrev);
  CHECK(channelButtonEvent(ClickEvent::Long) == InputEvent::Menu);
  CHECK(channelButtonEvent(ClickEvent::None) == InputEvent::None);
}

// The case's four keys: a press acts at once on release (no double-click wait), a hold acts
// once, while held.
static void testFrontKeys() {
  ButtonSim b(1000, frontKeyTiming(FrontKey::VolUp));
  b.hold(true, 80);
  b.hold(false, 40);
  CHECK(b.only(ClickEvent::Single));  // no 350 ms wait
  b.hold(true, 80);
  b.hold(false, 40);
  CHECK(b.events.size() == 2 && b.events[1] == ClickEvent::Single);  // two presses are two presses
  b.hold(true, BUTTON_LONG_PRESS_MS + 100);
  b.hold(false, 100);
  CHECK(b.events.size() == 3 && b.events[2] == ClickEvent::Long);  // nothing more on release

  CHECK(frontKeyEvent(FrontKey::ChDown, ClickEvent::Single) == InputEvent::ChPrev);
  CHECK(frontKeyEvent(FrontKey::ChUp, ClickEvent::Single) == InputEvent::ChNext);
  CHECK(frontKeyEvent(FrontKey::VolDown, ClickEvent::Single) == InputEvent::VolDown);
  CHECK(frontKeyEvent(FrontKey::VolUp, ClickEvent::Single) == InputEvent::VolUp);
  CHECK(frontKeyEvent(FrontKey::ChUp, ClickEvent::Long) == InputEvent::Menu);     // hold CH+: settings
  CHECK(frontKeyEvent(FrontKey::VolDown, ClickEvent::Long) == InputEvent::Mute);  // hold VOL-: mute
  CHECK(frontKeyEvent(FrontKey::ChDown, ClickEvent::Long) == InputEvent::Power);  // hold CH-: standby
  CHECK(frontKeyEvent(FrontKey::VolUp, ClickEvent::Long) == InputEvent::VolUp);
  CHECK(frontKeyEvent(FrontKey::VolUp, ClickEvent::None) == InputEvent::None);

  // CH- needs a longer hold (standby): 1.5 s is still a press.
  ButtonSim ch(1000, frontKeyTiming(FrontKey::ChDown));
  ch.hold(true, 1500);
  ch.hold(false, 50);
  CHECK(ch.only(ClickEvent::Single));
  ch.hold(true, POWER_HOLD_MS + 100);
  CHECK(ch.events.size() == 2 && ch.events[1] == ClickEvent::Long);  // while still held
}

// The CRT switch-off: squeeze to a line, shrink to a dot, fade; every stage only ever shrinks.
static void testPowerOffAnimation() {
  using namespace poweroff;
  const Frame start = frameAt(0);
  CHECK(start.bandHalf == FULL_HALF_H && start.lineHalfW == FULL_HALF_W && !start.done);
  int band = FULL_HALF_H, line = FULL_HALF_W, dot = 256;
  bool shrinking = true;
  for (uint32_t t = 0; t <= TOTAL_MS + 40; t += 10) {
    const Frame f = frameAt(t);
    shrinking = shrinking && f.bandHalf <= band && f.lineHalfW <= line && (f.lineHalfW > 0 || f.dot <= dot);
    band = f.bandHalf;
    line = f.lineHalfW;
    if (f.lineHalfW == 0) dot = f.dot;
  }
  CHECK(shrinking);
  CHECK(frameAt(COLLAPSE_MS).bandHalf == LINE_HALF_H && frameAt(COLLAPSE_MS).lineHalfW == FULL_HALF_W);
  CHECK(frameAt(COLLAPSE_MS + SHRINK_MS).lineHalfW == 0 && frameAt(COLLAPSE_MS + SHRINK_MS).dot == 255);
  CHECK(frameAt(TOTAL_MS).done && !frameAt(TOTAL_MS - 1).done);
}

// ---------------------------------------------------------------------------------------------
// Touch gestures: a 20 ms poll, like the firmware.

struct Stroke {
  GestureDetector det;
  uint32_t now;
  std::vector<Gesture> events;

  explicit Stroke(uint32_t startMs) : now(startMs) {}

  void touch(int x, int y, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
      const Gesture g = det.update(true, x, y, now);
      if (g != Gesture::None) events.push_back(g);
      now += 20;
    }
  }
  void release() {
    const Gesture g = det.update(false, 0, 0, now);
    if (g != Gesture::None) events.push_back(g);
    now += 20;
  }
  bool only(Gesture g) const { return events.size() == 1 && events[0] == g; }
};

static Gesture swipe(int x0, int y0, int x1, int y1) {
  Stroke s(5000);
  s.touch(x0, y0, 60);
  s.touch((x0 + x1) / 2, (y0 + y1) / 2, 60);
  s.touch(x1, y1, 60);
  s.release();
  return s.events.size() == 1 ? s.events[0] : Gesture::None;
}

static void testTap() {
  Stroke s(5000);
  s.touch(100, 100, 100);
  s.touch(105, 103, 100);  // a finger always wobbles a little
  s.release();
  CHECK(s.only(Gesture::Tap));
}

static void testSwipes() {
  CHECK(swipe(50, 120, 150, 125) == Gesture::SwipeRight);
  CHECK(swipe(250, 120, 150, 110) == Gesture::SwipeLeft);
  CHECK(swipe(160, 200, 165, 100) == Gesture::SwipeUp);  // screen y grows downwards
  CHECK(swipe(160, 40, 150, 140) == Gesture::SwipeDown);
  CHECK(swipe(100, 100, 160, 150) == Gesture::SwipeRight);  // dominant axis wins
  CHECK(swipe(100, 100, 125, 100) == Gesture::None);        // too short for a swipe, too long for a tap
}

static void testLongTouch() {
  Stroke s(5000);
  s.touch(160, 120, 780);
  CHECK(s.events.empty());
  s.touch(162, 121, 200);  // fires while still held
  CHECK(s.only(Gesture::LongPress));
  s.touch(162, 121, 500);
  s.release();
  CHECK(s.only(Gesture::LongPress));  // once, and nothing on release
}

static void testMovedHoldIsNotLong() {
  Stroke s(5000);
  s.touch(100, 120, 100);
  s.touch(170, 120, 1200);
  s.release();
  CHECK(s.only(Gesture::SwipeRight));
}

static void testGestureOverflow() {
  Stroke s(0xFFFFFFFFu - 300);
  s.touch(160, 120, 900);
  s.release();
  CHECK(s.only(Gesture::LongPress));

  Stroke t(0xFFFFFFFFu - 50);
  t.touch(100, 100, 120);
  t.release();
  CHECK(t.only(Gesture::Tap));
}

static void testReleaseWithoutTouch() {
  GestureDetector d;
  CHECK(d.update(false, 0, 0, 100) == Gesture::None);
}

static void testGestureMapping() {
  CHECK(gestureToEvent(Gesture::Tap) == InputEvent::ToggleOsd);
  CHECK(gestureToEvent(Gesture::SwipeLeft) == InputEvent::ChPrev);
  CHECK(gestureToEvent(Gesture::SwipeRight) == InputEvent::ChNext);
  CHECK(gestureToEvent(Gesture::SwipeUp) == InputEvent::VolUp);
  CHECK(gestureToEvent(Gesture::SwipeDown) == InputEvent::VolDown);
  CHECK(gestureToEvent(Gesture::LongPress) == InputEvent::Menu);
  CHECK(gestureToEvent(Gesture::None) == InputEvent::None);
}

// Controller reports portrait 240x320; the screen is landscape 320x240.
static void testTouchMapping() {
  const TouchAxes rot1{true, false, true};  // Freenove Sketch 12.1: x = raw.y, y = 240 - raw.x
  TouchPoint p = mapTouch(0, 0, rot1, 320, 240);
  CHECK(p.x == 0 && p.y == 239);
  p = mapTouch(239, 319, rot1, 320, 240);
  CHECK(p.x == 319 && p.y == 0);
  p = mapTouch(100, 200, rot1, 320, 240);
  CHECK(p.x == 200 && p.y == 139);

  const TouchAxes rot3 = axesForRotation(rot1, 3);  // upside down: flip both axes
  p = mapTouch(100, 200, rot3, 320, 240);
  CHECK(p.x == 119 && p.y == 100);
  CHECK(axesForRotation(rot1, 1).invertY == rot1.invertY);

  p = mapTouch(-5, 400, rot1, 320, 240);  // garbage from the controller stays on screen
  CHECK(p.x == 319 && p.y == 239);
}

// ---------------------------------------------------------------------------------------------
// Audio: volume curve and synthesizer.

static void testVolumeCurve() {
  CHECK(codecVolumeFor(0) == 0);                  // user 0 = silent
  CHECK(codecVolumeFor(1) == CODEC_VOLUME_MIN);
  CHECK(codecVolumeFor(100) == CODEC_VOLUME_MAX);
  CHECK(codecVolumeFor(250) == CODEC_VOLUME_MAX);  // out of range clamps
  bool monotonic = true;
  for (int v = 2; v <= 100; ++v) monotonic &= codecVolumeFor(v) >= codecVolumeFor(v - 1);
  CHECK(monotonic);
  CHECK(codecVolumeFor(50) > CODEC_VOLUME_MIN && codecVolumeFor(50) < CODEC_VOLUME_MAX);
}

static std::vector<int16_t> renderAll(Synth& synth) {
  std::vector<int16_t> out;
  int16_t block[256];
  size_t n;
  while ((n = synth.render(block, 256)) > 0) out.insert(out.end(), block, block + n);
  return out;
}

static void testToneFrequencyAndLength() {
  Synth synth;
  synth.start(SoundRequest{SoundKind::Tone, 440, 1000, 50}, 44100);
  const std::vector<int16_t> s = renderAll(synth);
  CHECK(s.size() == 44100);

  int crossings = 0;
  for (size_t i = 1; i < s.size(); ++i) crossings += (s[i - 1] < 0) != (s[i] < 0);
  CHECK(std::abs(crossings - 880) <= 4);  // 440 Hz = 880 sign changes per second

  int peak = 0;
  for (int16_t v : s) peak = std::max(peak, std::abs(static_cast<int>(v)));
  CHECK(peak <= 32767 / 2 + 1);  // 50 % level
  CHECK(peak >= 32767 / 2 * 95 / 100);
  CHECK(!synth.active());
  int16_t tail[8];
  CHECK(synth.render(tail, 8) == 0);
}

static void testFadeAvoidsClicks() {
  Synth synth;
  synth.start(SoundRequest{SoundKind::Tone, 1000, 100, 100}, 44100);
  const std::vector<int16_t> s = renderAll(synth);
  CHECK(std::abs(static_cast<int>(s.front())) < 500);
  CHECK(std::abs(static_cast<int>(s.back())) < 500);
}

static void testNoise() {
  Synth synth;
  synth.start(SoundRequest{SoundKind::Noise, 0, 200, 20}, 44100);
  const std::vector<int16_t> s = renderAll(synth);
  CHECK(s.size() == 8820);
  long long sum = 0;
  int peak = 0;
  for (int16_t v : s) {
    sum += v;
    peak = std::max(peak, std::abs(static_cast<int>(v)));
  }
  CHECK(peak <= 32767 * 20 / 100 + 1);  // low-level static stays low
  CHECK(peak > 32767 * 20 / 100 / 2);
  CHECK(std::llabs(sum / static_cast<long long>(s.size())) < 32767 * 20 / 100 / 20);
}

// ---------------------------------------------------------------------------------------------
// Media: MJPEG frame splitting, playback clock, frame pacing.

// A fake JPEG: SOI, payload (with byte stuffing FF 00 and a restart marker), EOI.
static std::vector<uint8_t> fakeJpeg(uint8_t tag, size_t payload) {
  std::vector<uint8_t> j = {0xFF, 0xD8};
  for (size_t i = 0; i < payload; ++i) j.push_back(static_cast<uint8_t>(tag + i));
  j.insert(j.end(), {0xFF, 0x00, 0xFF, 0xD0, tag});
  j.insert(j.end(), {0xFF, 0xD9});
  return j;
}

struct MemReader {
  const std::vector<uint8_t>& data;
  size_t pos = 0;
  size_t chunk;
  size_t operator()(uint8_t* dst, size_t max) {
    const size_t n = std::min({max, chunk, data.size() - pos});
    std::copy(data.begin() + pos, data.begin() + pos + n, dst);
    pos += n;
    return n;
  }
};

static std::vector<std::vector<uint8_t>> splitAll(const std::vector<uint8_t>& stream, size_t chunk,
                                                  size_t frameCap, uint32_t* overflows = nullptr) {
  std::vector<uint8_t> frame(frameCap);
  std::vector<uint8_t> scratch(64);
  JpegFrameSplitter splitter;
  splitter.attach(frame.data(), frame.size(), scratch.data(), scratch.size());
  MemReader reader{stream, 0, chunk};
  std::vector<std::vector<uint8_t>> frames;
  size_t len = 0;
  while (splitter.next(reader, len) == JpegFrameSplitter::Result::Frame) {
    frames.emplace_back(frame.begin(), frame.begin() + len);
  }
  if (overflows) *overflows = splitter.overflows();
  return frames;
}

static void testSplitterFindsFrames() {
  const auto a = fakeJpeg(0x10, 40), b = fakeJpeg(0x40, 5), c = fakeJpeg(0x70, 90);
  std::vector<uint8_t> stream = {0x00, 0x12, 0xFF, 0x34};  // junk before the first frame
  for (const auto* f : {&a, &b, &c}) stream.insert(stream.end(), f->begin(), f->end());
  for (size_t chunk : {1u, 2u, 7u, 64u}) {  // markers straddle chunk borders
    const auto frames = splitAll(stream, chunk, 256);
    CHECK(frames.size() == 3);
    if (frames.size() == 3) CHECK(frames[0] == a && frames[1] == b && frames[2] == c);
  }
}

static void testSplitterOverflowSkipsFrame() {
  const auto small = fakeJpeg(0x10, 20), big = fakeJpeg(0x20, 300), after = fakeJpeg(0x30, 20);
  std::vector<uint8_t> stream;
  for (const auto* f : {&small, &big, &after}) stream.insert(stream.end(), f->begin(), f->end());
  uint32_t overflows = 0;
  const auto frames = splitAll(stream, 16, 128, &overflows);  // big does not fit in 128 bytes
  CHECK(frames.size() == 2);
  if (frames.size() == 2) CHECK(frames[0] == small && frames[1] == after);
  CHECK(overflows == 1);

  const auto exact = fakeJpeg(0x50, 128 - 9);  // exactly fills the buffer: still a frame
  CHECK(exact.size() == 128);
  CHECK(splitAll(exact, 16, 128).size() == 1);
}

static void testSplitterTruncatedTail() {
  auto stream = fakeJpeg(0x10, 30);
  const auto partial = fakeJpeg(0x20, 30);
  stream.insert(stream.end(), partial.begin(), partial.end() - 2);  // file cut before EOI
  CHECK(splitAll(stream, 8, 256).size() == 1);
}

static void testClockWithoutAudio() {
  PlaybackClock clock;
  clock.start(1000, false, 44100, 2048);
  CHECK(clock.positionMs(1000) == 0);
  CHECK(clock.positionMs(1500) == 500);
  PlaybackClock wrap;
  wrap.start(0xFFFFFF00u, false, 44100, 2048);
  CHECK(wrap.positionMs(0x00000100u) == 512);  // millis() wrapped
}

static void testClockFollowsAudio() {
  PlaybackClock clock;
  clock.start(1000, true, 44100, 2048);
  clock.onAudioWritten(1000);   // still inside the DMA queue: nothing heard yet
  CHECK(clock.positionMs(99999) == 0);
  clock.onAudioWritten(44100 + 2048 - 1000);
  CHECK(clock.positionMs(99999) == 1000);  // audio is the master, wall time is ignored
  for (int i = 0; i < 600; ++i) clock.onAudioWritten(44100);  // 10 more minutes: no overflow
  CHECK(clock.positionMs(0) == 601000);
}

static void testClockAfterAudioEnds() {
  PlaybackClock clock;
  clock.start(0, true, 44100, 2048);
  clock.onAudioWritten(2048 + 44100 * 2);  // 2 s heard
  clock.onAudioEnded(5000);
  CHECK(clock.positionMs(5000) == 2000);
  CHECK(clock.positionMs(5250) == 2250);  // wall clock continues from the audio position
}

static void testFramePacing() {
  constexpr uint32_t frameMs = 1000 / 24;
  CHECK(decideFrame(0, 0, frameMs) == FrameAction::Show);
  CHECK(decideFrame(10, 41, frameMs) == FrameAction::Wait);   // early: wait
  CHECK(decideFrame(60, 41, frameMs) == FrameAction::Show);   // a bit late: still show
  CHECK(decideFrame(41 + frameMs + 1, 41, frameMs) == FrameAction::Drop);  // over a frame late
}

// ---------------------------------------------------------------------------------------------
// SD layout helpers.

static void testEpisodeFileFilter() {
  CHECK(isEpisodeFile("capitulo_01.mjpeg"));
  CHECK(isEpisodeFile("CAPITULO.MJPEG"));    // FAT is case-insensitive
  CHECK(!isEpisodeFile("._capitulo_01.mjpeg"));  // macOS AppleDouble junk
  CHECK(!isEpisodeFile(".hidden.mjpeg"));
  CHECK(!isEpisodeFile("capitulo_01.aac"));
  CHECK(!isEpisodeFile("mjpeg"));
  CHECK(!isEpisodeFile(".mjpeg"));
  CHECK(!isEpisodeFile(""));
}

static void testAudioPathForVideo() {
  char out[64];
  CHECK(audioPathFor("/retrotv/media/demo/demo.mjpeg", out, sizeof(out)));
  CHECK(std::string(out) == "/retrotv/media/demo/demo.aac");
  CHECK(audioPathFor("/X/EP.MJPEG", out, sizeof(out)) && std::string(out) == "/X/EP.aac");
  char tiny[8];
  CHECK(!audioPathFor("/retrotv/media/demo/demo.mjpeg", tiny, sizeof(tiny)));  // never overflows
  CHECK(!audioPathFor("/retrotv/notes.txt", out, sizeof(out)));
}

int main() {
  testSingleClick();
  testDoubleClick();
  testLongPress();
  testBounceIgnored();
  testShortGlitchIgnored();
  testMillisOverflow();
  testClickThenHoldIsLong();
  testButtonMapping();
  testFrontKeys();
  testPowerOffAnimation();

  testTap();
  testSwipes();
  testLongTouch();
  testMovedHoldIsNotLong();
  testGestureOverflow();
  testReleaseWithoutTouch();
  testGestureMapping();
  testTouchMapping();

  testVolumeCurve();
  testToneFrequencyAndLength();
  testFadeAvoidsClicks();
  testNoise();

  testSplitterFindsFrames();
  testSplitterOverflowSkipsFrame();
  testSplitterTruncatedTail();
  testClockWithoutAudio();
  testClockFollowsAudio();
  testClockAfterAudioEnds();
  testFramePacing();

  testEpisodeFileFilter();
  testAudioPathForVideo();

  runChannelTests();
  runOverlayTests();
  runOnAirTests();
  runTeletextTests();
  runRemoteTests();
  runWifiTests();
  runWebTests();
  runConfigTests();
  runBatteryTests();
  runVoiceTests();

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
