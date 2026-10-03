# Desarrollo

Compilar y flashear cada variante, depurar por el puerto serie, probar en la placa y en el ordenador, y qué
comprueba la integración continua. Volver al [README](../README.md).

## Requisitos

- [PlatformIO Core](https://platformio.org/) 6.x (`pio`). La primera compilación descarga `espressif32@6.9.0`
  (Arduino core 2.0.17) y las librerías de `platformio.ini`.
- Cable USB-C de datos. La placa usa el USB nativo del ESP32-S3: no hace falta driver de puente serie.
- `ffmpeg` para convertir vídeo (`brew install ffmpeg` en macOS) y `python3` para los índices `.idx` y las
  herramientas de `tools/`.
- Para la app de standby (wake word): nada más; `pio` descarga `espressif32@6.11.0` (ESP-IDF 5.4.1) y los
  componentes de `standby/src/idf_component.yml`.

## Compilar y flashear cada variante

Los entornos son los de [`platformio.ini`](../platformio.ini). Qué variante corresponde a cada montaje:
[README](../README.md#variantes-qué-montar-y-qué-firmware).

| Entorno | Para | Compilar | Flashear por USB-C |
|---|---|---|---|
| `pautv` (por defecto) | Placa suelta o carcasa con teclas, sin micrófono | `pio run -e pautv` | `pio run -e pautv -t upload` |
| `voice` | Con teclas, más micrófono: palmadas, STANDBY VOZ, grabadora | `pio run -e voice` | `pio run -e voice -t upload` |
| `voice_nokeys` | Carcasa sin botones (tele90 v10 sin teclas) | `pio run -e voice_nokeys` | `pio run -e voice_nokeys -t upload` |
| `voice_ww` | Como `voice`, y STANDBY VOZ pasa a la app de standby (wake word, experimental) | `pio run -e voice_ww` | `pio run -e voice_ww -t upload` |
| `voice_nokeys_ww` | Como `voice_nokeys`, con la app de standby | `pio run -e voice_nokeys_ww` | `pio run -e voice_nokeys_ww -t upload` |
| `standby/` (otro proyecto) | La app de standby en `app1`, con «Hola ESP» y, si hay modelo, «Hey Retro» | `pio run -d standby` o `tools/standby_flash.sh --build` | `tools/standby_flash.sh [puerto]` |

- `pio run` sin `-e` compila solo `pautv` (`default_envs`). `pio device monitor` abre el log serie a 115200.
- **App de standby:** `tools/standby_flash.sh` compila `standby/`, empaqueta el modelo de «Hola ESP» y escribe solo
  `app1` y la partición del modelo; no toca la tele (`app0`), los ajustes ni la tabla de particiones. Está escrito
  para macOS (usa `stat -f`) y su puerto por defecto es `/dev/cu.usbmodem101`. Detalle: [wake word](WAKEWORD.md).
- **«Hey Retro»:** el modelo (`standby/models/heyretro.tflite`) no está en el repositorio. Sin él, la app de standby
  compila igual y arranca solo con «Hola ESP» y palmadas ([cómo se entrena](WAKEWORD.md)).
- **Wi-Fi para desarrollo:** `include/secrets.h` (copia de `include/secrets.example.h`, ignorado por Git). Es
  opcional: sin él, el firmware compila y lee las redes de `wifi.json` en la SD.

Si la subida falla porque el puerto no aparece: mantén BOOT, pulsa RESET, suelta BOOT y vuelve a lanzar el upload.
Si dice `No serial data received`, basta con repetirlo.

## Depuración por el puerto serie

Con `PAUTV_DEBUG_STATS 1` (en `config.h`), cada 5 s durante la reproducción se imprimen fps, ms de decodificación y
de dibujo, `av_drift_ms`, `dropped_frames`, desbordamientos, KB/s de la SD, heap, PSRAM y las marcas de vida de las
tareas. Abrir el puerto reinicia la placa. Además acepta estos comandos por el monitor serie:

- `n` / `p`: canal siguiente / anterior. `+` / `-`: volumen. `x`: silencio. `o`: OSD fijo. `M`: menú. `q`: apagar
  (= mantener CH−). Hacen lo mismo que los mandos.
- Con el firmware `voice`: `v` (niveles del micrófono), `V` (pausa la captura), `R` (grabar), `Y` (mensaje de
  prueba), `E` (borrar los mensajes) y `W` (encender desde STANDBY VOZ). Detalle en [RETROTV Voice](VOICE.md).
- `F`: el apagado que provoca una batería agotada (para probar el despertar cada 5 min de la versión sin botones).
- `d`: *dithering* sí/no, para compararlo (sale en pantalla "DITHER ON/OFF"). Por defecto, apagado.
- `w`: enseña el aviso de batería, para verlo sin gastarla.
- `m`: foto de la memoria (heap, mínimo, bloque más grande, PSRAM).
- `z`: 100 cambios de canal automáticos, uno cada 1.5 s (prueba rápida de estabilidad).
- `S<segundos>` + Enter: **soak test**.
  - Cambia de canal cada N segundos; `S0` no cambia nunca, útil para medir un solo canal durante horas.
  - Cada minuto escribe una línea `[SOAK]` con memoria (actual, mínima, bloque más grande, PSRAM frente al inicio),
    `dropped_frames`, `av_drift_ms`, errores de audio y marcas de vida de las tareas (`STALLED` si alguna se para).
  - `s` lo detiene.
- `T <ruta>` + Enter: sintoniza cualquier carpeta o `.mjpeg` como canal temporal (pruebas); cualquier zapeo vuelve
  a `channels.json`. Con `T http://<servidor>:8080/channel/<n>` sintoniza un canal remoto sin tocar la SD.
- `D <s> <carpeta> [url de bench] [scan]` + Enter: lee un capítulo de la SD durante s segundos como lo hace la
  reproducción y da los KB/s; opcionalmente con la Wi-Fi recibiendo o buscando redes a la vez.
- `B <n> <url>` + Enter: caudal TCP bruto durante 10 s con n conexiones (1–4) a `/api/bench` de un RETROTV Server
  (`B 1 http://192.168.1.x:8080/api/bench`). Para la reproducción mientras mide.
- `g<página>` + Enter: en el canal de teletexto, va a esa página (`g101`, `g203`). Cada página nueva se escribe
  también en el log como texto, fila a fila.
- Mientras suena un canal remoto, una línea `[NET]`. Sale cada segundo los primeros 30 s tras sintonizar y
  después cada 5 s. Campos:
  - `network_kbps`: caudal;
  - `buffer_bytes` y `buffer_fill`: búfer de vídeo y audio;
  - `read_stalls` y `wait_ms`: veces que el reproductor encontró el búfer vacío y cuánto esperó;
  - `timeouts` y `reconnects`;
  - `rssi`;
  - `dropped_frames`, `fps` y `av_drift_ms` (media y máximo);
  - `link`: la tarea de red encontró datos, nada o el anillo lleno;
  - `gap_max_ms`: la espera más larga por un byte de vídeo.

  Al sintonizar, `[TUNE] time_to_first_video_ms` y `time_to_stable_playback_ms` (5 s seguidos sin descartes ni
  esperas). Al conectar la Wi-Fi, `[WIFI] +Ns rssi` cada segundo durante 30 s. Las líneas `[SERVER]` y `[REMOTE]`
  cuentan cada sesión, conexión y reintento. Qué significan y qué se midió con ellas:
  [medidas de red](NETWORK_TUNING.md).

Nada de esto se activa solo, y con `PAUTV_DEBUG_STATS 0` ni siquiera se compila.

## Pruebas en la placa

```sh
tools/make_test_fixtures.sh /Volumes/RETROTV   # clips sintéticos en /retrotv/test (8 MB)
tools/device_tests.py                        # con la SD en la placa y el USB conectado
```

El script reinicia la placa y comprueba en el log, sin tocar nada:
- la transición entre capítulos indexados;
- que detecta un `.idx` que no coincide;
- el canal sin índices;
- el EOF limpio con audio más corto o más largo que el vídeo;
- que un archivo vacío no provoca un bucle;
- que el zapeo vuelve a los canales normales.

Además, que no hay reinicios, watchdog ni tareas atascadas.

Canales remotos, con la placa en el mismo Wi-Fi que el ordenador:

```sh
tools/remote_device_tests.py   # arranca su propio RETROTV Server, lo para y lo vuelve a arrancar
```

Con `--live` prueba un directo HLS: monta su propia fuente para pararla y volver a arrancarla, y hace 20 zapeos entre
el directo y la SD. Comprueba:
- que el canal remoto se ve (fps, desfase A/V, kbps);
- los cambios remoto → SD, SD → remoto y remoto → remoto, y que el servidor cierra cada sesión;
- que al parar el servidor sale NO SIGNAL con reintentos y que la imagen vuelve sola;
- que el zapeo sigue respondiendo, sin reinicios ni errores de SD.

`tools/stream_profiles.py` compara perfiles de directo (fps, calidad, búfer) midiendo en la placa.

`tools/battery_log.py http://retrotv.local descarga.csv` registra una descarga completa por Wi-Fi (batería llena,
sin USB-C) hasta que la tele se apaga, y da la autonomía y la curva de tensión medida para `src/power/Battery.h`.

## Tests en el ordenador

```sh
tools/run_host_tests.sh              # clang/gcc del sistema, con ASan y UBSan
SDKROOT=$(xcrun --show-sdk-path) CXX=g++-16 tools/run_host_tests.sh   # GCC de Homebrew en macOS
SANITIZE=0 tools/run_host_tests.sh   # sin sanitizers
```

Comprueban la lógica pura (clics, gestos, ejes del táctil, volumen y sintetizador, separador MJPEG,
reloj A/V, `channels.json`, nombres ASCII, recorte del OSD, índice `.idx`, posición en emisión, teletexto, anillo de
bytes, protocolo de canales remotos, mando web y ajustes, redes Wi-Fi, batería y voz: niveles, palmadas, standby por
voz y grabadora), compiladas con clang, ASan y UBSan, más los autotests de `make_index.py` y `make_dist.py`. No hace falta la placa.
ArduinoJson se toma de `.pio/libdeps` y, si falta, se descarga con `pio pkg install`.

Nuestro código compila con `-Wall -Wextra -Werror`. ArduinoJson entra con `-isystem`, y los dos falsos positivos que
GCC aún encuentra dentro de la librería (`maybe-uninitialized` y `aggressive-loop-optimizations`, tras el inlining)
se ignoran solo alrededor de ella, en `test/third_party.h`. Probado con clang y GCC 16, con y sin sanitizers. Con
GCC 13 (el de Ubuntu 24.04), dos avisos `format-truncation` en los propios tests (`test/config_tests.cpp` y
`test/wifi_tests.cpp`) paran la compilación por `-Werror`; la integración continua usa clang.

El servidor tiene sus propios tests (`pytest`, ver [RETROTV Server](../server/README.md)).

## Integración continua

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) corre en cada pull request y en cada push a la rama
por defecto del repositorio (`main` aquí; en un fork, la que tenga), en Ubuntu y sin placa. Un push a otra rama
aparece en Actions con los jobs saltados: esa rama la comprueba su pull request. Solo tiene permiso para leer el
repositorio, no usa secretos y nunca flashea nada.

**En un fork** funciona igual, pero GitHub desactiva los workflows de los forks hasta que su dueño los activa en la
pestaña Actions. Un pull request de un fork hacia RetroTV corre en RetroTV, sin secretos y con permisos de solo
lectura; si es la primera contribución de esa persona, GitHub pide que alguien con permisos lo apruebe antes.

| Job | Qué comprueba | Comando |
|---|---|---|
| `host-tests` | Tests C++ de la lógica pura con clang, ASan y UBSan, y los autotests de `make_index.py` y `make_dist.py` | `CXX=clang++ tools/run_host_tests.sh` |
| `server-tests` | Tests del servidor (Python 3.12), con FFmpeg real para los de HLS y transcodificación | `python -m pytest` en `server/` |
| `firmware` | Que compilan los cinco entornos de `platformio.ini`; en `voice_nokeys_ww`, además, que `standby/partitions.csv` sigue siendo la tabla del firmware | `pio run -e <entorno>` |
| `standby` | Que compila la app de standby sin el modelo de «Hey Retro» (el caso de cualquier clon del repositorio) | `pio run -d standby` |

Solo se guardan en caché las descargas de pip y de PlatformIO. Los toolchains (unos 2 GB el de Arduino y 4 GB el de
ESP-IDF) se descargan en cada ejecución: guardarlos llenaría casi toda la caché de 10 GB del repositorio.

Lo que la CI **no** puede comprobar y queda para la placa ([plan de pruebas](TEST_PLAN.md)): pantalla, audio y
altavoz, microSD, Wi-Fi y mando web reales, micrófono y palmadas, LED, teclas, «Hola ESP» y «Hey Retro» (aciertos y
falsos positivos), batería y consumo. Tampoco empaqueta `srmodels.bin` ni prueba el traspaso a la app de standby.

## Paquete del código para compartir

```sh
tools/make_dist.py            # dist/retrotv-<versión>-<commit>.zip con lo que hay en HEAD
```

Sale de `git archive`, así que solo lleva archivos que Git sigue: nunca `include/secrets.h`, `include/title_tags.h`,
`.pio`, entornos virtuales, cachés, `server/media`, la configuración local del servidor ni `.git`. Después revisa el
ZIP por nombres y rutas (vídeo, audio, índices, carcasas, temporales) y comprueba que ningún texto de tu
`include/secrets.h` aparece dentro, sin escribirlo en pantalla. Si algo falla, borra el ZIP y lo dice. Lleva
`include/secrets.example.h` para configurar la Wi-Fi. Los cambios sin commit no entran: haz commit antes.

## Estructura del código

```
platformio.ini          entornos pautv (el normal), voice (+ micrófono), voice_nokeys (sin botones) y sus versiones _ww (wake word): espressif32@6.9.0, gnu++17, -Wall -Wextra en src/
include/config.h        ajustes de la app: tiempos, rotación, SPI, audio, vídeo, tareas y prioridades, PAUTV_DEBUG_STATS
include/board_config.h  pines verificados (fuentes en docs/HARDWARE.md) y ejes del táctil
include/app_types.h     estados, eventos de entrada, macro de log con prefijo
src/main.cpp            setup/loop → App
src/app/                App: estados (App.cpp), reproducción, cambio de canal y OSD (AppPlayback.cpp), pantallas y ajustes (AppScreens.cpp), teletexto (AppTeletext.cpp), depuración por serie y soak test (AppDebug.cpp)
src/channels/           ChannelManager: channels.json, validación, zapeo, nombres ASCII (C++ puro, testeado); ChannelSchedule: programación en emisión; ScheduleBuilder: la programación de las guías, en su propia tarea
src/media/              MediaPlayer (tareas de vídeo y audio, start/stop); SdPrefetch (lectura anticipada de la SD); PlaybackClock, JpegFrameSplitter, EpisodeIndex y OnAir (C++ puro)
src/display/            DisplayManager: panel, retroiluminación, DisplayTask (único dueño del LCD); BlockClip (recorte bajo el OSD)
src/ui/                 UIManager: buzones de estado y OSD, estática; UIScreens: pantallas, ajustes y OSD
src/audio/              AudioManager (I2S legacy, ES8311, amplificador, tarea de efectos) y Synth (C++ puro)
src/input/              ButtonLogic y GestureLogic (C++ puro), Buttons, TouchManager (lector FT6336 propio)
src/storage/            StorageManager (SD_MMC 40→20 MHz, estructura, JSON, capítulos), SdFile (lectura POSIX) y SdLayout (rutas, channels.json por defecto)
src/network/            WifiManager: máquina de estados sin bloqueos, reintentos, NTP; RemoteSource + HttpStream: canales remotos (tarea de red, sesión, dos streams HTTP); ByteRing y RemoteProtocol (C++ puro)
src/web/                mando web: servidor HTTP, página, API, ajustes y emparejamiento
src/teletext/           Teletext: filas, títulos y orden de páginas (C++ puro)
src/settings/           SettingsStore: volumen, último canal y brillo en NVS con escritura diferida
src/power/              Battery: lectura, porcentaje y avisos; Standby: qué hace apagar, con teclas o sin ellas
src/diagnostics/        Diagnostics: PSRAM, flash y heap en ejecución, escaneo I2C, batería y LED de estado
src/voice/              RETROTV Voice (solo con -e voice): captura del micrófono, palmadas, standby por voz, grabadora y canal MENSAJES
lib/es8311/             driver ES8311 de Espressif, copiado sin modificar del sketch 07.1 de Freenove
tools/                  convert_video.sh, make_index.py, make_logo.py, make_remote_sticker.py, make_demo_clip.sh, make_test_fixtures.sh,
                        run_host_tests.sh, device_tests.py, remote_device_tests.py, stream_profiles.py, battery_log.py, make_dist.py,
                        standby_flash.sh
standby/                app de standby (experimental, ESP-IDF 5.4.1, partición app1): «Hola ESP» con ESP-SR y «Hey Retro» con microWakeWord; ver docs/WAKEWORD.md
test/                   tests en el ordenador (host, channel, overlay, onair, teletext, remote, web, config, wifi, battery y voice_tests.cpp)
server/                 RETROTV Server (Python + FastAPI): canales por red y directos (FFmpeg, HLS, 3Cat, RTVE, Pluto TV); ver server/README.md
data/example-config/    channels.json y wifi.example.json de ejemplo
docs/                   CHANNELS, CONTROLS, REMOTE, DEVELOPMENT, ARCHITECTURE, HARDWARE, TEST_PLAN, NETWORK_TUNING, PROVIDERS, VOICE, WAKEWORD; img/ con las imágenes
.github/workflows/      ci.yml: integración continua (tests y compilaciones, sin placa)
LICENSES/               textos de GPL-3.0 y Apache-2.0 para los archivos con esas licencias
```

