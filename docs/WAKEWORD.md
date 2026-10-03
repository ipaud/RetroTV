# Wake word «Hola ESP» en STANDBY VOZ (prueba experimental)

Experimental: entornos `voice_ww` y `voice_nokeys_ww` más la app `standby/`. Objetivo: con la tele en STANDBY VOZ, decir **«Hola ESP»** la enciende. Las palmadas siguen
funcionando. El firmware habitual no cambia y, con la tele encendida, nada escucha palabras.

## Lo que hay que saber antes

| Dato | Comprobado (2026-10-03) |
|---|---|
| Modelo «Hola ESP» | `wn10_es_holaesp` (WakeNet10), en ESP-SR **2.5.5** (registro de componentes, 24-09-2026). Unos 811 KB |
| ESP-SR 2.5.5 necesita | **ESP-IDF ≥ 5.0**, esp-dl ~3.3.12 y dl_fft (su `idf_component.yml`) |
| Firmware de la tele | Arduino 2.0.17 = **ESP-IDF 4.4** (`espressif32@6.9.0`). No puede enlazar ESP-SR 2.5.5 |
| Modelo cargado de | una partición de datos de la flash (`esp_srmodel_init(label)`), formato `srmodels.bin` |
| MultiNet (comandos) | inglés y chino. Fuera de esta prueba; «Hey Retro» tampoco |

La investigación anterior (fase M de [VOICE.md](VOICE.md)) concluía que no había «HEY RETRO» ni comandos en español.
Sigue siendo cierto para «HEY RETRO» y los comandos, pero desde ESP-SR 2.5.5 sí hay una palabra en español de serie:
«Hola ESP».

## Auditoría de la placa y del firmware

| | |
|---|---|
| Placa | Freenove FNK0104A (sin táctil): ESP32-S3R8, 8 MB de PSRAM octal, 16 MB de flash QSPI |
| Audio | códec ES8311 (I2C 0x18) con micrófono MEMS analógico en MIC1; I2S MCLK 4, BCLK 5, WS 7, DOUT 8, DIN 6 |
| Firmware | PlatformIO Core 6.1.19, `espressif32@6.9.0`, Arduino 2.0.17 (ESP-IDF 4.4), C++17 |
| I2S | driver legacy, full duplex en `I2S_NUM_0`: **44,1 kHz**, 16 bits, estéreo, MCLK ×256; el micro va en el canal izquierdo |
| Memoria en STANDBY VOZ (log) | heap interno 88–98 KB libres, bloque mayor 45–63 KB, PSRAM 6104 KB libres |
| Particiones (`default_16MB.csv`) | nvs 20 KB · otadata 8 KB · **app0** 6,25 MB · **app1** 6,25 MB · **spiffs** 3,375 MB · coredump 64 KB |
| Uso | firmware ~1,25 MB en `app0`. **`app1` y `spiffs` no se usan**: ni OTA, ni SPIFFS/LittleFS/FFat en el código. Los medios van en la microSD |
| STANDBY VOZ hoy | `enterStandby()` → `powerDown()` (CRT, pantalla, Wi-Fi, LED, amplificador) → `voiceStandby()`: CPU a 80 MHz, captura a 44,1 kHz, detector de palmadas, LED, batería, teclas, serie `W`; despertar = `ESP.restart()` |

## Diseño: una app de standby en `app1`, sin migrar el firmware

Migrar todo el firmware a Arduino 3.x / ESP-IDF 5 tocaría el vídeo, el I2S, la SD, la Wi-Fi y el mando a la vez. En
vez de eso:

```
firmware de la tele (app0, Arduino 2.0.17, sin cambios en el uso normal)
   │  STANDBY VOZ con PAUTV_WAKEWORD_ENABLED=1
   │  1. ¿hay en app1 una app "retrotv_standby" válida?  no → STANDBY VOZ de siempre (palmadas)
   │  2. sí → app1 pasa a ser la partición de arranque → reinicio
   ▼
app de standby (app1, ESP-IDF 5.4.1 + ESP-SR 2.5.5, proyecto standby/)
   │  lo primero: app0 vuelve a ser la de arranque (cualquier reinicio vuelve a la tele)
   │  pantalla, luz, LED y altavoz apagados; Wi-Fi nunca se enciende
   │  ES8311 + I2S a 16 kHz, 16 bits, mono  →  WakeNet «Hola ESP»  +  detector de palmadas
   │  despertar (palabra, tres palmadas con silencio, tecla, serie W) → reinicio → app0
   │  batería agotada → reposo profundo (como el firmware: teclas, y temporizador sin teclas)
   ▼
firmware de la tele: arranque normal, intro y último canal
```

- **Por qué es la integración más pequeña:** el firmware solo gana una función de ~25 líneas (cambiar la partición
  de arranque) y un entorno nuevo. El vídeo, el audio a 44,1 kHz, las palmadas y el mando no se tocan. WakeNet no
  existe en el firmware: no puede ejecutarse durante el vídeo.
- **16 kHz:** la app de standby configura el ES8311 y el I2S a 16 kHz desde cero (MCLK 4,096 MHz = 256 × 16 kHz,
  fila que el driver del códec ya trae). No hay remuestreo ni captura simultánea: el firmware de la tele ya no corre.
- **Palmadas:** la app usa el mismo `ClapDetector` y las mismas reglas de STANDBY VOZ (`VoiceStandby.h`, tres
  palmadas con silencio alrededor) del firmware, a 16 kHz. Los dos detectores reciben los mismos bloques.
- **Ajustes:** lee los del firmware en NVS (`pautv`: palmadas, sensibilidad, LED ESCUCHA) y `ww_keys` (si la carcasa
  tiene teclas), que escribe el firmware antes de ceder el control.
- **Seguridad:** la app de standby se borra a sí misma de la partición de arranque al empezar. Si se cuelga, el
  watchdog o una tecla (reinicio) devuelven la tele. Si `app1` está vacía o no es la app de standby, el firmware no
  cambia nada y usa el STANDBY VOZ de siempre.

### Efectos y cambios

| Qué | Efecto |
|---|---|
| Plataforma nueva solo para `standby/` | `espressif32@6.11.0` (ESP-IDF 5.4.1), ya instalada. El firmware sigue en `espressif32@6.9.0` |
| `app1` | pasa a contener la app de standby. No había nada (sin OTA) |
| `spiffs` (3,375 MB) | pasa a contener `srmodels.bin` (el modelo, ~0,8 MB). No había nada; no se cambia la tabla de particiones |
| Bootloader | se queda el de Arduino 2.0.17 (ESP-IDF 4.4), que arranca la app de ESP-IDF 5.4.1 (la compatibilidad hacia delante del bootloader es la que usa OTA). A comprobar en la placa |
| `otadata` | se escribe dos veces por standby (entrar y despertar): unos pocos sectores de flash |
| Firmware habitual (`pautv`, `voice`, `voice_nokeys`) | igual que antes. `PAUTV_WAKEWORD_ENABLED` solo se activa en `voice_ww` / `voice_nokeys_ww` |
| Cambio de app | en cada traspaso y despertar hay unos cientos de ms de arranque con los pines en su estado de reset (amplificador sin controlar, LED en el pin de UART0): igual que el despertar actual, a comprobar de oído |
| Consumo | WakeNet necesita CPU continua; seguramente más que los ~40 mA estimados de STANDBY VOZ. A medir |

### Cómo volver atrás

- Flashear el firmware de siempre (`pio run -e voice_nokeys -t upload`): no mira `app1`, la app de standby se queda
  ahí sin usarse.
- Para borrarla: `esptool.py --chip esp32s3 erase_region 0x650000 0x640000` (app1) y `0xc90000 0x360000` (modelo).

## Compilar y flashear

| Pieza | Comando | Dónde va |
|---|---|---|
| Firmware de la tele con el traspaso | `pio run -e voice_ww -t upload` (con teclas) o `pio run -e voice_nokeys_ww -t upload` (v10 sin teclas) | `app0`, como siempre |
| App de standby + modelo | `tools/standby_flash.sh [puerto]` (por defecto `/dev/cu.usbmodem101`) | `app1` 0x650000 y `spiffs` 0xc90000 |
| Solo compilar y empaquetar | `tools/standby_flash.sh --build` | nada |

`tools/standby_flash.sh` compila `standby/` (`pio run -d standby`), empaqueta el modelo con el `movemodel.py` de
ESP-SR (`srmodels.bin`) y escribe esas dos particiones con esptool, sin tocar bootloader, tabla de particiones,
`otadata`, NVS ni `app0`. Antes comprueba que `standby/partitions.csv` es idéntica a la `default_16MB.csv` del
firmware y que cada binario cabe en su partición.

Tamaños (2026-10-03): app de standby **674 KB** (de 6,25 MB), `srmodels.bin` **384 KB** (de 3,375 MB, solo
`wn10_es_holaesp`). El firmware `voice_ww` ocupa 1 261 089 bytes, 1 048 más que `voice`.

Versiones fijadas: `espressif32@6.11.0` (ESP-IDF 5.4.1), `tool-cmake@3.30.2` (esp-dl usa `cmake_language`, que la
CMake 3.16 de la plataforma no tiene), esp-sr 2.5.5, esp-dl 3.3.13, dl_fft 0.8.0 (`standby/dependencies.lock`).
`standby/managed_components/` y `standby/sdkconfig.standby` se generan al compilar y no se suben.

### Qué hace cada parte

- **Firmware (`PAUTV_WAKEWORD_ENABLED=1`):** al entrar en STANDBY VOZ, tras soltar la tecla, `handOverToStandbyApp()`
  mira si en `app1` hay una app con `project_name` `retrotv_standby`. Si no, sigue con el STANDBY VOZ de siempre.
  Si sí, guarda `ww_keys` en NVS (si la carcasa tiene teclas), pone `app1` como arranque y reinicia.
- **App de standby (`standby/src/main.cpp`):** apaga luz, LED y amplificador; vuelve a poner `app0` como arranque;
  lee los ajustes (solo lectura, nunca borra NVS); arranca I2S (driver nuevo) a 16 kHz y el ES8311 con las
  mismas ganancias de micro que el firmware; carga WakeNet desde `spiffs`. Cada bloque de 512 muestras (32 ms) va
  a WakeNet y al detector de palmadas y se sobrescribe con el siguiente: no se guarda audio.
- **Vuelta a la tele:** lo primero que hace la app es poner `app0` como arranque (3 intentos) y lo comprueba otra
  vez antes de cada reinicio. Si no lo consigue, no reinicia (volvería a la app de standby) y lo dice en el log.
- **Guardas:** las detecciones de los primeros 1,5 s se ignoran (el ruido del propio cambio); al detectar, la app
  reinicia, así que no hay disparos repetidos. Una tecla que siga pulsada al empezar no cuenta hasta soltarla.
  Si falla el audio, vuelve a la tele; si falta el modelo, sigue solo con palmadas.
- **Serie (USB):** `W` despierta, `C` cambia la CPU 80 → 160 → 240 MHz (para medir la carga de WakeNet), `T` hace que «Hola ESP» solo cuente y no despierte (pruebas de aciertos y falsos positivos). Ojo: `F` en la tele es el reposo de batería agotada.

### Registro (serie, 115200)

```
[STANDBY] RETROTV standby app <commit> (ESP-IDF v5.4.1): «Hola ESP» and claps
[STANDBY] next boot: the TV (ESP_OK)
[STANDBY] memory before WakeNet / with WakeNet: internal … KB free (largest block … KB), psram … KB free
[STANDBY] WakeNet: wn10_es_holaesp ready in … ms (chunk 512 samples = 32 ms, mode 90)
[STANDBY] stats: 240 MHz, WakeNet … us avg / … us max per 32000 us chunk (load …%), read errors …, floor … dB, peaks L … R …, battery … mV
[STANDBY] «Hola ESP» detected (detect … us, … ms after start)
[STANDBY] wake: «Hola ESP», … s in standby: switching the TV on
```

## Resultados

Solo lo medido en la placa. Lo demás queda **pendiente**.

| Prueba | Resultado |
|---|---|
| Compilan `pautv`, `voice`, `voice_nokeys`, `voice_ww`, `voice_nokeys_ww` y `standby` | sí (2026-10-03) |
| Tests de host | 692 comprobaciones, 0 fallos (2026-10-03) |
| Traspaso del firmware a la app de standby | **sí** (2026-10-03): `handing over to the standby app 87831d0`, reinicio `RTC_SW_CPU_RST` |
| El bootloader de ESP-IDF 4.4 arranca la app de ESP-IDF 5.4.1 | **sí** (2026-10-03). Los primeros intentos se colgaban: la compilación había quedado con la caché de instrucciones a 32 KB pero la IRAM enlazada para 16 KB (PlatformIO no regenera `memory.ld` al cambiar `sdkconfig.defaults`), y la app pisaba su propio código al configurar la caché. Visto con OpenOCD por el JTAG del USB (doble excepción tras `rom_config_instruction_cache_mode`). Con una compilación limpia arranca; `standby_flash.sh` la limpia sola si cambia `sdkconfig.defaults` |
| Del traspaso a escuchar | 1,1 s (reinicio, bootloader, PSRAM, códec, modelo) |
| Vuelta a la tele si la app de standby falla | **sí**: el bootloader de Arduino tiene la marcha atrás de OTA activada; una app que no se confirma y se reinicia queda ABORTED y arranca `app0` |
| Memoria con WakeNet | interna 257 KB libres (bloque mayor 164 KB), PSRAM 7798 KB libres. WakeNet ocupa ~12 KB internos y ~390 KB de PSRAM; carga en 121 ms |
| Carga de CPU de WakeNet por bloque de 32 ms | 240 MHz: 2,95 ms (9 %), 160 MHz: 3,55 ms (11 %), 80 MHz: 5,4 ms (16 %, máximo 6,3 ms). Sin errores de lectura de audio. Se queda a **80 MHz**, como el STANDBY VOZ de siempre |
| Despertar por serie `W` | reinicio limpio a la tele (`reset reason 3`), último canal |
| Aciertos «Hola ESP» (modo `T`, solo contar) | 17 detecciones de unas 20 frases (10 a 20–30 cm y 10 a 1 m, del usuario), una cada 4–5 s, voz a −28/−37 dBFS de pico. El log no separa las dos distancias: no hubo pausa al cambiar de sitio. 0 detecciones dobles por frase |
| «Hola ESP» enciende la tele | 2 de 2 a la primera (1 m y 20–30 cm, 2026-10-03) |
| Tiempo de encendido | de la detección al arranque de la tele 0,4 s; intro a los 3,4 s; canal en pantalla a los 8,6 s (la intro dura ~5 s). El tiempo desde el final de la palabra hasta la detección no se puede medir con este log |
| Estabilidad | 35 min seguidos en STANDBY VOZ (2026-10-03): sin errores de lectura de audio, memoria constante (257 KB internos libres de principio a fin), carga estable 16 % a 80 MHz |
| Falsos positivos en habitación tranquila | 0 en 35 min (suelo −62 a −79 dBFS, pico máximo −38 dBFS) |
| Falsos positivos (horas, con audio de series) | pendiente |
| Palmadas a la vez que WakeNet | funcionan con el mismo detector a 16 kHz: 2 de 3 secuencias encendieron (la primera perdió una palmada; motivo no registrado en esa versión, ahora sí se registra). Encendido ~1,9 s después de la primera palmada (cierre de secuencia 0,7 s + silencio después 0,5 s). Picos −14/−24 dB, brillo 22–33 % |
| Despertar limpio | sí, 4 de 4 (2026-10-03): sin chasquidos ni destellos (usuario), intro y último canal, `reset reason 3` |
| Consumo en standby con y sin WakeNet | **pendiente: no hay medidor** (ver la estimación abajo) |

## ¿Es razonable con la batería YUNIQUE de 3000 mAh?

| | Dato | Cómo se sabe |
|---|---|---|
| STANDBY VOZ de siempre (palmadas) | ~40 mA (30–50) | **estimado** por la batería: 72 → 64 % en ~6 h de noche (TV12), sin medidor |
| Carga de WakeNet | 16 % de un núcleo a 80 MHz, 5,4 ms por bloque de 32 ms | **medido** en el log |
| Lo que cambia frente a STANDBY VOZ | misma CPU (80 MHz), mismo códec y micro, Wi-Fi y pantalla apagadas; I2S a 16 kHz en vez de 44,1; además, WakeNet: cálculo y lecturas de PSRAM | **medido** (configuración) |
| Consumo con WakeNet | ~42–50 mA: unos pocos mA más que STANDBY VOZ por el 16 % de CPU y la PSRAM | **estimado**, sin medir |
| Autonomía en standby | 3000 mAh / 42–50 mA ≈ 60–70 h, unos **2,5–3 días** desde llena (STANDBY VOZ: ~3 días) | **estimado** |

Conclusión provisional: sí es razonable para dejarla en standby de un día para otro, igual que STANDBY VOZ con
palmadas; no lo es para semanas, para eso sigue AHORRO MAX (y la batería agotada acaba siempre en reposo
profundo). Para confirmarlo hace falta un medidor USB, o repetir la prueba de TV12 (una noche sin USB, % de
batería antes y después) con `voice_nokeys_ww`.

## Pendiente

- Falsos positivos durante horas con audio de series cerca (modo `T`, que solo cuenta).
- Consumo medido (medidor USB) o, sin medidor, una noche sin USB comparando el % de batería.
- Aciertos por distancia por separado (con una pausa marcada entre 20–30 cm y 1 m).
- Si se sigue adelante: elegir entre DET_MODE_90 y 95 con esos datos, y decidir si «Hola ESP» se puede activar
  y desactivar desde AJUSTES > VOZ.
- «Hey Retro» y comandos en español: fuera de esta prueba.
