# Hardware — Freenove ESP32-S3 Display 2.8" (FNK0104A / FNK0104B)

Cada dato de esta página se ha comprobado contra el repositorio oficial
[Freenove/Freenove_ESP32_S3_Display](https://github.com/Freenove/Freenove_ESP32_S3_Display).
La copia revisada corresponde a su último commit, del 2026-08-10. Los pines del firmware están en
[`include/board_config.h`](../include/board_config.h), que es la única fuente de verdad en el código.

## Fuentes

| Id | Archivo del repo oficial | Qué aporta |
|---|---|---|
| S1 | `Libraries/FNK0104AB/TFT_eSPI_Setups_v1.3.zip` → `FNK0104AB_2.8_240x320_ILI9341.h` | Pines del LCD, driver, color e inversión, SPI |
| S2 | `Tutorial_No_Touch/Sketches/Sketch_03.1_Button_RGB` | LED RGB y botón BOOT |
| S3 | `Tutorial_No_Touch/Sketches/Sketch_05.1_Battery_Voltage` | ADC de batería |
| S4 | `Tutorial_No_Touch/Sketches/Sketch_06.1_SDMMC_Test` | Pines de SD_MMC y frecuencia |
| S5 | `Tutorial_No_Touch/Sketches/Sketch_07.1_Music` (`.ino`, `es8311.h/.cpp`) | I2S, I2C, amplificador, driver ES8311 |
| S6 | `Tutorial_With_Touch/Sketches/Sketch_11.1_Touch` | RST e INT del táctil |
| S7 | `Libraries/FNK0104AB/FT6336U_v1.0.2.zip` | Dirección y registros del táctil |
| S8 | `Datasheet/ES3C28P_ES2N28P_Specification_V1.0.pdf` (LCDWIKI CR2025-MI6872, 2025-06-14) | Pines (§4.2), consumo (§3.6), medidas (§3.4), peso (§3.7) |
| S9 | `Schematic/2.8inch_ESP32-S3_Display_Schematic.pdf` (2025-06-11) | Componentes reales, pull-ups, divisores, conectores |

## Mapa de pines verificado

| Función | GPIO / dato | Fuente |
|---|---|---|
| LCD ILI9341V por SPI | MOSI 11, SCLK 12, MISO 13, CS 10, DC 46 | S1, S8, S9 |
| Reset del LCD | Compartido con CHIP_PU/EN, sin GPIO (`TFT_RST -1`) | S1, S8, S9 |
| Ajustes del LCD | `ILI9341_2_DRIVER`, `TFT_BGR`, `TFT_INVERSION_ON`, 240x320, 40 MHz, `USE_HSPI_PORT` | S1 |
| Retroiluminación | GPIO45 a la puerta del BSS138 (Q4), que conmuta en el lado bajo: HIGH = encendida, admite PWM | S1, S8, S9 |
| microSD (SD_MMC, 4 bits) | CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47; pull-ups de 10K en la placa | S4, S5, S8, S9 |
| Bus I2C compartido | SDA 16, SCL 15, 400 kHz; pull-ups de 4.7K (R29, R30) | S5, S6, S8, S9 |
| Códec ES8311 | I2C 0x18 (CE a nivel bajo) | S5, S9 |
| I2S | MCLK 4, BCLK 5, WS 7, DOUT (ESP→códec) 8, DIN (códec→ESP) 6 | S5, S9 |
| Amplificador | **SC8002B** (U6); habilitación en GPIO1, **LOW = encendido** | S5, S8, S9 |
| Táctil (solo B) | FT6336U/G en I2C 0x38; RST 18 e INT 17, con pull-ups de 10K | S6, S7, S8, S9 |
| Conector FPC del táctil (P1) | RST, INT, SDA, SCL, VDD, GND | S9 |
| LED RGB | WS2812 (XL-5050RGBC) en GPIO42 | S2, S8, S9 |
| Botón BOOT | GPIO0, pull-up de 10K (R10), LOW al pulsar | S2, S8, S9 |
| Batería | ADC en GPIO9, divisor 200K/200K → V = mV × 2; cargador TP4054 (~290 mA reales) | S3, S8, S9 |
| Conector de expansión | 4 pines de 1.25 mm: GPIO2, GPIO3, GPIO14, GPIO21. **Sin GND ni 3V3. Sin pull-ups** | S8, S9 |
| USB-C | USB nativo del S3 (GPIO19/20), sin puente USB-serie → `ARDUINO_USB_CDC_ON_BOOT=1` | S8, S9 |
| Chip y memoria | **ESP32-S3R8** (8 MB de PSRAM OPI integrada) + flash 25VQ128 (16 MB, QSPI) | S8, S9 |
| Altavoz | JST de 1.25 mm, 2 pines; máximo 1.5 W a 8 Ω o 2 W a 4 Ω | S8 |
| Consumo | 140 mA solo la pantalla; 560 mA con pantalla, altavoz y carga | S8 |
| Módulo | 86 × 50 mm; 5.6 mm con táctil y 4.4 mm sin él | S8 |

## Conflictos entre fuentes y decisión tomada

1. **Modelo del amplificador.** El brief decía FM8002E; el esquema (S9) muestra U6 = SC8002B. Ambos son de la
   familia 8002, con la entrada SHUTDOWN activa en alto, y la especificación (S8) dice "low level enable".
   **Decisión:** GPIO1 en LOW = amplificador encendido, igual que el sketch 07.1.
2. **Controlador táctil.** La librería y el sketch de Freenove (S6, S7) dicen FT6336U; la especificación LCDWIKI (S8)
   dice FT6336G. Los dos responden en 0x38 (el esquema lo confirma) y comparten el mapa de registros que usamos
   (TD_STATUS en 0x02 y punto 1 en 0x03–0x06). **Decisión:** lector propio por registros, válido para ambos.
3. **Dirección de datos de I2S.** La tabla de la especificación (S8) llama "output" a GPIO6 e "input" a GPIO8
   porque habla desde el punto de vista del códec. **Decisión:** manda el sketch (S5): DOUT (ESP→códec) = 8 y DIN = 6.
4. **Versión del core en el sketch 07.1.** La versión actual usa el core Arduino **3.x** (`ESP_I2S.h`,
   `I2SClass`, ESP32-audioI2S). Su `es8311_codec_init()` configura 16 kHz con MCLK ×384 y aborta con
   `ESP_ERROR_CHECK` si algo falla. **Decisión:** usamos el core 2.0.17 con I2S legacy y llamamos a
   `es8311_create` + `es8311_init` con MCLK = 11.2896 MHz (44100 × 256). La fila `{11289600, 44100}` existe en la
   tabla `coeff_div` del driver, así que es una configuración soportada. El S3 no tiene APLL: MCLK sale de un
   divisor fraccional, con algo de jitter, pero BCLK y LRCK derivan del mismo MCLK y la relación ×256 se mantiene.
   **Verificado el 2026-09-29: el pitido y el tono de 440 Hz suenan limpios por el altavoz.**
5. **Puerto SPI del LCD.** El setup de TFT_eSPI (S1) usa `USE_HSPI_PORT` (SPI3); nosotros usamos Arduino_GFX con
   `FSPI` (SPI2). En el S3 ambos periféricos llegan a cualquier pin por la matriz GPIO y la SD usa el periférico SDMMC
   aparte, así que no hay conflicto. **REQUIRES HARDWARE TEST.**
6. **Nombre del producto.** La especificación usa la nomenclatura de LCDWIKI: ES3C28P (con táctil) = FNK0104B y
   ES3N28P (sin táctil) = FNK0104A. El nombre del archivo PDF dice "ES2N28P", que es una errata.
7. **Librería FT6336U de Freenove.** No se usa: `readByte()` hace `delay(10)` en cada byte, `begin()` bloquea 500 ms
   y `scan()` tarda más de 100 ms, lo cual es incompatible con un bucle que no bloquea. Se sustituye por una lectura en
   ráfaga de 5 bytes desde 0x02 (fase 3).

## Variante A o B: cómo saberlo

La serigrafía "Capacitive Touch" aparece en las fotos de las dos variantes, porque la PCB es la misma. Las diferencias físicas son:

- La B tiene un cable plano naranja con chip enchufado en el FPC de 6 pines (P1), junto al conector I2C.
- Grosor: 5.6 mm con táctil y 4.4 mm sin él. Se puede medir con un calibre (S8 §3.4).
- Peso con embalaje: 86 g la B y 78 g la A (S8 §3.7).

**Lo que manda es la detección en ejecución:** al arrancar se escanea el bus I2C y, si 0x38 responde, se considera
que hay táctil (fase 3).

## Mapeo de ejes del táctil

El controlador informa en su marco vertical nativo (240×320). Para la rotación 1, Freenove
(`Sketch_12.1_TFT_Touch_Draw`) usa `x = raw.y` e `y = 240 − raw.x`. En `board_config.h` eso se traduce en
`TOUCH_SWAP_XY = true`, `TOUCH_INVERT_X = false` y `TOUCH_INVERT_Y = true`. La rotación 3 invierte ambos ejes en el
código. Un táctil capacitivo no necesita calibración: si el rastro del diagnóstico va al revés, basta con cambiar
esos tres valores. No se guardan en NVS porque ninguna pantalla los modifica.

## Pines de arranque (strapping) y avisos

- **GPIO0 (BOOT):** si el mando de CANAL está conectado a BOOT y se mantiene pulsado al encender, la placa entra en modo
  descarga. No pulses CANAL mientras la enciendes.
- **GPIO45 (retroiluminación) y GPIO46 (DC del LCD):** son pines de arranque. El firmware solo los usa como salidas
  después del arranque.
- **GPIO3:** es pin de arranque (fuente de JTAG) y no se usa.

## Integración física en la carcasa

- **Carcasa tele90 v5** (modelo en Blender, no incluido): 4 teclas, LED de 3 mm, altavoz 40×28 en el
  techo, USB-C por el lateral derecho, microSD con alargador en la base trasera y una LiPo 103665 detrás de la placa.
- **Teclas:** pulsadores de 6x6 mm a GND, de izquierda a derecha CH− GPIO2, CH+ GPIO3, VOL− GPIO14, VOL+ GPIO21 (los
  cuatro pines del conector de expansión). Se leen con `INPUT_PULLUP` interno (~45K): el conector no tiene pull-ups.
- **LED:** TXD del conector UART (GPIO43, libre porque el log va por USB) con ~1K en serie.
- El conector de expansión no tiene GND. Sácalo del conector I2C (3V3, GND, IO15, IO16) o del UART (RXD, TXD, GND, 5V).
- **microSD con alargador:** un cable alarga las pistas de 40 MHz. La tele se recupera sola de los timeouts
  (`App::recoverSd`), pero con la carcasa montada hay que repetir la prueba `D … scan` (T18.2) y, si falla mucho más, bajar
  a 20 MHz.
- Con la placa suelta, BOOT (GPIO0) hace de CH+ con clic, doble clic y mantener.
- La pantalla queda hundida unos 7 mm tras una ventana de 55.5 × 41.5 mm, así que el táctil es incómodo en los bordes:
  los mandos deben poder hacerlo todo.

## Verificado en la placa del usuario (2026-09-29)

- Chip ESP32-S3 rev v0.2, MAC 44:1b:f6:ce:69:24. USB-Serial-JTAG 303A:1001 en `/dev/cu.usbmodem101`.
- PSRAM OPI detectada: 8189 KB.
- LCD con Arduino_GFX, SPI a 40 MHz y rotación 1: horizontal correcto. El orden BGR y la inversión (IPS) están bien:
  el verde sale verde.
- Flash física (ID JEDEC): 16384 KB.
- Escaneo I2C: solo 0x18 (ES8311). 0x38 no responde, ni siquiera tras un pulso de reset en `CTP_RST` (GPIO18).
- Batería sin LiPo conectada: 4094 mV. Es la salida del cargador TP4054, no una medida de batería.
- LiPo real (2026-10-01): YUNIQUE 103665, 3,7 V 3000 mAh con JST 1.25, la del hueco de la tele90 v9. Conector JP1
  del esquema: pin 1 = BAT+, pin 2 = GND. Recién llegada y sin USB, la tele funciona de la batería y marca 61 %
  (~3,89 V). Carga a ~300 mA (R_PROG 3,3 kΩ): unas 10–11 h de vacía a llena.
- Vídeo: 24,0 fps estables con un capítulo real. Decodificar (JPEGDEC con SIMD del S3) cuesta 13–25 ms por fotograma
  y dibujar por SPI a 40 MHz 38 ms, de un presupuesto de 41,7 ms: **el SPI es el cuello de botella** y deja
  ~4 ms de margen. A 80 MHz (`PAUTV_SPI_HZ`) dibujar baja a 22 ms pero la imagen sale mal (probado 2026-10-01):
  se queda en 40 MHz y el vídeo local va a 20 fps (50 ms por fotograma).
- Leer de la SD mientras se reproduce va a ~1 MB/s (3,85 MB/s sin vídeo) y casi todo es esperar a la tarjeta: el
  vídeo local se lee por delante en otra tarea (`SdPrefetch`, 2026-10-01). Con q5 a 20 fps y el dithering: 20 fps,
  0 descartes; sin leer por delante, 15–17 fps.
- Audio: el ES8311 arranca a 44.1 kHz con MCLK ×256, el SC8002B se activa con GPIO1 en LOW y el tono de 440 Hz suena
  limpio por el altavoz de 40×28 mm.
- microSD Philips SDHC de 32 GB (29817 MB útiles) en FAT32/MBR: monta a 40 MHz en 4 bits.
- Wi-Fi a 2.4 GHz (red DIGIFIBRA-UTC3), entre −53 y −57 dBm con la placa fuera de la carcasa. Conecta en 2,9–5,0 s;
  el primer arranque tras grabar superó los 6 s (calibración inicial de la radio), por eso el primer intento tiene
  un tope de 8 s.
- **Variante confirmada: FNK0104A (sin táctil).** El conector FPC del táctil está vacío (comprobado a la vista) y 0x38
  sigue sin responder tras un pulso de reset en `CTP_RST`. Todo se maneja con los mandos y BOOT.

## Pendiente de verificar con la placa (REQUIRES HARDWARE TEST)

- Pulsadores de CANAL (GPIO2) y VOLUMEN (GPIO14) cableados dentro de la carcasa. Hasta ahora solo se ha usado BOOT.
- Rango útil del volumen (`CODEC_VOLUME_MIN/MAX` = 45..85) con el altavoz montado detrás de la rejilla.
- Curva del porcentaje (`power/Battery.h`, genérica) contra una descarga real, y autonomía (~0,3 A: unas 8–10 h).
- Táctil y mapeo de ejes en rotación 1 y 3: solo con una FNK0104B.
