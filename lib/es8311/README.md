# es8311 (Espressif ES8311 codec driver)

`es8311.h`, `es8311.cpp` and `es8311_reg.h` are copied **unmodified** from Freenove's
`Tutorial_No_Touch/Sketches/Sketch_07.1_Music/` in
[Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display) (commit of 2026-08-10).
The code is Espressif's, licensed Apache-2.0 (SPDX headers in each file).

RETROTV calls `es8311_create` + `es8311_init` itself and never calls the sketch helper
`es8311_codec_init()` (16 kHz ×384, aborts on error). See `docs/HARDWARE.md`, conflict 4.
