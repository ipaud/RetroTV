# The pure voice headers (ClapDetector, VoiceStandby), config.h and board_config.h come from the TV
# firmware next door, unchanged; so does the ES8311 driver (lib/es8311, legacy I2C API).
idf_component_register(
    SRCS "main.cpp" "MicroWakeWord.cpp" "../../lib/es8311/es8311.cpp"
    INCLUDE_DIRS "." "../../include" "../../src" "../../lib/es8311"
    REQUIRES driver esp_driver_i2s esp_driver_gpio esp_adc esp_partition app_update nvs_flash esp_timer
             esp_driver_usb_serial_jtag
    EMBED_FILES "../models/heyretro.tflite")
