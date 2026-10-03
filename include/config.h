#pragma once

// Application settings. Pins live in board_config.h.

#include <stdint.h>

constexpr const char* PAUTV_VERSION = "0.2.0-alpha2";

// Periodic stats on the serial port (every 5 s) once playback exists.
#ifndef PAUTV_DEBUG_STATS
#define PAUTV_DEBUG_STATS 1
#endif

// --- Display --------------------------------------------------------------------------------
constexpr uint8_t PAUTV_ROTATION = 1;  // landscape: 1 or 3 (3 = upside down)
static_assert(PAUTV_ROTATION == 1 || PAUTV_ROTATION == 3, "RETROTV is landscape only");
constexpr int32_t PAUTV_SPI_HZ = 40000000;  // Freenove value; 80 MHz is a hardware experiment
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

constexpr uint8_t BRIGHTNESS_DEFAULT_PCT = 80;
constexpr uint8_t BRIGHTNESS_MIN_PCT = 10;
constexpr uint8_t BACKLIGHT_PWM_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_PWM_HZ = 5000;
constexpr uint8_t BACKLIGHT_PWM_BITS = 8;

// --- Boot sequence --------------------------------------------------------------------------
constexpr uint32_t BOOT_SCREEN_MS = 1500;
constexpr uint32_t BOOT_DIAG_MS = 3000;
constexpr uint32_t HOME_CARD_MS = 1000;
constexpr uint32_t INTRO_MAX_MS = 20000;   // the power-on video is cut here, whatever its length
constexpr uint32_t APP_LOOP_PERIOD_MS = 10;  // loopTask sleeps this long so lower tasks run
constexpr uint32_t TEST_CARD_REFRESH_MS = 1000;  // clock / FPS / network line on the test card

// --- Input ----------------------------------------------------------------------------------
// Front buttons and BOOT: click / double click / hold.
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_DOUBLE_CLICK_MS = 350;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 1000;
constexpr uint32_t POWER_HOLD_MS = 2000;           // hold CH- this long: standby
constexpr uint32_t POWER_OFF_CRACKLE_MS = 120;     // the static crackle of a CRT switching off...
constexpr uint8_t POWER_OFF_CRACKLE_PCT = 10;      // ...soft
constexpr uint32_t STANDBY_RELEASE_WAIT_MS = 5000; // standby waits for the keys to be let go
constexpr uint32_t LED_BLINK_MS = 80;              // front LED out for a moment with each order
constexpr uint32_t BATTERY_READ_MS = 5000;         // the LiPo voltage, smoothed over readings (power/Battery.h)
constexpr uint32_t BATTERY_WARNING_MS = 5000;      // "BATERIA 14%": big, centred, blinking
constexpr uint32_t BATTERY_EMPTY_NOTICE_MS = 5000; // "BATERIA AGOTADA" the same way, then standby
constexpr uint32_t BATTERY_CHARGE_NOTICE_MS = 3000; // "CARGANDO" when the cable goes in
// The case's front keys. 1: tele90 v9, or v10 with keys. 0: the button-less tele90 v10, run from the
// web remote and claps: nothing may then leave the TV where only a key wakes it (src/power/Standby.h).
#ifndef PAUTV_HAS_KEYS
#define PAUTV_HAS_KEYS 1
#endif
constexpr uint32_t FLAT_CHECK_S = 300;          // button-less, flat battery: wake every 5 min to see if it charges...
constexpr uint32_t FLAT_RESUME_MV = 3700;       // ...and switch on from this reading (USB in reads ~4.1 V)
constexpr uint32_t NO_OFF_NOTICE_MS = 3000;     // button-less: "NO SE APAGA" when off could never be undone
constexpr uint32_t REMOTE_STANDBY_POLL_MS = 50; // remote standby: how often the web remote's orders are read
constexpr uint32_t REMOTE_STANDBY_CPU_MHZ = 80; // the lowest speed the Wi-Fi works at
// Capacitive touch (FNK0104B only): tap / swipe / hold, in screen pixels.
constexpr int TOUCH_TAP_MAX_PX = 15;
constexpr int TOUCH_SWIPE_MIN_PX = 40;
constexpr uint32_t TOUCH_LONG_PRESS_MS = 800;
constexpr uint32_t TOUCH_POLL_MS = 20;        // 50 Hz register poll
constexpr uint32_t TOUCH_RESET_PULSE_MS = 10;

// --- Storage (microSD) -----------------------------------------------------------------------
constexpr uint8_t SD_MOUNT_ATTEMPTS_PER_FREQ = 2;  // 40 MHz twice, then 20 MHz twice
constexpr uint8_t SD_MAX_OPEN_FILES = 5;           // video + audio + config, with margin
constexpr uint32_t SD_REMOUNT_WINDOW_MS = 30000;   // a failing card is mounted again at most...
constexpr uint8_t SD_REMOUNTS_PER_WINDOW = 3;      // ...this many times per window
constexpr size_t JSON_FILE_MAX_BYTES = 8192;       // channels.json / wifi.json

// --- Settings (NVS) --------------------------------------------------------------------------
// A value is written only after it stopped changing for this long: zapping wears no flash.
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 4000;
constexpr uint8_t VOLUME_DEFAULT = 50;
constexpr uint8_t VOLUME_STEP = 5;
constexpr uint16_t CHANNEL_DEFAULT = 1;

// --- Wi-Fi and clock -------------------------------------------------------------------------
// Never blocks: the first attempt runs behind the boot and diagnostics screens.
constexpr uint32_t PAUTV_WIFI_FIRST_TIMEOUT_MS = 8000;  // then LOCAL MODE at once (6 s timed out on the real router)
constexpr uint32_t WIFI_ATTEMPT_TIMEOUT_MS = 10000;     // background retries
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 60000;
constexpr const char* PAUTV_HOSTNAME = "retrotv";  // the name the router lists
constexpr const char* PAUTV_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";  // Spain (peninsula)
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";
constexpr long PAUTV_VALID_EPOCH = 1700000000;  // earlier = clock never set by NTP

// --- RETROTV Voice (docs/VOICE.md) ------------------------------------------------------------
// An optional layer, all off in the normal build: the TV is then exactly what it was. Build it in
// with `pio run -e voice` (-DPAUTV_VOICE_ENABLED=1). Each part can still be left out on its own.
#ifndef PAUTV_VOICE_ENABLED
#define PAUTV_VOICE_ENABLED 0
#endif
#ifndef PAUTV_MIC_ENABLED  // I2S RX + the codec's microphone input, the VU meter, MIC TEST
#define PAUTV_MIC_ENABLED PAUTV_VOICE_ENABLED
#endif
#ifndef PAUTV_CLAP_ENABLED  // double clap = off to voice standby (mute without CLAP_WAKE), triple = next channel
#define PAUTV_CLAP_ENABLED PAUTV_MIC_ENABLED
#endif
#ifndef PAUTV_CLAP_WAKE_ENABLED  // voice standby: two claps turn the TV off and on (AJUSTES > VOZ > APAGADO)
#define PAUTV_CLAP_WAKE_ENABLED PAUTV_CLAP_ENABLED
#endif
#ifndef PAUTV_WAKEWORD_ENABLED  // reserved: "HEY RETRO" is not implemented (needs ESP-SR)
#define PAUTV_WAKEWORD_ENABLED 0
#endif
#ifndef PAUTV_VOICE_COMMANDS_ENABLED  // reserved: offline voice commands are not implemented
#define PAUTV_VOICE_COMMANDS_ENABLED 0
#endif
#ifndef PAUTV_RECORDER_ENABLED  // short WAV messages, only with REC on screen (AJUSTES > VOZ)
#define PAUTV_RECORDER_ENABLED PAUTV_MIC_ENABLED
#endif
#if PAUTV_WAKEWORD_ENABLED || PAUTV_VOICE_COMMANDS_ENABLED
#error "HEY RETRO and voice commands are not implemented yet (docs/VOICE.md)"
#endif
#if PAUTV_CLAP_ENABLED && !PAUTV_MIC_ENABLED
#error "PAUTV_CLAP_ENABLED needs PAUTV_MIC_ENABLED"
#endif
#if PAUTV_RECORDER_ENABLED && !PAUTV_MIC_ENABLED
#error "PAUTV_RECORDER_ENABLED needs PAUTV_MIC_ENABLED"
#endif
#if PAUTV_CLAP_WAKE_ENABLED && !PAUTV_CLAP_ENABLED
#error "PAUTV_CLAP_WAKE_ENABLED needs PAUTV_CLAP_ENABLED"
#endif
// Voice standby (AppVoice.cpp): the TV looks off (CRT off, backlight, speaker, LED and Wi-Fi off)
// but the microphone keeps listening. A wake is a restart: the same boot as from deep sleep.
constexpr uint32_t VOICE_STANDBY_POLL_MS = 20;
constexpr uint32_t LISTEN_LED_FLASH_MS = 120;  // one clap in standby: "I heard you"
constexpr uint32_t VOICE_STANDBY_CPU_MHZ = 80;  // I2S runs from its own PLL clock, not the CPU's
// Recorder (voice/Recorder.h): short messages, only with REC on screen.
constexpr uint32_t REC_SAMPLE_RATE = 16000;    // WAV, PCM 16 bit mono
constexpr float REC_LOWPASS_HZ = 6000.0f;      // speech; the rest would alias at 16 kHz
constexpr uint32_t REC_MAX_MS = 15000;         // 15 s = 480 KB
constexpr float REC_HIGHPASS_HZ = 300.0f;      // saved without the bass the TV's small speaker cannot play...
constexpr float REC_TARGET_DB = -14.0f;        // ...with the voice raised to -14 dBFS (its loud 20 ms blocks)...
constexpr float REC_MAX_GAIN_DB = 36.0f;       // ...but at most +36 dB, so a silent room stays quiet
constexpr uint32_t REC_COUNTDOWN_MS = 3000;    // 3, 2, 1
constexpr uint32_t REC_RESULT_MS = 2500;       // MENSAJE GUARDADO / ERROR AL GUARDAR
constexpr uint32_t REC_SCREEN_REFRESH_MS = 250;
constexpr const char* REC_DIR = "/retrotv/voice/messages";
// MENSAJES channel (internal source "messages", voice/MessagePlayer.h): its task only exists while
// the channel is on and replaces the video's audio task then (same core and priority, also paced
// by writePcm).
constexpr uint32_t MSG_TASK_STACK = 4096;
constexpr uint32_t MSG_GAP_MS = 1500;          // silence between two messages
constexpr uint32_t MSG_STOP_WAIT_MS = 500;
constexpr float MSG_LOWPASS_HZ = 7000.0f;      // 16 kHz -> 44.1 kHz: no images above the voice
constexpr uint32_t MSG_SCREEN_REFRESH_MS = 250;
// The microphone shares the playback I2S port (same clocks, 44.1 kHz, 16 bit, stereo slots); the
// ES8311 puts its one ADC channel in the left slot. Its analogue MEMS microphone is on MIC1.
constexpr uint8_t MIC_PGA_REG14 = 0x1A;     // MIC1P/N in, analogue PGA +30 dB (Freenove's echo sketch)
constexpr uint8_t MIC_ADC_VOLUME_REG17 = 0xC8;  // ADC digital volume +4.5 dB (0xBF = 0 dB)
constexpr uint8_t MIC_ADC_SCALE = 4;        // REG16 ADC gain scale-up, 6 dB a step: 4 = +24 dB, the chip's default
                                            // (0 dB read room sound at -80..-89 dBFS, 2026-10-02)
constexpr size_t MIC_BLOCK_FRAMES = 512;    // 11.6 ms per read
constexpr uint32_t MIC_READ_TIMEOUT_MS = 100;
// The capture task sleeps in i2s_read. Above the display (prio 3, whose 38 ms frames would
// otherwise eat the ~46 ms RX DMA cushion), below audio playback (prio 5). ~0.5 % of a core.
constexpr int MIC_TASK_CORE = 1;
constexpr int MIC_TASK_PRIO = 4;
constexpr uint32_t MIC_TASK_STACK = 4096;
constexpr int16_t MIC_FLOOR_DB10 = -900;    // levels below -90 dBFS show as -90
constexpr int16_t MIC_METER_MIN_DB10 = -600;  // the VU bar spans -60..0 dBFS
constexpr uint32_t MIC_SCREEN_REFRESH_MS = 66;  // the MICROFONO screen, ~15 Hz
constexpr uint32_t MIC_PEAK_HOLD_MS = 1000;   // the peak mark on the bar
constexpr uint32_t MIC_TEST_MS = 10000;       // serial `v`: levels this long
constexpr uint32_t MIC_TEST_LOG_MS = 250;
// Clap detector (voice/ClapDetector.h). Starting values, to calibrate with real claps.
constexpr uint32_t CLAP_WINDOW_MS = 5;            // energy windows
constexpr float CLAP_FLOOR_ALPHA = 1.0f / 300;    // noise floor EMA per window: ~1.5 s
constexpr uint8_t CLAP_SENSITIVITY_DEFAULT = 80;  // 60 missed claps in the case with the programme raising the floor (2026-10-03)
constexpr uint8_t CLAP_SENSITIVITY_STEP = 10;     // AJUSTES > VOZ > SENSIBLE
constexpr int16_t CLAP_MARGIN_MAX_DB10 = 300;     // over the floor at sensitivity 0
constexpr int16_t CLAP_MARGIN_MIN_DB10 = 100;     // at 100 (60 -> 18 dB)
constexpr int16_t CLAP_MIN_DB10 = -460;           // never below -46 dBFS, however quiet the room
                                                  // (real claps at 1 m: -22..-38 dBFS over a -54..-66 room)
constexpr int16_t CLAP_ATTACK_DB10 = 90;          // the rise within one or two windows
constexpr int16_t CLAP_DECAY_DB10 = 100;          // it must drop this far from its peak...
constexpr uint32_t CLAP_MAX_MS = 100;             // ...this soon, or it is not a clap
constexpr uint32_t CLAP_ECHO_MS = 180;            // after a clap, its echo is ignored: two hands are never
                                                  // faster (real doubles: 215-380 ms; a mute pop + echo: 161 ms)
constexpr uint32_t CLAP_GAP_MAX_MS = 700;         // claps further apart are separate sequences
// What makes a sequence of claps real (a knock, an alarm or a rattle is not), checked when it closes:
constexpr uint32_t CLAP_RHYTHM_MAX_MS = 600;      // two hands: 0.18-0.6 s between claps (real: 0.21-0.5 s)
constexpr int16_t CLAP_PEAK_SPREAD_DB10 = 120;    // the claps of one sequence within 12 dB (real triples: up to 10.1)
constexpr uint8_t CLAP_SEQUENCE_MAX = 3;          // 4 or more (an alarm beeping, something rattling): nothing
constexpr float CLAP_HF_HZ = 2000.0f;             // a clap is bright: much of its energy above 2 kHz...
constexpr float CLAP_HF_MIN_SHARE = 0.10f;        // ...a knock is mostly below it. Claps: 21-35 % with the board bare,
                                                  // 11-17 % inside the case (2026-10-03): the plastic dulls them
constexpr int16_t CLAP_STANDBY_MIN_DB10 = -360;   // in STANDBY VOZ only a clap near the TV counts (-36 dBFS)
// Claps switch the TV off (while it plays) and on (STANDBY VOZ): three, with quiet around them in
// standby. Two let household pairs through, even with every check above (user, 2026-10-03); three
// claps in a hand rhythm and of even loudness rarely happen by chance. 4 or more are nothing.
constexpr uint8_t CLAP_POWER_CLAPS = 3;
constexpr uint32_t STANDBY_QUIET_BEFORE_MS = 2500;  // no bang in the 2.5 s before the first clap
constexpr uint32_t STANDBY_QUIET_AFTER_MS = 500;    // nor in the 0.5 s after the sequence closed (1.2 s in all)
constexpr int16_t CLAP_PLAYBACK_EXTRA_DB10 = 60;  // threshold up while the TV plays sound
// A "clap" that lines up with a sharp rise in the programme's own sound is the TV hearing itself
// (voice/PlaybackEnvelope.h). The mic's timestamps run 0-12 ms late (RX DMA), the speaker's +-5 ms.
constexpr uint32_t CLAP_TV_LOOKBACK_MS = 80;
constexpr uint32_t CLAP_TV_LOOKAHEAD_MS = 30;
constexpr int16_t CLAP_TV_RISE_DB10 = 80;   // the programme jumped 8 dB...
constexpr int16_t CLAP_TV_MIN_DB10 = -350;  // ...to at least -35 dBFS (digital, before the volume)
// ...unless it is loud and stands far over the floor. In a dense programme a rise falls in that window
// for about every other clap. Measured in the case (2026-10-03): the user's claps -10 to -25 dBFS,
// 30.5-45 dB over the floor, half of them dropped; the programme's own bangs 24-32 dB over the floor
// but at -38 to -44 dBFS (earlier punches at 75 % volume: -14 to -24 dBFS, at most 26 dB over).
constexpr int16_t CLAP_TV_OVER_FLOOR_DB10 = 280;
constexpr int16_t CLAP_TV_OVER_MIN_DB10 = -300;
constexpr float CLAP_TV_HIGHPASS_HZ = 400.0f;  // measured above this: the small speaker plays no bass
constexpr uint32_t CLAP_SELF_SOUND_TAIL_MS = 350; // after its own static, beep, mute or volume change (the
                                                  // speaker pops when the DAC mutes), claps are not taken

// --- Audio (ES8311 + I2S) ------------------------------------------------------------------
constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
constexpr int AUDIO_MCLK_MULTIPLE = 256;  // MCLK = 11.2896 MHz, a row of the ES8311 coeff table
constexpr int AUDIO_DMA_BUF_COUNT = 8;
constexpr int AUDIO_DMA_BUF_LEN = 256;   // frames: 8 x 256 / 44.1 kHz = ~46 ms of cushion
constexpr uint32_t AUDIO_WRITE_TIMEOUT_MS = 100;
// User volume 0..100 -> ES8311 volume register scale (75 = 0 dB, ~1.28 dB per unit).
// 0 is silence; 1..100 spread over the useful range. Tune by ear on the real speaker.
constexpr int CODEC_VOLUME_MIN = 45;
constexpr int CODEC_VOLUME_MAX = 85;
constexpr uint32_t SYNTH_FADE_MS = 5;  // ramp in/out so tones never click
constexpr uint16_t BEEP_HZ = 1000;
constexpr uint32_t BEEP_MS = 80;
constexpr uint8_t BEEP_LEVEL_PCT = 70;  // 40 % was too faint on the stock speaker
constexpr uint16_t TEST_TONE_HZ = 440;
constexpr uint32_t TEST_TONE_MS = 1000;
constexpr uint8_t TEST_TONE_LEVEL_PCT = 90;

// --- Video (raw MJPEG + ADTS AAC, see tools/convert_video.sh) ----------------------------------
constexpr uint8_t VIDEO_FPS = 24;                    // episodes without an .idx; the most one may ask for
constexpr size_t JPEG_FRAME_BUF_SIZE = 48 * 1024;    // one compressed frame, PSRAM
constexpr size_t VIDEO_READ_CHUNK = 4096;            // the splitter's reads from the read-ahead ring
constexpr uint32_t SD_PREFETCH_RING_SIZE = 128 * 1024;  // PSRAM, power of two: ~0.5 s of video ahead
constexpr size_t SD_PREFETCH_CHUNK = 16 * 1024;     // one card read, DMA-capable RAM
constexpr uint32_t SD_PREFETCH_STALL_MS = 3000;     // no byte for this long: the card is gone
constexpr int VIDEO_BLOCK_PIXELS = 2048;             // = JPEGDEC MAX_BUFFERED_PIXELS
constexpr bool VIDEO_DITHER_DEFAULT = false;        // off: on the panel its 4x4 pattern reads as squares in dark scenes (serial d)
constexpr int VIDEO_BLOCK_COUNT = 6;                 // decoded blocks queued for the display
constexpr uint32_t VIDEO_BLOCK_WAIT_MS = 200;        // decoder gives up a frame if display stalls
constexpr size_t AAC_INPUT_BUF_SIZE = 4096;
constexpr uint32_t MEDIA_STOP_TIMEOUT_MS = 500;
constexpr uint32_t MEDIA_STATS_PERIOD_MS = 5000;
// A frame reaches the glass ~45 ms after it is released (decode ~18 ms + SPI draw ~38 ms at
// 40 MHz, measured on the board: av_drift_ms avg 44). Releasing it this much earlier puts
// picture and sound back together.
constexpr uint32_t VIDEO_PIPELINE_LEAD_MS = 40;

// --- Channels, zapping, OSD ----------------------------------------------------------------
constexpr uint32_t CHANNEL_STATIC_MS = 200;      // static between channels (120-300 ms)
constexpr uint32_t CHANNEL_FLASH_MS = 60;        // CRT horizontal flash before the new picture
constexpr uint8_t STATIC_NOISE_LEVEL_PCT = 15;   // hiss under the static, soft on purpose
constexpr uint32_t NO_SIGNAL_SKIP_MS = 3000;     // a broken channel shows NO SIGNAL, then zaps on
constexpr uint32_t OSD_CHANNEL_MS = 1500;
constexpr uint32_t ALERT_BLINK_ON_MS = 700;     // an alert OSD (battery) blinks slowly: shown...
constexpr uint32_t ALERT_BLINK_OFF_MS = 500;    // ...then hidden
constexpr uint32_t OSD_VOLUME_MS = 1000;
constexpr uint32_t STARTUP_ERROR_MS = 5000;      // SD / channels.json problem, then fallback
constexpr uint32_t SETTINGS_REFRESH_MS = 1000;   // live Wi-Fi line in the settings menu
constexpr uint32_t TELETEXT_PAGE_MS = 12000;     // teletext pages turn by themselves...
constexpr uint32_t TELETEXT_BY_HAND_MS = 60000;  // ...slower once one was chosen by hand
constexpr uint32_t TELETEXT_REFRESH_MS = 1000;   // header clock, minutes left
constexpr uint32_t TELETEXT_GUIDE_MS = 60000;    // the server's guide of live channels, asked again
constexpr uint8_t BRIGHTNESS_STEP_PCT = 10;
// Debug builds only (PAUTV_DEBUG_STATS), started from the serial monitor, never by itself:
// "z" = STRESS_ZAP_COUNT channel changes every STRESS_ZAP_INTERVAL_MS; "S<seconds>" = soak test
// zapping every <seconds> (0 = never) with a snapshot every SOAK_SNAPSHOT_MS.
constexpr uint16_t STRESS_ZAP_COUNT = 100;
constexpr uint32_t STRESS_ZAP_INTERVAL_MS = 1500;
constexpr uint32_t SOAK_DEFAULT_ZAP_S = 60;
constexpr uint32_t SOAK_SNAPSHOT_MS = 60000;
constexpr size_t SERIAL_TX_BUFFER = 4096;       // USB log ring: a burst of stats lines fits without cuts
constexpr uint32_t NET_VERBOSE_MS = 30000;     // [NET] every second for this long after a remote tune
constexpr uint32_t WIFI_WARMUP_LOG_S = 30;     // [WIFI] rssi every second after joining
constexpr uint32_t STABLE_SECONDS = 5;         // clean seconds in a row = stable playback
constexpr uint32_t BENCH_MS = 10000;
constexpr int BENCH_MAX_CONNECTIONS = 4;
constexpr uint32_t DISK_STRESS_MAX_S = 3600;
constexpr uint32_t DISK_STRESS_SCAN_MS = 10000;  // SD stress: a Wi-Fi scan every 10 s (~4 s each)

// --- FreeRTOS tasks -------------------------------------------------------------------------
// Core 1 serves audio, so it must never be blocked for long:
//   audio   prio 5  highest of the app; sleeps in i2s_write (DMA ~46 ms cushion) and pre-empts
//                   everything else on core 1, so drawing can never starve the codec.
//   display prio 3  the only task that touches the panel; SPI writes are polled, pre-emptible.
//   loop    prio 1  Arduino loopTask: state machine, input, I2C.
// Core 0 hosts Wi-Fi/lwIP (prio 18-23), the JPEG decoder and the remote-channel reader:
//   sdread  prio 3  reads a local episode ahead (SdPrefetch); mostly waits on the card.
//   schedule prio 1 builds channel programmes for the guides (ScheduleBuilder), off the loop.
//   video   prio 2  split + decode; yields at least once per frame so IDLE0 feeds the task WDT.
//   net     prio 3  drains the two sockets of a remote channel into its rings; sleeps 1 ms per
//                   pass, so it never starves the decoder or IDLE0.
constexpr int DISPLAY_TASK_CORE = 1;
constexpr int DISPLAY_TASK_PRIO = 3;
constexpr uint32_t DISPLAY_TASK_STACK = 6144;
constexpr uint32_t DISPLAY_INIT_TIMEOUT_MS = 2000;
constexpr uint32_t DISPLAY_TICK_MS = 40;  // 25 fps for animated screens (static, test card)

constexpr int AUDIO_TASK_CORE = 1;
constexpr int AUDIO_TASK_PRIO = 5;
constexpr uint32_t MEDIA_AUDIO_TASK_STACK = 6144;
constexpr int VIDEO_TASK_CORE = 0;
constexpr int VIDEO_TASK_PRIO = 2;
constexpr uint32_t VIDEO_TASK_STACK = 8192;
constexpr int SD_PREFETCH_TASK_CORE = 0;
constexpr int SD_PREFETCH_TASK_PRIO = 3;
constexpr uint32_t SD_PREFETCH_TASK_STACK = 4096;
constexpr int SCHEDULE_TASK_CORE = 0;
constexpr int SCHEDULE_TASK_PRIO = 1;
constexpr uint32_t SCHEDULE_TASK_STACK = 6144;
constexpr uint32_t SD_REMOUNT_BUILDER_WAIT_MS = 3000;  // a channel's .idx headers, read before a remount
constexpr int NET_TASK_CORE = 0;
constexpr int NET_TASK_PRIO = 3;
constexpr uint32_t NET_TASK_STACK = 6144;
// Web remote (src/web): the HTTP server's task, on the Wi-Fi core, above the decoder. With the
// dither on the decoder keeps core 0 ~90 % busy and, below it, requests waited up to 12 s. A
// request takes a few ms; opening the remote (page + 26 logos) costs ~1 s of dropped frames.
constexpr int WEB_TASK_CORE = 0;
constexpr int WEB_TASK_PRIO = 3;
constexpr uint32_t WEB_TASK_STACK = 6144;  // settings requests parse JSON bodies
constexpr const char* WEB_HOSTNAME = "retrotv";  // http://retrotv.local (retrotv-server.local is the server)
constexpr uint8_t WEB_COMMAND_QUEUE = 8;
constexpr uint32_t WEB_GUIDE_IDLE_MS = 120000;   // the guide is kept up while a phone asked in this time
constexpr uint32_t WEB_GUIDE_REFRESH_MS = 30000; // made again this often (sooner when a programme is found)
constexpr uint32_t WEB_GUIDE_STEP_MS = 500;      // how often the builder is handed the next channel
constexpr size_t WEB_LOGO_MAX_BYTES = 32 * 1024;  // per channel logo, loaded into PSRAM at boot

// --- Remote channels (RETROTV Server, server/README.md) ------------------------------------------
// Buffers in PSRAM, powers of two: 512 KB of video holds 3 s even at ~170 KB/s, 16 KB of audio
// ~4 s. Playback starts once the server's prebuffer_ms of video frames are in (default 1.5 s;
// counted in frames, so it means the same at any bitrate). A small prime (48 KB) let the audio
// clock start while the server's first seconds were still on their way: on a live channel the
// picture then arrived late and was dropped for 10-20 s.
constexpr uint32_t REMOTE_VIDEO_RING = 512 * 1024;
constexpr uint32_t REMOTE_AUDIO_RING = 16 * 1024;
constexpr uint32_t REMOTE_RING_HEADROOM = 64 * 1024;  // a ring this close to full counts as primed
constexpr uint32_t REMOTE_PRIME_AUDIO = 1024;
constexpr uint32_t REMOTE_PRIME_TIMEOUT_MS = 6000;      // + 2x the prebuffer: then NO DATA
constexpr uint32_t REMOTE_PRIME_TIMEOUT_MAX_MS = 14000;
constexpr uint32_t REMOTE_CONNECT_TIMEOUT_MS = 4000;  // TCP connect: room for one SYN retransmit (lwIP: 3 s)
constexpr uint32_t REMOTE_REPLY_TIMEOUT_MS = 2000;    // HTTP head of a stream, session JSON body
constexpr uint32_t REMOTE_SESSION_TIMEOUT_MS = 16000; // POST /api/sessions: a live source first starts FFmpeg (server: 10 s max)
constexpr uint32_t REMOTE_MDNS_TIMEOUT_MS = 1500;     // resolving <name>.local
constexpr uint32_t REMOTE_STALL_MS = 3000;            // no byte while playing: signal lost
constexpr uint32_t REMOTE_TUNE_TIMEOUT_MS = 25000;    // the App's backstop for a whole tune
constexpr uint32_t REMOTE_READ_SLICE_MS = 2;          // a waiting read re-checks this often
