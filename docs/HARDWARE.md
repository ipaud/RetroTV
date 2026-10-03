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
| Batería | ADC en GPIO9, divisor 200K/200K → V = mV × 2; cargador TP4054, ~300 mA nominales (R_PROG 3,3 kΩ; corriente no medida) | S3, S8, S9 |
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

- **GPIO0 (BOOT):** mantener BOOT pulsado al encender o al enchufar mete la placa en modo descarga. Ninguna tecla de
  la carcasa va a GPIO0.
- **GPIO45 (retroiluminación) y GPIO46 (DC del LCD):** son pines de arranque. El firmware solo los usa como salidas
  después del arranque.
- **GPIO3 (tecla CH+):** es pin de arranque (fuente de JTAG). Tenerlo pulsado al encender solo cambia de dónde sale el
  JTAG, que la tele no usa.

## Integración física en la carcasa

**Carcasa vigente: tele90 v9**, diseño propio modelado en Blender por script. Mide **92,5 × 85 × 59 mm** (ancho ×
alto × fondo, sin las patas) según su modelo. Los archivos del modelo **no están en este repositorio** y la carcasa no
se distribuye como imprimible hasta publicar sus STL revisados.

- **Piezas:** frontal, cuerpo y tapa trasera, 4 teclas, soporte de pulsadores y 4 patas.
- **Dentro:** la placa tras la ventana, el altavoz de 40×28 mm bajo la rejilla del techo y la LiPo **detrás de la placa**,
  en el cuerpo.
- **Por fuera:** 4 teclas con su símbolo grabado, LED de 3 mm (piloto) y USB-C directo por el lateral derecho.
- **tele90 v10:** la v9 en dos piezas (frontal y un cuerpo con la trasera cerrada, 8 imanes), mismas medidas.
  Tiene una **variante sin botones**: frontal liso con solo el LED, que se maneja con el mando web y las palmadas.
  Esa variante necesita el firmware `voice_nokeys` (ver abajo). Archivos fuera del repo, como los de la v9.
- **Versiones anteriores** (v5 y las de 97 y 112 mm de fondo): superadas. Sus cotas, la ventana de pantalla, la posición
  de la batería y el alargador de microSD **no valen para la v9**.

**Sin botones (`PAUTV_HAS_KEYS 0`, entorno `voice_nokeys`).** En reposo profundo solo despierta una tecla o BOOT, y
en esa carcasa no hay teclas y BOOT queda dentro. Para que nunca se quede apagada sin salida:
- ⏻ del mando web (y tres palmadas) apagan a **STANDBY VOZ**, diga lo que diga APAGADO; tres palmadas la encienden.
- Si STANDBY VOZ no puede escuchar (micrófono o palmadas desactivados, o un firmware sin voz), ⏻ apaga a
  **STANDBY WI-FI**: pantalla, sonido y LED apagados, pero la Wi-Fi y el mando web siguen. ⏻ (o cualquier tecla del
  mando) la vuelve a encender; el mando dice APAGADA mientras tanto. Consumo sin medir.
- Solo si tampoco hay Wi-Fi la tele **no se apaga** y lo dice en pantalla ("NO SE APAGA / SIN WI-FI NI PALMADAS").
- Con la batería agotada duerme igual, pero **se despierta sola cada 5 min** (`FLAT_CHECK_S`), mira la batería y, si
  lee 3,7 V o más (`FLAT_RESUME_MV`; con el USB-C enchufado lee ~4,1 V), arranca. Si no, vuelve a dormir sin
  encender nada. Es decir: al cargarla, se enciende sola en unos minutos.

Cableado (el mismo en cualquier carcasa):

- **Teclas:** pulsadores de 6x6 mm a GND, de izquierda a derecha CH− GPIO2, CH+ GPIO3, VOL− GPIO14, VOL+ GPIO21 (los
  cuatro pines del conector de expansión). Se leen con `INPUT_PULLUP` interno (~45K): el conector no tiene pull-ups.
- **LED:** TXD del conector UART (GPIO43, libre porque el log va por USB) con ~1K en serie.
- El conector de expansión no tiene GND. Sácalo del conector I2C (3V3, GND, IO15, IO16) o del UART (RXD, TXD, GND, 5V).
- Con la placa suelta, BOOT (GPIO0): clic = canal siguiente, doble clic = anterior, mantener = ajustes.
- **Si se alarga la microSD** con un cable (el modelo v9 no describe ninguno), las pistas de 40 MHz se alargan: la tele se
  recupera sola de los timeouts (`App::recoverSd`), pero hay que repetir la prueba `D … scan` (T18.2) y, si falla
  mucho más, bajar a 20 MHz.

## Batería

| | Dato | Origen |
|---|---|---|
| Celda | **YUNIQUE 103665**, LiPo de una celda (1S), **3,7 V nominales**, **3000 mAh anunciados** por el vendedor | etiqueta / anuncio (capacidad no medida) |
| Medidas | 10 × 36 × 65 mm (el nombre 103665) | formato estándar |
| Conector | JST 1.25 de 2 pines; en la placa (JP1) pin 1 = BAT+ y pin 2 = GND | esquema S9 |
| Carga | TP4054 de la placa, ~300 mA nominales (R_PROG 3,3 kΩ) | esquema S9 (no medida) |

> [!WARNING]
> **Comprueba la polaridad antes de conectar cualquier batería.** Las LiPo con JST 1.25 no siguen un estándar: el rojo
> y el negro a veces vienen al revés. Al revés, el cargador y la placa se pueden dañar. Con un polímetro: el positivo
> de la batería tiene que llegar al pin 1 (BAT+) de JP1.

**Medido en la placa** (lecturas del ADC, `/api/state` o `[BOOT] battery`):
- Recién llegada, sin USB: 61 % (~3,89 V), 2026-10-01.
- Carga por el USB del Mac con la tele encendida: del 81 % al 99 % en unas 2 h 45 min; llena se queda en ~4,19 V
  leídos, 2026-10-02.
- Sin batería conectada, la lectura es la salida del cargador: ~4,1 V (~90 %).

**Estimado, no medido:**
- Carga completa de vacía a llena: unas 10–11 h (3000 mAh / ~300 mA).
- Autonomía viendo la tele: unas 8–10 h, suponiendo ~0,3 A. Sin medir: `tools/battery_log.py` registra una descarga
  completa por Wi-Fi.
- STANDBY VOZ: ~40 mA (30–50), de una noche en standby (72 → 64 % en ~6 h): unos 3 días desde llena.
- Reposo profundo (AHORRO MAX): sin medir.
- El porcentaje sale de una curva genérica de LiPo (`src/power/Battery.h`), no de una descarga real de esta celda.

## Verificado en la placa del usuario (2026-09-29)

- Chip ESP32-S3 rev v0.2. USB-Serial-JTAG 303A:1001 en `/dev/cu.usbmodem101`.
- PSRAM OPI detectada: 8189 KB.
- LCD con Arduino_GFX, SPI a 40 MHz y rotación 1: horizontal correcto. El orden BGR y la inversión (IPS) están bien:
  el verde sale verde.
- Flash física (ID JEDEC): 16384 KB.
- Escaneo I2C: solo 0x18 (ES8311). 0x38 no responde, ni siquiera tras un pulso de reset en `CTP_RST` (GPIO18).
- Batería sin LiPo conectada: 4094 mV. Es la salida del cargador TP4054, no una medida de batería.
- LiPo real (2026-10-01): YUNIQUE 103665 (ver [Batería](#batería)). Recién llegada y sin USB, la tele funciona de la
  batería y marca 61 % (~3,89 V).
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
- Wi-Fi a 2.4 GHz (la red de casa), entre −53 y −57 dBm con la placa fuera de la carcasa. Conecta en 2,9–5,0 s;
  el primer arranque tras grabar superó los 6 s (calibración inicial de la radio), por eso el primer intento tiene
  un tope de 8 s.
- **Variante confirmada: FNK0104A (sin táctil).** El conector FPC del táctil está vacío (comprobado a la vista) y 0x38
  sigue sin responder tras un pulso de reset en `CTP_RST`. Todo se maneja con los mandos y BOOT.

## Pendiente de verificar con la placa (REQUIRES HARDWARE TEST)

- **Las cuatro teclas montadas** (GPIO2, 3, 14 y 21 a GND, T19.2) y encender con ellas desde el reposo (T22.3). Hasta
  ahora se han usado BOOT, el mando web y el puerto serie; la lógica de las teclas solo está probada en el ordenador.
- **El LED del frontal completo** (T19.3): encendido mientras funciona y apagado un instante con cada orden. En STANDBY
  VOZ el usuario vio su destello (TV10).
- Rango útil del volumen (`CODEC_VOLUME_MIN/MAX` = 45..85) con el altavoz montado detrás de la rejilla.
- Autonomía real y curva del porcentaje (`power/Battery.h`, genérica) con una descarga completa
  (`tools/battery_log.py`); consumo con un medidor en reproducción, STANDBY VOZ y reposo (T22.4, TV12).
- Batería agotada estando en STANDBY VOZ (TV13b).
- Todo dentro de la carcasa v9 montada: Wi-Fi, temperatura y SD (las medidas de arriba son con la placa fuera).
- Táctil y mapeo de ejes en rotación 1 y 3: solo con una FNK0104B.
