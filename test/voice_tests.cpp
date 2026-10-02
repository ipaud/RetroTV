// RETROTV Voice, pure parts (docs/VOICE.md): microphone levels. Run: tools/run_host_tests.sh

#include <math.h>

#include <string>
#include <vector>

#include "check.h"
#include "voice/ClapDetector.h"
#include "voice/MicMeter.h"
#include "voice/Recorder.h"
#include "voice/VoiceStandby.h"

static std::vector<int16_t> sine(float amplitude, float hz, size_t n, float rate = 44100.0f) {
  std::vector<int16_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(lroundf(amplitude * 32767.0f * sinf(6.2831853f * hz * i / rate)));
  return v;
}

static void testDb() {
  CHECK(micDb10(1.0f) == 0);
  CHECK(micDb10(0.5f) == -60);  // -6.0 dB
  CHECK(micDb10(0.0f) == MIC_FLOOR_DB10);
  CHECK(micDb10(1e-6f) == MIC_FLOOR_DB10);  // -120 dB shows as the floor
  CHECK(micDb10(2.0f) == 0);                // never above full scale
  CHECK(micMeterPct(MIC_METER_MIN_DB10) == 0 && micMeterPct(MIC_FLOOR_DB10) == 0);
  CHECK(micMeterPct(0) == 100);
  CHECK(micMeterPct(-300) == 50);
}

static void testSilence() {
  MicMeter m;
  const std::vector<int16_t> zeros(512, 0);
  const MicLevels l = m.block(zeros.data(), zeros.size());
  CHECK(l.rmsDb10 == MIC_FLOOR_DB10 && l.peakDb10 == MIC_FLOOR_DB10 && l.clipped == 0);
  CHECK(m.block(zeros.data(), 0).rmsDb10 == MIC_FLOOR_DB10);  // an empty read
}

static void testDcOffsetIgnored() {
  // The codec's DC offset is not level: after the high-pass settles, a constant reads as silence.
  MicMeter m;
  const std::vector<int16_t> dc(4410, 1000);
  m.block(dc.data(), dc.size());
  const MicLevels l = m.block(dc.data(), dc.size());
  CHECK(l.rmsDb10 < -700);
}

static void testSineLevels() {
  MicMeter m;
  const std::vector<int16_t> s = sine(0.5f, 1000.0f, 4410);
  m.block(s.data(), s.size());  // settle the filter
  const MicLevels l = m.block(s.data(), s.size());
  CHECK(l.peakDb10 >= -65 && l.peakDb10 <= -55);  // -6 dB peak
  CHECK(l.rmsDb10 >= -95 && l.rmsDb10 <= -85);    // -9 dB rms (sine: peak - 3 dB)
  CHECK(l.clipped == 0);
}

static void testClipping() {
  MicMeter m;
  std::vector<int16_t> s(100, 0);
  s[10] = 32767;
  s[20] = -32768;
  s[30] = 32000;  // loud, not clipped
  CHECK(m.block(s.data(), s.size()).clipped == 2);
}


// --- Clap detector -----------------------------------------------------------------------------
// A signal is built at 44.1 kHz and fed in 512-frame blocks, like the capture task does.

namespace {

constexpr uint32_t RATE = 44100;

float dbAmp(float db) { return 32767.0f * powf(10.0f, db / 20.0f); }

struct Signal {
  std::vector<float> s;
  uint32_t seed = 12345;
  float noise() {  // uniform -1..1, deterministic
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<float>(seed & 0xFFFF) / 32768.0f - 1.0f;
  }
  size_t at(uint32_t ms) const { return static_cast<size_t>(static_cast<uint64_t>(ms) * RATE / 1000); }
  void length(uint32_t ms) { s.assign(at(ms), 0.0f); }
  // Steady white noise at about `db` dBFS rms (uniform noise: rms = amplitude / sqrt 3).
  void background(float db) {
    const float a = dbAmp(db) * 1.732f;
    for (float& v : s) v += a * noise();
  }
  // A clap: a noise burst whose envelope dies with a 6 ms time constant, peak at `db` dBFS.
  void clap(uint32_t ms, float db, uint32_t tauMs = 6) {
    const float a = dbAmp(db);
    const size_t start = at(ms);
    const size_t len = at(tauMs * 10);
    for (size_t i = 0; i < len && start + i < s.size(); ++i) {
      s[start + i] += a * noise() * expf(-static_cast<float>(i) / (RATE * tauMs / 1000.0f));
    }
  }
  // A knock on wood or a door: the same burst through a 300 Hz low-pass (dull, little above 2 kHz).
  void knock(uint32_t ms, float db) {
    Biquad lp1 = Biquad::lowPass(300.0f, static_cast<float>(RATE));
    Biquad lp2 = Biquad::lowPass(300.0f, static_cast<float>(RATE));
    const float a = dbAmp(db) * 4.0f;  // the filter takes most of the noise's energy
    const size_t start = at(ms);
    const size_t len = at(60);
    for (size_t i = 0; i < len && start + i < s.size(); ++i) {
      s[start + i] += lp2.step(lp1.step(a * noise())) * expf(-static_cast<float>(i) / (RATE * 0.008f));
    }
  }
  // A short beep (an alarm, a phone), `lengthMs` long, at hz.
  void beep(uint32_t ms, uint32_t lengthMs, float hz, float db) {
    const float a = dbAmp(db);
    for (size_t i = at(ms); i < at(ms + lengthMs) && i < s.size(); ++i) {
      s[i] += a * sinf(6.2831853f * hz * static_cast<float>(i) / RATE);
    }
  }
  // A loud steady sound (speech, music, a hoover), `ms` long.
  void sustained(uint32_t ms, uint32_t lengthMs, float db) {
    const float a = dbAmp(db) * 1.732f;
    for (size_t i = at(ms); i < at(ms + lengthMs) && i < s.size(); ++i) s[i] += a * noise();
  }
};

struct Run {
  int claps = 0;
  int sequences[4] = {};
  int lastSequence = 0;
};

// Feeds the whole signal; millis of the first sample = startMs.
Run feed(ClapDetector& d, const Signal& sig, uint32_t startMs = 1000) {
  Run r;
  std::vector<int16_t> block(512);
  for (size_t done = 0; done < sig.s.size(); done += block.size()) {
    const size_t n = sig.s.size() - done < block.size() ? sig.s.size() - done : block.size();
    for (size_t i = 0; i < n; ++i) {
      const float v = sig.s[done + i];
      block[i] = static_cast<int16_t>(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : v));
    }
    const uint32_t endMs = startMs + static_cast<uint32_t>(static_cast<uint64_t>(done + n - 1) * 1000 / RATE);
    d.feed(block.data(), n, endMs);
    if (d.takeClap()) ++r.claps;
    if (const uint8_t q = d.takeSequence()) {
      ++r.sequences[q];
      r.lastSequence = q;
    }
  }
  return r;
}

}  // namespace

static void testClapSilenceAndSteadyNoise() {
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  Run r = feed(d, sig);
  CHECK(r.claps == 0 && r.lastSequence == 0);

  ClapDetector d2;
  Signal noisy;
  noisy.length(3000);
  noisy.background(-45.0f);
  r = feed(d2, noisy);
  CHECK(r.claps == 0 && r.lastSequence == 0 && d2.stats().sustained == 0);
  CHECK(d2.floorDb10() > -500 && d2.floorDb10() < -400);  // the floor found the -45 dB room
}

static void testClapSingle() {
  ClapDetector d;
  Signal sig;
  sig.length(2500);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 1);
  CHECK(r.sequences[1] == 1 && r.sequences[2] == 0);
  CHECK(d.stats().claps == 1 && d.lastPeakDb10() > -250);
}

static void testClapLongSoundIsNot() {
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  sig.background(-60.0f);
  sig.sustained(1000, 400, -15.0f);  // loud, sudden, but 400 ms long
  const Run r = feed(d, sig);
  CHECK(r.claps == 0 && r.lastSequence == 0);
  CHECK(d.stats().sustained == 1);
}

static void testClapTooFastIsNotADouble() {
  // A speaker pop and its echo 160 ms later: no pair of hands claps that fast.
  ClapDetector d;
  Signal sig;
  sig.length(2500);
  sig.background(-60.0f);
  sig.clap(1000, -15.0f);
  sig.clap(1160, -20.0f);
  const Run r = feed(d, sig);
  CHECK(r.sequences[2] == 0 && r.sequences[1] == 1);
}

static void testClapEchoIgnored() {
  ClapDetector d;
  Signal sig;
  sig.length(2500);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  sig.clap(1050, -18.0f);  // the wall answers 50 ms later
  const Run r = feed(d, sig);
  CHECK(r.claps == 1 && r.sequences[1] == 1 && r.sequences[2] == 0);
}

static void testClapDoubleAndTriple() {
  ClapDetector d;
  Signal two;
  two.length(2500);
  two.background(-60.0f);
  two.clap(1000, -12.0f);
  two.clap(1300, -14.0f);
  Run r = feed(d, two);
  CHECK(r.claps == 2 && r.sequences[2] == 1 && r.sequences[1] == 0 && r.lastSequence == 2);
  CHECK(d.lastGapMs() >= 290 && d.lastGapMs() <= 310);

  ClapDetector d3;
  Signal three;
  three.length(3000);
  three.background(-60.0f);
  three.clap(1000, -12.0f);
  three.clap(1250, -13.0f);
  three.clap(1500, -12.0f);
  r = feed(d3, three);
  CHECK(r.claps == 3 && r.sequences[3] == 1 && r.sequences[2] == 0 && r.sequences[1] == 0);
}

static void testClapDoubleWaitsForTheWindow() {
  // Two claps are not reported before CLAP_GAP_MAX_MS has passed: a third could still come.
  ClapDetector d;
  Signal sig;
  sig.length(1300 + CLAP_GAP_MAX_MS / 2);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  sig.clap(1300, -12.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 2 && r.lastSequence == 0);
}

static void testClapSecondClapNotLost() {
  // A clap's tail must not lift the floor so much that an equal clap right after goes unheard.
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  sig.background(-55.0f);
  sig.clap(1000, -25.0f, 15);  // a softer clap with a longer room tail
  sig.clap(1200, -25.0f, 15);
  const Run r = feed(d, sig);
  CHECK(r.claps == 2 && r.sequences[2] == 1);
  CHECK(d.floorDb10() < -500);  // still near the room's -55 dB
}

static void testClapSlowSequenceIsTwoSingles() {
  ClapDetector d;
  Signal sig;
  sig.length(3500);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  sig.clap(1000 + CLAP_GAP_MAX_MS + 200, -12.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 2 && r.sequences[1] == 2 && r.sequences[2] == 0);
}

static void testClapNoiseFloorFollowsTheRoom() {
  // In a loud room (-35 dB) a clap barely over the noise is not one; a real one still is.
  ClapDetector d;
  Signal sig;
  sig.length(6000);
  sig.background(-35.0f);
  sig.clap(3000, -30.0f);  // +5 dB: lost in the room
  sig.clap(4500, -8.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 1 && r.sequences[1] == 1);
}

static void testClapSensitivity() {
  // Floor -50 dB, clap -28 dB: sensitivity 100 (threshold -40) hears it, 0 (threshold -20) does not.
  for (int s : {100, 0}) {
    ClapDetector d;
    d.setSensitivity(static_cast<uint8_t>(s));
    Signal sig;
    sig.length(4000);
    sig.background(-50.0f);
    sig.clap(2500, -28.0f);
    const Run r = feed(d, sig);
    CHECK(r.claps == (s == 100 ? 1 : 0));
  }
  ClapDetector d;
  d.setSensitivity(250);  // clamped
  CHECK(d.sensitivity() == 100);
}

static void testClapExtraMarginAndOwnSounds() {
  // While the TV plays sound the threshold rises; its own static is not a clap.
  ClapDetector loud;
  loud.setSensitivity(100);
  loud.setExtraMarginDb10(250);
  Signal sig;
  sig.length(4000);
  sig.background(-50.0f);
  sig.clap(2500, -28.0f);
  CHECK(feed(loud, sig).claps == 0);

  ClapDetector d;
  d.suppressUntil(1000 + 2000);  // the first clap falls inside the TV's own sound
  Signal own;
  own.length(5000);
  own.background(-60.0f);
  own.clap(1500, -12.0f);
  own.clap(3500, -12.0f);
  const Run r = feed(d, own);
  CHECK(r.claps == 1 && d.stats().suppressed == 1 && r.sequences[1] == 1);
}

static void testPlaybackEnvelope() {
  PlaybackEnvelope p;
  CHECK(!p.riseBetween(0, 10000, 80, -350));  // nothing played yet
  uint32_t t = 1000;
  for (int i = 0; i < 20; ++i, t += 6) p.push(t, -400);  // a quiet scene
  CHECK(!p.riseBetween(1000, t, 80, -350));
  p.push(t, -150);  // a punch: +25 dB in one block
  const uint32_t bang = t;
  for (int i = 0; i < 10; ++i) p.push(t += 6, -300);
  CHECK(p.riseBetween(bang - 50, bang + 20, 80, -350));
  CHECK(!p.riseBetween(bang + 6, bang + 200, 80, -350));  // after it: only a slow fall
  CHECK(!p.riseBetween(bang - 50, bang + 20, 300, -350));  // not a 30 dB jump
  for (int i = 0; i < 300; ++i) p.push(t += 6, -400);  // the ring wraps; old entries are not read
  CHECK(!p.riseBetween(bang - 50, bang + 20, 80, -350));
}

static void testHighPass() {
  // Bass is what the small speaker cannot play: 100 Hz loses >10 dB, 2 kHz passes within 1 dB.
  auto gainDb = [](float hz) {
    HighPass hp(400.0f, 44100.0f);
    float in = 0.0f, out = 0.0f;
    for (int i = 0; i < 44100; ++i) {
      const float x = sinf(6.2831853f * hz * i / 44100.0f);
      const float y = hp.step(x);
      if (i > 4410) {  // after it settles
        in += x * x;
        out += y * y;
      }
    }
    return 10.0f * log10f(out / in);
  };
  CHECK(gainDb(100.0f) < -10.0f);
  CHECK(gainDb(2000.0f) > -1.0f);
  PlaybackEnvelope p;
  for (uint32_t t = 0; t < 60; t += 6) p.push(t, t == 30 ? -100 : -400);
  CHECK(p.maxRise(0, 60, -350) == 300 && p.maxRise(0, 20, -350) == 0);
}

static void testClapThatIsTheProgramme() {
  // The speaker went bang right when the mic heard a "clap": the TV hearing itself.
  Signal sig;
  sig.length(2500);
  sig.background(-60.0f);
  sig.clap(1000, -15.0f);
  PlaybackEnvelope tv;
  for (uint32_t t = 900; t < 1200; t += 6) tv.push(t, t >= 1000 && t < 1012 ? -120 : -400);
  ClapDetector heard;
  heard.setPlayback(&tv);
  Run r = feed(heard, sig, 0);
  CHECK(r.claps == 0 && heard.stats().fromTv == 1);

  // The same clap with the programme quiet (or its bang a second away) is a real one.
  PlaybackEnvelope quiet;
  for (uint32_t t = 900; t < 1200; t += 6) quiet.push(t, t >= 2000 && t < 2012 ? -120 : -400);
  ClapDetector real;
  real.setPlayback(&quiet);
  r = feed(real, sig, 0);
  CHECK(r.claps == 1 && real.stats().fromTv == 0);
}

static void testStandbyClaps() {
  constexpr uint32_t QUIET = STANDBY_QUIET_BEFORE_MS + 500;
  VoiceStandby s;
  CHECK(s.onClap(0) == StandbyAction::None);   // nothing heard
  CHECK(s.onClap(1) == StandbyAction::Blink && s.onClap(2) == StandbyAction::Blink);  // the LED counts
  s.onSequence(1, QUIET, 10, 1000);
  CHECK(!s.pending() && s.update(10, 5000) == StandbyAction::None);  // one clap: stays off
  s.onSequence(3, QUIET, 10, 1000);
  CHECK(!s.pending());                                               // three: not the sign
  s.onSequence(2, 1600, 10, 1000);
  CHECK(!s.pending());                                               // a bang 1.6 s before (the user's case)
  s.onSequence(2, QUIET, 10, 1000);
  CHECK(s.pending() && s.update(10, 1000 + STANDBY_QUIET_AFTER_MS - 1) == StandbyAction::None);
  CHECK(s.update(11, 1000 + STANDBY_QUIET_AFTER_MS) == StandbyAction::None && !s.pending());  // a bang after
  s.onSequence(2, UINT32_MAX, 20, 2000);                             // nothing ever heard before: fine
  CHECK(s.update(20, 2000 + STANDBY_QUIET_AFTER_MS) == StandbyAction::Wake && s.waking());
  CHECK(s.onClap(1) == StandbyAction::None);                         // already waking
  s.cancelWake();                                                    // e.g. a flat battery: keep listening
  s.onSequence(2, QUIET, 30, 3000);
  CHECK(!s.waking() && s.update(30, 3000 + STANDBY_QUIET_AFTER_MS) == StandbyAction::Wake);
}

static void testQuietBeforeAndBangs() {
  // A dull bang 1 s before a double: the quiet before is ~1 s, and it counts as a bang.
  ClapDetector d;
  Signal c;
  c.length(5000);
  c.background(-60.0f);
  c.knock(1000, -10.0f);
  c.clap(2000, -12.0f);
  c.clap(2300, -12.0f);
  const Run r = feed(d, c);
  CHECK(r.sequences[2] == 1 && d.lastQuietBeforeMs() >= 900 && d.lastQuietBeforeMs() <= 1100);
  CHECK(d.bangs() >= 3);

  ClapDetector q;  // the same double in a quiet room: nothing heard before
  Signal e;
  e.length(5000);
  e.background(-60.0f);
  e.clap(2000, -12.0f);
  e.clap(2300, -12.0f);
  feed(q, e);
  CHECK(q.lastQuietBeforeMs() == UINT32_MAX);
}

static void testKnockIsNotAClap() {
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  sig.background(-60.0f);
  sig.knock(1000, -15.0f);
  sig.knock(1300, -15.0f);  // two knocks in a clapping rhythm
  const Run r = feed(d, sig);
  CHECK(r.claps == 0 && r.lastSequence == 0 && d.stats().dull == 2);
  CHECK(d.lastHfShare() < CLAP_HF_MIN_SHARE);
}

static void testUnevenPairIsNotADouble() {
  // A loud clap and a faint tick 300 ms later: not two hands.
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  sig.background(-60.0f);
  sig.clap(1000, -10.0f);
  sig.clap(1300, -30.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 2 && r.lastSequence == 0 && d.stats().badSequences == 1);
  CHECK(std::string(d.lastRejected()) == "loudness");
}

static void testSlowPairIsNotADouble() {
  // 650 ms apart: still one sequence (window 700 ms), but too slow for a double clap.
  ClapDetector d;
  Signal sig;
  sig.length(3000);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  sig.clap(1650, -12.0f);
  const Run r = feed(d, sig);
  CHECK(r.claps == 2 && r.lastSequence == 0 && std::string(d.lastRejected()) == "rhythm");
}

static void testAlarmIsNotATriple() {
  // An alarm: five short 3 kHz beeps 300 ms apart. Bright and even, but four or more is nothing.
  ClapDetector d;
  Signal sig;
  sig.length(4000);
  sig.background(-60.0f);
  for (int i = 0; i < 5; ++i) sig.beep(1000 + 300 * i, 30, 3000.0f, -15.0f);
  const Run r = feed(d, sig);
  CHECK(r.lastSequence == 0 && r.sequences[3] == 0 && r.sequences[2] == 0);
  CHECK(std::string(d.lastRejected()) == "too many");
}

static void testStandbyNeedsALouderClap() {
  // STANDBY VOZ: a clap far away (-40 dBFS) does not count; a near one (-15) does.
  for (float db : {-40.0f, -15.0f}) {
    ClapDetector d;
    d.setMinDb10(CLAP_STANDBY_MIN_DB10);
    Signal sig;
    sig.length(2500);
    sig.background(-65.0f);
    sig.clap(1000, db);
    CHECK(feed(d, sig).claps == (db > -30.0f ? 1 : 0));
  }
}

static void testClapIndexAsItHappens() {
  // Each clap is reported as it is accepted, with its place in the sequence.
  ClapDetector d;
  Signal sig;
  sig.length(2000);
  sig.background(-60.0f);
  sig.clap(500, -12.0f);
  sig.clap(800, -12.0f);
  std::vector<int16_t> block(512);
  std::vector<int> indices;
  for (size_t done = 0; done < sig.s.size(); done += block.size()) {
    const size_t n = sig.s.size() - done < block.size() ? sig.s.size() - done : block.size();
    for (size_t i = 0; i < n; ++i) block[i] = static_cast<int16_t>(sig.s[done + i]);
    d.feed(block.data(), n, 1000 + static_cast<uint32_t>((done + n - 1) * 1000 / RATE));
    if (const uint8_t c = d.takeClap()) indices.push_back(c);
  }
  CHECK(indices.size() == 2 && indices[0] == 1 && indices[1] == 2);
}

static float sineGainDb(float hz) {
  // 0.5 s of a sine at 44.1 kHz through the downsampler: output level against input level.
  Downsampler ds(44100.0f, 16000.0f, REC_LOWPASS_HZ);
  std::vector<int16_t> in(22050), out(9000);
  for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<int16_t>(16000.0f * sinf(6.2831853f * hz * i / 44100.0f));
  const size_t n = ds.feed(in.data(), in.size(), out.data(), out.size());
  float a = 0.0f, b = 0.0f;
  for (size_t i = in.size() / 4; i < in.size(); ++i) a += static_cast<float>(in[i]) * in[i];
  for (size_t i = n / 4; i < n; ++i) b += static_cast<float>(out[i]) * out[i];
  return 10.0f * log10f((b / (n - n / 4)) / (a / (in.size() - in.size() / 4)));
}

static void testDownsampler() {
  Downsampler ds(44100.0f, 16000.0f, REC_LOWPASS_HZ);
  std::vector<int16_t> in(44100, 0), out(20000);
  size_t total = 0;
  for (size_t i = 0; i < in.size(); i += 512) total += ds.feed(in.data() + i, in.size() - i < 512 ? in.size() - i : 512, out.data() + total, out.size() - total);
  CHECK(total >= 15999 && total <= 16001);  // one second in, one second out, block by block
  CHECK(sineGainDb(1000.0f) > -1.0f && sineGainDb(1000.0f) < 0.5f);  // speech passes
  CHECK(sineGainDb(10000.0f) < -15.0f);  // would alias to 6 kHz: filtered out first
  int16_t tiny[4];
  Downsampler full(44100.0f, 16000.0f, REC_LOWPASS_HZ);
  CHECK(full.feed(in.data(), 1000, tiny, 4) == 4);  // never writes past `max`
}

// 1 s at 16 kHz: a voice-like 500 Hz tone for 300 ms at `amp` (plus a DC offset), then quiet.
static std::vector<int16_t> spoken(float amp) {
  std::vector<int16_t> v = sine(amp, 500.0f, 16000, 16000.0f);
  for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<int16_t>((i < 4800 ? v[i] : 0) + 400);
  return v;
}

static float rmsDb(const std::vector<int16_t>& v, size_t from, size_t to) {
  double s = 0.0;
  for (size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
  return static_cast<float>(10.0 * log10(s / (to - from) / (32768.0 * 32768.0) + 1e-12));
}

static float normalizedGain(std::vector<int16_t> v) {
  return normalizeRecording(v.data(), v.size(), 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB).gainDb;
}

static void testNormalizeRecording() {
  std::vector<int16_t> voice = spoken(0.01f);  // -43 dBFS while speaking, 70 % pauses
  const Normalized n = normalizeRecording(voice.data(), voice.size(), 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB);
  CHECK(fabsf(n.levelDb + 43.0f) < 1.5f);
  CHECK(fabsf(rmsDb(voice, 800, 4800) - REC_TARGET_DB) < 1.5f);  // the voice now at -14 dBFS
  CHECK(fabsf(rmsDb(voice, 6000, 16000)) > 60.0f);             // the pauses (and the DC) stay quiet
  std::vector<int16_t> clicked = spoken(0.01f);
  clicked[2000] = 20000;  // one loud click does not hold the voice down
  CHECK(fabsf(normalizedGain(clicked) - n.gainDb) < 1.0f);
  std::vector<int16_t> hum = sine(0.1f, 60.0f, 16000, 16000.0f);  // mains hum the speaker cannot play
  CHECK(normalizeRecording(hum.data(), hum.size(), 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB).levelDb < -45.0f);
  CHECK(normalizedGain(sine(0.0005f, 1000.0f, 16000, 16000.0f)) == REC_MAX_GAIN_DB);  // a quiet room: capped
  std::vector<int16_t> loud = sine(0.95f, 1000.0f, 16000, 16000.0f);
  const float g = normalizeRecording(loud.data(), loud.size(), 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB).gainDb;
  int top = 0;
  for (const int16_t s : loud) top = abs(s) > top ? abs(s) : top;
  CHECK(g < 0.0f && top < 32767);  // a loud one comes down, never clipped
  std::vector<int16_t> zero(100, 0);
  normalizeRecording(zero.data(), zero.size(), 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB);
  CHECK(zero[0] == 0 && zero[99] == 0);
  CHECK(normalizeRecording(nullptr, 0, 16000.0f, REC_TARGET_DB, REC_MAX_GAIN_DB).gainDb == 0.0f);
}

static void testWavHeaderAndParse() {
  uint8_t h[WAV_HEADER_BYTES];
  wavHeader(h, 16000, 1, 16, 32000);
  CHECK(memcmp(h, "RIFF", 4) == 0 && getLe(h + 4, 4) == 36 + 32000 && memcmp(h + 8, "WAVEfmt ", 8) == 0);
  CHECK(getLe(h + 20, 2) == 1 && getLe(h + 22, 2) == 1 && getLe(h + 24, 4) == 16000);
  CHECK(getLe(h + 28, 4) == 32000 && getLe(h + 32, 2) == 2 && getLe(h + 34, 2) == 16);
  CHECK(memcmp(h + 36, "data", 4) == 0 && getLe(h + 40, 4) == 32000);
  WavInfo w;
  CHECK(parseWav(h, sizeof(h), 44 + 32000, w) && w.dataOffset == 44 && w.dataBytes == 32000);
  CHECK(w.durationMs() == 1000);
  CHECK(parseWav(h, sizeof(h), 44 + 1000, w) && w.dataBytes == 1000);  // a cut file plays what it has
  uint8_t bad[WAV_HEADER_BYTES];
  memcpy(bad, h, sizeof(bad));
  bad[0] = 'X';
  CHECK(!parseWav(bad, sizeof(bad), 44 + 32000, w));  // not RIFF
  memcpy(bad, h, sizeof(bad));
  bad[20] = 3;  // float, not PCM
  CHECK(!parseWav(bad, sizeof(bad), 44 + 32000, w));
  memcpy(bad, h, sizeof(bad));
  bad[34] = 8;  // 8-bit
  CHECK(!parseWav(bad, sizeof(bad), 44 + 32000, w));
  CHECK(!parseWav(h, 20, 44 + 32000, w));  // truncated header
}

static void testMessageNames() {
  CHECK(messageIdOf("msg_0004.wav") == 4 && messageIdOf("msg_9999.wav") == 9999);
  CHECK(messageIdOf("msg_0000.wav") == -1 && messageIdOf("msg_04.wav") == -1 && messageIdOf("msg_00a4.wav") == -1);
  CHECK(messageIdOf("msg_0004.wav.tmp") == -1 && messageIdOf("._msg_0004.wav") == -1 && messageIdOf("MSG_0004.WAV") == -1);
  char name[16];
  messageName(12, name, sizeof(name));
  CHECK(strcmp(name, "msg_0012.wav") == 0);
}

static void testRecorderFlow() {
  RecorderFlow f;
  CHECK(f.phase() == RecPhase::Idle);
  f.start(1000);
  CHECK(f.update(1000) == RecPhase::Countdown && f.countdown(1000) == 3);
  CHECK(f.update(2500) == RecPhase::Countdown && f.countdown(2500) == 2);
  CHECK(f.update(3999) == RecPhase::Countdown && f.countdown(3999) == 1);
  CHECK(f.update(4000) == RecPhase::Recording && f.recordedMs(4000) == 0);
  CHECK(f.update(9000) == RecPhase::Recording && f.recordedMs(9000) == 5000);
  CHECK(f.update(4000 + REC_MAX_MS) == RecPhase::Saving);  // the limit stops it
  f.saved(20000, true);
  CHECK(f.update(20000) == RecPhase::Saved && f.update(20000 + REC_RESULT_MS) == RecPhase::Idle);

  RecorderFlow c;
  c.start(0);
  c.stop(1500);  // a key during the countdown: nothing is recorded
  CHECK(c.update(5000) == RecPhase::Idle);

  RecorderFlow s;
  s.start(0xFFFFFFFFu - 1000);  // millis() wraps during REC
  CHECK(s.update(0xFFFFFFFFu - 1000 + REC_COUNTDOWN_MS) == RecPhase::Recording);
  s.stop(4000);
  CHECK(s.phase() == RecPhase::Saving);
  s.saved(4100, false);
  CHECK(s.update(4100) == RecPhase::Failed && s.update(4100 + REC_RESULT_MS) == RecPhase::Idle);
}

static void testClapMillisWrap() {
  ClapDetector d;
  Signal sig;
  sig.length(2500);
  sig.background(-60.0f);
  sig.clap(1000, -12.0f);
  sig.clap(1300, -12.0f);
  const Run r = feed(d, sig, 0xFFFFFFFFu - 1150);  // millis() wraps between the two claps
  CHECK(r.claps == 2 && r.sequences[2] == 1);
}

void runVoiceTests() {
  testDb();
  testSilence();
  testDcOffsetIgnored();
  testSineLevels();
  testClipping();
  testClapSilenceAndSteadyNoise();
  testClapSingle();
  testClapLongSoundIsNot();
  testClapEchoIgnored();
  testClapTooFastIsNotADouble();
  testClapDoubleAndTriple();
  testClapDoubleWaitsForTheWindow();
  testClapSecondClapNotLost();
  testClapSlowSequenceIsTwoSingles();
  testClapNoiseFloorFollowsTheRoom();
  testClapSensitivity();
  testClapExtraMarginAndOwnSounds();
  testClapMillisWrap();
  testPlaybackEnvelope();
  testHighPass();
  testStandbyClaps();
  testKnockIsNotAClap();
  testUnevenPairIsNotADouble();
  testSlowPairIsNotADouble();
  testAlarmIsNotATriple();
  testStandbyNeedsALouderClap();
  testQuietBeforeAndBangs();
  testClapIndexAsItHappens();
  testDownsampler();
  testNormalizeRecording();
  testWavHeaderAndParse();
  testMessageNames();
  testRecorderFlow();
  testClapThatIsTheProgramme();
}
