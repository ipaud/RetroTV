# Arquitectura — RETROTV v0.2.0-alpha2 (V0.1 + en emisión + teletexto + canales por red + directo HLS/3Cat)

Cómo está construido el firmware y por qué. Los pines están en [HARDWARE.md](HARDWARE.md) y las pruebas en
[TEST_PLAN.md](TEST_PLAN.md).

## Principios

1. **Cada recurso tiene un único dueño.**
   - La pantalla la toca solo la DisplayTask.
   - El bus I2C (códec y táctil) lo usa solo la loopTask.
   - El puerto I2S lo comparten dos escritores, uno por bloque y con un mutex.
2. **Nada se reserva mientras se reproduce.**
   - Tareas, buffers, colas y el decodificador AAC se crean una sola vez en `begin()`.
   - Cambiar de canal no reserva ni libera nada, y nunca reinicia el ESP.
3. **Nada bloquea el bucle principal.**
   - El Wi-Fi, el cambio de canal, los tiempos del OSD y la escritura de ajustes son máquinas de estados que avanzan
     un poco en cada vuelta.
   - Las únicas esperas son acotadas: `MediaPlayer::stop()` con un tope de 500 ms, y el montaje de la SD al arrancar.
4. **La lógica pura se prueba en el ordenador.**
   - Clics, gestos, ejes del táctil, sintetizador, separador MJPEG, reloj A/V, `channels.json` y recorte del OSD no
     incluyen `Arduino.h`.
   - `tools/run_host_tests.sh` los compila con ASan y UBSan: 253 comprobaciones, más el autotest de
     `make_index.py`.
5. **Lo que no se puede probar en el ordenador se automatiza en la placa.** `tools/device_tests.py` maneja la tele por
   el puerto serie, con la sintonía de prueba `T <ruta>`, y comprueba el log contra fixtures sintéticas.

## Capas

```
 UI           App (máquina de estados, loopTask)            UIManager + UIScreens (renderers)
                 │ publica UiState / OsdState  ─────────────────▶ buzones (sección crítica corta)
 ─────────────────┼───────────────────────────────────────────────────────────────────────────────
 Servicios    ChannelManager   MediaPlayer ── PlaybackClock   SettingsStore   Diagnostics   WifiManager
                 │                 │  JpegFrameSplitter
 ─────────────────┼─────────────────┼─────────────────────────────────────────────────────────────
 Hardware     StorageManager   DisplayManager (DisplayTask)   AudioManager + lib/es8311   Buttons   TouchManager
              SD_MMC            Arduino_GFX / ILI9341          I2S legacy / ES8311 / SC8002B GPIO    FT6336 (I2C)
```

- Las dependencias van hacia abajo.
- **DisplayManager no conoce la UI:** recibe un `RenderFn(gfx, now, ctx, overlay&)` que la App registra al arrancar.
- **MediaPlayer lee de un `Stream*`:** hoy es un `File` de la SD; en la V0.2 será una conexión de red, sin tocar
  el reproductor.

| Módulo | Responsabilidad | Dónde corre |
|---|---|---|
| `App` (`src/app/`) | Estados, qué se ve, qué suena, qué significa cada evento en cada estado | loopTask |
| `UIManager` / `UIScreens` | Buzones de `UiState`, `OsdState` y la página de teletexto; dibuja pantallas, estática, OSD y teletexto (`UITeletext.cpp`) | DisplayTask |
| `DisplayManager` | Inicializa el panel, retroiluminación, pool de bloques de vídeo, recorte bajo el OSD | DisplayTask |
| `MediaPlayer` | Reproduce MJPEG + AAC desde `Stream*`, con `start`/`stop`, reloj y estadísticas | tareas `video` y `media-audio` |
| `PlaybackClock` | Posición común para audio y vídeo (C++ puro) | la escribe el audio; la leen vídeo y pantalla |
| `AudioManager` | ES8311, I2S, amplificador, volumen y silencio, tarea `audio-fx` (pitido, tono, siseo), `writePcm` | loopTask (I2C) y tareas de audio |
| `ChannelManager` | `channels.json`: validación, orden, zapping, nombres ASCII (C++ puro) | loopTask |
| `ChannelSchedule` | Programación en emisión de un canal: capítulos en orden natural (`compareEpisodeNames`: sin separadores, números por valor) + duración leída de cada `.idx`; se construye en la primera sintonía, en PSRAM | loopTask |
| `Teletext` (`src/teletext/`) | Filas de teletexto con códigos de color, orden de páginas, títulos de capítulo desde el nombre (C++ puro) | loopTask |
| `RemoteSource` + `HttpStream` (`src/network/`) | Canales remotos: una tarea crea la sesión en RETROTV Server, abre vídeo y audio por HTTP y los vuelca a anillos en PSRAM; `HttpStream` es el `Stream` que lee el reproductor | tarea `net` (core 0); `readBytes` en las tareas del reproductor |
| `ByteRing` / `RemoteProtocol` | Anillo SPSC y lectura con espera; URLs, JSON de sesión, cabeceras HTTP, chunked (C++ puro) | — |
| `StorageManager` | SD_MMC a 40→20 MHz, estructura de carpetas, JSON pequeño, listado de capítulos | loopTask |
| `SettingsStore` | Volumen, último canal y brillo en NVS, con escritura diferida | loopTask |
| `WifiManager` | Estación Wi-Fi sin bloqueos, reintentos, NTP | loopTask (+ pila Wi-Fi en el core 0) |
| `Buttons` / `TouchManager` | Mandos + BOOT; FT6336 detectado en ejecución; lógica en `ButtonLogic.h` y `GestureLogic.h` | loopTask |
| `Diagnostics` | PSRAM, flash, heap, escaneo I2C, batería, LED | loopTask |

## Tareas, núcleos y prioridades

| Tarea | Core | Prio | Qué hace | Por qué ahí |
|---|---|---|---|---|
| `media-audio` | 1 | 5 | AAC → PCM → `i2s_write` (bloquea hasta que hay hueco en el DMA) | La más alta de la app: el dibujo nunca deja al audio sin datos. Colchón DMA 8×256 = 46 ms |
| `audio-fx` | 1 | 5 | Pitido, tono de 440 Hz, siseo del cambio de canal | Solo suena cuando el reproductor está parado |
| `display` | 1 | 3 | Dibuja bloques de vídeo al llegar; UI a 25 fps | Escribe el SPI por sondeo y el audio la desaloja |
| `loopTask` | 1 | 1 | App, entrada, I2C, Wi-Fi, NVS | Duerme 10 ms por vuelta |
| `video` | 0 | 2 | Lee de la SD (o de la red), acompasa, decodifica JPEG | Lejos del audio. Convive con Wi-Fi/lwIP (prio 18–23). `vTaskDelay(1)` por fotograma para que IDLE0 alimente el watchdog, que **no se desactiva** |
| `net` | 0 | 3 | Canales remotos: DNS/mDNS, sesión, dos sockets → anillos | Toda la espera de red vive aquí, nunca en el loop ni en el audio. Duerme 1 tick por vuelta: IDLE0 y el decodificador siempre tienen el core |
| `httpd` | 0 | 3 | Mando web (`src/web/`): la página (un solo HTML con sus cuatro diseños, que solo cambian el CSS), `/api/state`, `/api/channels`, `/api/guide`, `/api/logo` (cada logo en color, negro y blanco, leído a PSRAM al arrancar) y las órdenes a una cola que vacía el loop. Los ajustes (`/api/config/`) también pasan al loop, que es el dueño de la SD, la Wi-Fi y NVS; el servidor espera su respuesta hasta 6 s | Por debajo de todo lo que reproduce. Una petición son unos cientos de bytes; nunca toca el reproductor ni la pantalla. Como mucho 5 sockets, y cada respuesta cierra su conexión |

El reparto está comentado en `include/config.h` (`*_TASK_CORE`, `*_TASK_PRIO`).

## Pantalla: un solo dueño

```
 App ──UiState──▶ [buzón] ─┐
 App ──OsdState─▶ [buzón] ─┤        DisplayTask (core 1)
                           ├──▶ cada 40 ms: render_(gfx, now, ctx, overlay)
 video task ──VideoBlock──▶ cola "ready" ─▶ drawBlock(): recorta alrededor de overlay ─▶ SPI
                ▲                                      │
                └──────────── cola "free" ◀────────────┘   (pool fijo de 6 bloques de 4 KB)
```

- **Buzones:** la App nunca llama a Arduino_GFX. Publica estado (copia bajo `portENTER_CRITICAL`) y la DisplayTask
  lo recoge en su siguiente vuelta.
- **Vídeo:** el decodificador rellena bloques del pool (hasta 2048 píxeles, el máximo que entrega JPEGDEC) y los pone
  en la cola; la DisplayTask los dibuja y los devuelve al pool. Si el pool se llena, el decodificador espera: es la
  contrapresión.
- **OSD:** el renderer declara su caja en `overlay`.
  - `forEachVisibleSpan()` (`display/BlockClip.h`) parte cada bloque para no pisarla nunca, y la estática tampoco
    pinta encima.
  - Al quitar el OSD, el siguiente fotograma repinta la zona.
  - El recorte se prueba píxel a píxel en el ordenador.
- **Fotograma mostrado:** cuando se dibuja el último bloque de un fotograma, la DisplayTask llama al *hook* del
  reproductor, que mide `av_drift_ms` en ese instante.

## Vídeo y audio

```
 SD (.mjpeg) ─readBytes 4 KB─▶ JpegFrameSplitter ─▶ buffer de 48 KB (PSRAM)
                                                      │
                     PlaybackClock + 40 ms de ventaja ─┤ ¿antes de tiempo? espera · ¿tarde > 1 fotograma? descarta
                                                      ▼
                                          JPEGDEC (SIMD del S3, RGB565 BE) ─▶ VideoBlock ─▶ DisplayTask

 SD (.aac, ADTS) ─readBytes─▶ Helix (AACFindSyncWord + AACDecode) ─▶ AudioManager::writePcm ─▶ I2S ─▶ ES8311
                                                                         │
                                                                         └─▶ PlaybackClock.onAudioWritten(muestras)
```

- **Separador:** busca FFD8…FFD9. En los datos de un JPEG, FF solo va seguido de 00 o de un marcador RST, así que
  FF D9 solo puede ser el final del fotograma. Un fotograma de más de 48 KB se trunca: el resto se salta, nunca se
  escribe fuera del buffer y se cuenta en `overflow`.
- **Reloj:** manda el audio.
  - Posición = (muestras aceptadas por I2S − latencia del DMA) / 44 100.
  - Sin audio, o cuando el audio acaba, sigue un reloj de pared desde la última posición.
  - Así la imagen y el sonido no se separan en un capítulo largo.
- **Ventaja de 40 ms** (`VIDEO_PIPELINE_LEAD_MS`): un fotograma llega al cristal unos 45 ms después de liberarse
  (unos 18 ms de decodificación y 38 de dibujo). Medido en la placa, `av_drift_ms` pasó de 44 a 5 de media.
- **AAC:** se usa la API pública de bajo nivel de Helix (`libhelix-aac/aacdec.h`).
  - `AACInitDecoder()` se llama una sola vez y `AACFlushCodec()` en cada programa nuevo.
  - El wrapper `AACDecoderHelix` no se usa porque su `begin()` libera y vuelve a reservar el decodificador.
- **Marcas de vida y estadísticas:** cada 5 s, con `PAUTV_DEBUG_STATS`.

Medido en la placa con un capítulo real de 23 minutos:

| | Valor |
|---|---|
| FPS | 23,9–24,0 |
| Decodificar un fotograma | 13–25 ms |
| **Dibujar un fotograma** | **38 ms** de 41,7 (SPI a 40 MHz, el cuello de botella) |
| av_drift_ms | media 4–6, máximo ~20 |
| Fotogramas descartados, desbordamientos, errores AAC | 0 |
| Lectura de la SD | 110–310 KB/s |

## start() y stop() sin reiniciar

- **Bits del event group:** `VIDEO_RUN` y `AUDIO_RUN` arrancan cada tarea; `VIDEO_IDLE` y `AUDIO_IDLE` indican que
  ha vuelto a esperar.
- **`start()`:**
  1. Limpia los bits IDLE y después pone los RUN, para que un `stop()` inmediato nunca vea un IDLE viejo.
  2. Reinicia el separador, el reloj y las estadísticas.
- **`stop()`:** pone `running_ = false` y espera los dos IDLE con un tope de 500 ms.
  - Todas las esperas internas están acotadas: lecturas de ≤4 KB, `acquireBlock` con tope, `i2s_write` con tope, y
    el callback del JPEG aborta el fotograma si `running_` es falso.
  - Luego descarta los bloques que quedaban en la cola.
- **Si una tarea no para a tiempo:** `stop()` devuelve `false` y la App muestra ERROR "MEDIA STUCK" sin cerrar los
  archivos que esa tarea aún usa. `start()` también se niega a arrancar si no pudo parar lo anterior, así nunca
  redirige tareas que siguen corriendo.
- **Códec caído:** si hay `.aac` pero el códec no está listo, `start()` reproduce sin audio con el reloj de pared. Si
  no, el reloj maestro (el audio) no avanzaría y la imagen se quedaría congelada.
- **Archivo roto:** si un capítulo termina sin haber mostrado ni un fotograma (vacío o indescifrable), la App lo trata
  como NO SIGNAL "FILE ERROR" en lugar de reiniciarlo en bucle. Un canal sintonizado en el último segundo de un
  capítulo sí muestra fotogramas, así que no se confunde.
- **Contadores:** son monotónicos (`PlaybackCounters`). Las estadísticas de 5 s y el soak test restan fotos propias,
  así que ninguno pone a cero los números del otro.
- **Prueba en la placa:** 100 cambios de canal con el heap y la PSRAM idénticos antes y después
  ([TEST_PLAN.md](TEST_PLAN.md)).

**Reservado una sola vez:**

| Qué | Dónde |
|---|---|
| Fotograma comprimido de 48 KB | PSRAM |
| Bloque de lectura de 4 KB | RAM interna |
| Entrada AAC de 4 KB | RAM interna |
| PCM de 8 KB | RAM interna |
| Decodificador Helix | PSRAM: `malloc` envía las reservas de más de 4 KB a PSRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`) |
| Objeto JPEGDEC y 6 bloques de vídeo de 4 KB | `.bss` |

## Máquina de estados

```
 BOOT ──1.5 s──▶ DIAGNOSTICS ──3 s + primer intento de Wi-Fi──▶ HOME ──1 s──▶ PLAYING ◀──────────┐
                     │                                                         │  ▲               │
                     └─ error de arranque (sin SD / channels.json) ─▶ ERROR 5 s ─┘  │               │
                                                                               CH±│  │ 260 ms        │
                                                                                  ▼  │               │
                                                                           CHANNEL_SWITCH            │
                                                          PLAYING ──MENU──▶ SETTINGS ──MENU─────────┘
                                                                              │   ▲
                                                                    DIAGNOSTICO ▼   │ MENU
                                                                            DIAGNOSTICS
 cualquier fallo grave (sin pantalla, sin PSRAM, MEDIA STUCK) ─▶ ERROR fijo
```

**Cambio de canal** (`beginSwitch` → `CHANNEL_SWITCH`, nunca bloquea):
1. Se elige el canal destino: siguiente o anterior habilitado.
2. `stop()` del reproductor y se cierran los archivos.
3. Estática a pantalla completa durante 200 ms, con siseo al 15 %.
4. Destello de 60 ms: una línea horizontal, como un CRT.
5. `startProgramme()`.
6. OSD `CH NN` + nombre + hora + Wi-Fi durante 1.5 s.

Si se pulsa CANAL durante la estática, cambia el destino y la estática vuelve a empezar, sin reproducir el canal
intermedio.

**Canales en directo:** sintonizar tarda unos segundos (sesión, FFmpeg, colchón). La estática y el siseo siguen todo
ese tiempo (`PlayMode::Tuning`); el destello llega cuando la señal está lista (`SignalFlash`, 60 ms) y justo después
sale la imagen. Al encender, tras la intro o la portada, el primer canal entra también con la transición.

**En PLAYING hay seis modos:**

| Modo | Cuándo |
|---|---|
| Video | Canal `local`. Con índices, en emisión (ver abajo). Sin ellos, un archivo o un capítulo al azar de una carpeta, distinto del anterior, desde el principio |
| TestCard | Canal `internal` `testcard` |
| Teletext | Canal `internal` `teletext` (ver abajo) |
| Tuning | Canal `remote` conectando: estática hasta que la red tiene ~250 ms en el búfer; después, Video con los streams de red |
| NoSignal con salto | El canal falló (sin SD, carpeta vacía, archivo roto): a los 3 s cambia solo al siguiente. Deja de saltar si fallan todos |
| NoSignal fijo | Tipos de la V0.2 (`remote`, `hls-proxy`, `tunarr`, `stream`) |

## En emisión

```
 tools/make_index.py ──▶ <nombre>.idx   cabecera de 32 B + una entrada de 8 B por segundo:
                                        (desplazamiento del fotograma MJPEG, desplazamiento de la trama ADTS)

 primera sintonía de un canal ──▶ ChannelSchedule::build   listado en una pasada, orden por nombre,
                                                           duración = cabecera de cada .idx (PSRAM)
 cada sintonía ──▶ onAirSlot(ahora_ms, duraciones) ──▶ (capítulo, ms) ──▶ entryFor ──▶ seek en .mjpeg y .aac
                                                                                      ──▶ MediaPlayer::start
```

- **`ahora`:** hora UTC con NTP. Sin ella, el **reloj de sesión** (un origen al azar por arranque + `millis()`), así
  que los canales siguen siendo continuos sin Internet.
- **Validación tras el seek:** los bytes deben ser `FF D8 FF` y una sincronización ADTS. Si no (capítulo reconvertido
  sin reindexar), los dos archivos vuelven a 0 y se registra el aviso.
- **Precisión:** el punto de entrada es el segundo indexado anterior (≤1 s antes de lo exacto). El audio empieza en
  la trama que suena en ese segundo o justo antes (≤23 ms).
- **Medido en la placa:** posición frente a la hora UTC del Mac, −0,2 y −1,1 s. Con el reloj de sesión, volver a un
  canal tras 15 s lo encuentra 16 s más adelante.
- **Programación y bucle:** al terminar un capítulo empieza el siguiente en orden, desde 0 (`advanceEpisode_`).
  Construir la programación es una vez por canal y arranque: 8 ms para un archivo y 73 ms para una carpeta de un
  capítulo, más una apertura de `.idx` por capítulo.
- **Sin índices:** si falta o está roto el `.idx` de cualquier capítulo, el canal usa el comportamiento de la V0.1.
  Un listado que no devuelve ningún capítulo válido (por ejemplo, nombres demasiado largos) nunca deja una
  programación vacía.
- **Validación pura:** `entryMatches()` comprueba `FF D8 FF` y la sincronización ADTS, y rechaza un índice sin audio
  si el `.aac` apareció después de indexar. `nextOnAirSlot()` decide si toca el siguiente capítulo o la posición en
  emisión.
- **`MediaPlayer` no cambia:** el `seek` lo hace la App sobre el `File` antes de pasar el `Stream*`.

## Teletexto

```
 AppTeletext (loopTask)                                   UIManager (DisplayTask)
   upcomingAirings(ahora, duraciones) por canal ──▶ tt::Page ──▶ buzón ──▶ drawTeletext: solo las filas que cambian
   título = episodeTitle(ruta)                     26×15 celdas         cada fila se pinta en el búfer de la
   cada 1 s (reloj), página nueva cada 12 s        + códigos de color    estática y sale en una transferencia
```

- **La guía no inventa nada:** `upcomingAirings()` usa la misma cuenta que la sintonía (`onAirSlot`), así que lo que
  dice coincide con lo que se ve al cambiar a ese canal. Si un canal lleva horas sin zapping, lo que se ve puede ir unos
  segundos por detrás de la guía (cada cambio de capítulo arranca desde 0 sin volver a mirar el reloj).
- **Filas:** cadenas C. Los bytes imprimibles ocupan una celda; `0x10–0x17` (tinta), `0x18–0x1F` (fondo hasta el
  final de la fila) y `0x0E` (doble altura) no ocupan ninguna. `tt::Row` nunca pasa de 26 celdas ni de 64 bytes.
- **Pintado:** la fuente GLCD 5×7 a ×2 (celdas de 12×16 px; ×4 en vertical para doble altura). Cada fila se compone
  píxel a píxel en el búfer de bandas de la estática (320×8) y se envía con `draw16bitRGBBitmap`. Una página entera son
  unos 150 KB por SPI; el reloj de la cabecera solo repinta su fila.
- **Programaciones:** el teletexto construye las que falten, una por vuelta del bucle. No suena nada en ese canal, así
  que leer la SD ahí no compite con ningún vídeo.
- **Sin OSD:** la cabecera ya dice la página y la hora; VOLUMEN y el toque pasan de página.
- **Todos los canales:** salen todos los activos menos el propio teletexto. Los locales, con su programación; los
  internos, con lo que son (carta de ajuste, QR del mando). Los de red, con la guía del servidor (`GET /api/guide`):
  la pide la tarea de red al entrar en el teletexto y cada minuto, solo mientras no hay ningún stream, y cualquier
  sintonía la corta. Las horas son las del servidor (`now_ms` + lo transcurrido). Sin guía, "EN DIRECTE". Se pide al
  servidor del primer canal de red; los canales de otro servidor salen sin guía.

## Canales por red (V0.2)

```
 FUENTE (archivos; después HLS oficial, 3Cat, RTVE, Tunarr)
   → RETROTV Server (server/): provider → sesión (t0) → /session/<id>/video + /audio
   → HTTP en la LAN
   → RemoteSource (tarea net) → HttpStream ×2 (anillos PSRAM) → MediaPlayer (sin cambios) → LCD + audio
```

**El firmware solo conoce** el número, el nombre, el tipo y la URL del canal (`http://<servidor>/channel/<n>`). Qué
hay detrás (archivo, HLS, 3Cat…) y FFmpeg viven en el servidor.

**La fuente es un `Stream`.** Un archivo de la SD (`fs::File`) ya lo es, así que no hay `LocalMediaSource`: sería un
envoltorio que no hace nada. `HttpStream` es el `Stream` de red. Así, JPEG, JPEGDEC, colas, pantalla, AAC,
`PlaybackClock` y la sincronía A/V son el mismo código para las dos fuentes.

**Un `readBytes` que devuelve 0 es el fin del stream** para `MediaPlayer`, y en red "aún no ha llegado" no es "se
acabó". Por eso `HttpStream::readBytes` espera en rodajas de 2 ms y devuelve 0 solo en tres casos:
- el cuerpo HTTP terminó;
- la App está parando (`cancel()`);
- pasan 3 s sin un byte, que cuenta como señal perdida y sale en `network_timeouts`.

**Sesión: vídeo y audio en el mismo instante.**
- Van en dos conexiones: `POST /api/sessions/<n>` fija t0 una sola vez, en la entrada `.idx` de ese segundo, y los
  dos GET de esa sesión empiezan ahí.
- Es la misma idea que el seek de la SD.
- El audio sigue siendo el reloj maestro. Si por una diferencia de duración entre pistas el vídeo va adelantado,
  espera; si va atrasado, descarta fotogramas.

**Tarea `net`:**
- Lleva los dos sockets.
- Lee solo lo que cabe en el anillo, así que un anillo lleno deja los datos en el socket y el control de flujo de
  TCP frena al servidor.
- Un `tune()` o un `close()` nuevo se atiende entre pasos acotados: connect 1,5 s, cabecera o JSON 2 s, mDNS 1,5 s.
  Zapear nunca espera más que uno de ellos.
- `stop` en orden:
  1. `cancel()` despierta una lectura en espera;
  2. `MediaPlayer::stop()`;
  3. `close()`: la tarea cuelga los sockets y el servidor da la sesión por terminada.

**Decisiones:**
- **`WiFiClient` directo, no `HTTPClient`:** hace falta timeout de connect por conexión, lectura de cabecera acotada,
  lectura incremental no bloqueante que el stop pueda cortar y cuerpo *chunked* decodificado al vuelo, que es lo que
  manda uvicorn. `HTTPClient` no da todo eso junto sobre un `Stream`.
- **Búfer:** 256 KB de vídeo (~1,3–2 s) y 16 KB de audio (~4 s), en PSRAM y reservados una vez. La reproducción
  empieza con ~250 ms (48 KB de vídeo, 1 KB de audio). Lo demás es colchón para cortes del Wi-Fi y no añade latencia
  al sintonizar.
- **mDNS:** `*.local` se resuelve con `mdns_query_a` sin anunciar ningún nombre. La tele se llama `retrotv` por DHCP y
  el servidor `retrotv-server.local` por mDNS. La última resolución se guarda y se olvida si el servidor deja de responder.
- **Reintentos:** en la App, nunca en la tarea. NO SIGNAL con motivo y una sintonía nueva a los 2, 4 y 8 s, y después
  cada 15 s. Un canal remoto no salta solo al siguiente: cuando vuelve el servidor, vuelve la imagen.

**Medido en la placa (demo 320×240, Wi-Fi a −48/−65 dBm):**

| Medida | Valor |
|---|---|
| Sintonía | ~0,3–0,5 s hasta tener 48 KB |
| Imagen | 23,8–24,0 fps, 0–2 descartes cada 5 s |
| Red | ~1,0–1,3 Mbps; el reproductor espera a la red 12–54 ms cada 5 s |
| Desfase A/V | 10–22 ms de media (5 ms en local). Probablemente porque la tarea `net` y el Wi-Fi comparten el core 0 con el decodificador; sigue por debajo de un fotograma |

## Directo: HLS y 3Cat (V0.2b)

La tele no sabe nada de HLS: ve las mismas `/session/<id>/video` y `/audio` que con un archivo. Todo el directo
ocurre en el servidor.

```
 provider (hls / 3cat) ─▶ HlsSource(url, variante 480p, audio ca)            server/app/providers/
 FfmpegSession: 1 FFmpeg ─┬─ perfil (20 fps, q14), 320x240 con bandas ─▶ pipe:<fd A> ─▶ búfer ≤1 MB ─▶ /video
   (-readrate 1, 2 s de   └─ 44,1 kHz mono AAC 32k ADTS                 ─▶ pipe:<fd B> ─▶ búfer ≤64 KB ─▶ /audio
    ventaja, 3 segmentos
    atrás)
```

**Mismo instante:**
- Un solo proceso y una sola entrada. Vídeo y audio comparten el reloj de FFmpeg y los dos empiezan en el tiempo 0
  (`fps=<n>:start_time=0`, `aresample=first_pts=0`).
- Lanzar dos FFmpeg al mismo HLS habría dado dos inicios distintos.

**Dos salidas de un proceso:**
- Pipes extra pasados como descriptores (`pipe:<fd>`): POSIX, igual en macOS y Linux, sin FIFOs ni sockets que
  limpiar.
- El servidor vacía los dos todo el tiempo, así que una salida lenta nunca bloquea la otra.

**Presión de la tele:** búferes acotados. Una tele que se queda más de ~1 MB atrás corta la sesión, con el motivo en
el log. Nunca se acumulan minutos en memoria.

**Arranque y caídas:**
- La sesión solo se da por buena cuando FFmpeg produce vídeo y audio. El límite es 10 s; después, 503 con motivo.
- Si FFmpeg sale, o pasa 8 s sin vídeo (fuente que deja de publicar), se reinicia hasta 3 veces, con 1, 2 y 4 s de
  espera. Después la sesión acaba y la tele pasa a su NO SIGNAL con reintentos.

**Cierre:** SIGTERM, SIGKILL a los 2 s y siempre se espera al proceso, así que no quedan zombis. Una sesión y su FFmpeg
viven y mueren juntos: cuelgue, caducidad, sustitución y apagado del servidor.

**Lo que costó medir en la placa:**
- **Modem sleep del Wi-Fi** (por defecto en Arduino): ~100 ms de ping. Apagado: 4–13 ms.
- **Ventana TCP de 5760 B** (fija en el core precompilado de Arduino 2.0.17). Limita cada conexión a ventana/RTT:
  511 KB/s con el enlace caliente (RTT 8,6 ms), unas 5 veces lo que pide el directo. Solo aprieta los primeros
  ~10–30 s tras unirse a la Wi-Fi, cuando el RTT sube a 58–72 ms (60 KB/s medidos). No hace falta tocarla: detalle
  en `docs/NETWORK_TUNING.md`.
- **Las esperas venían de la fuente, no de la red.**
  - **Causa:** empezando en el segmento más nuevo, FFmpeg lee pegado al borde del directo. Al acabar cada segmento
    de 3Cat (6 s) espera 1–5 s a que se publique el siguiente y luego recupera en ráfaga.
  - **Arreglo:** empezar 3 segmentos atrás (`-live_start_index -3`), a cambio de ~12 s más de retraso.
- **Los descartes venían de la CPU.** Con la Wi-Fi recibiendo, decodificar en el núcleo 0 tarda 19–31 ms (13–16 en
  local). A 24 fps no queda holgura, así que el directo va a **20 fps, `q:v 14`** (perfil `20fps-q14`, ~0,9 Mbps en
  SX3): 0 descartes medidos.
- **Perfiles y prebuffer:** los elige el servidor (`PAUTV_LIVE_PROFILE`, `PAUTV_PREBUFFER_MS`, o por canal). La
  sesión le dice a la tele los `fps` y el `prebuffer_ms`.
- **Cebado:** la tele cuenta fotogramas JPEG al recibir y empieza cuando tiene `prebuffer_ms` de vídeo (1,5 s por
  defecto), o con el anillo de 512 KB casi lleno. Su tope es de 6 s + 2 × prebuffer (máximo 14 s). Con un cebado
  de 0,4 s, el reloj del audio arrancaba antes de que llegara la ventaja del servidor y el vídeo se descartaba
  10–20 s.
- **`connect` de 4 s:** lwIP reenvía el SYN a los 3 s; con 1,5 s, un solo SYN perdido era `NO SERVER`.
- **Si la tele zapea durante el arranque de un directo,** el servidor lo detecta, para FFmpeg y no crea la sesión.

**Varias teles:** cada sesión es un FFmpeg. Funciona, pero no se comparte la transcodificación.

## Entrada

Todos los orígenes producen el mismo `InputEvent`, y la App decide qué significa según el estado.

| Evento | Teclas | BOOT | Táctil | PLAYING | SETTINGS | DIAGNOSTICS |
|---|---|---|---|---|---|---|
| CH_NEXT | CH+ | clic | desliza → | canal + | bajar | tono 440 Hz |
| CH_PREV | CH− | doble clic | desliza ← | canal − | subir | |
| VOL_UP | VOL+ | | desliza ↑ | volumen + | elegir / subir | tono 440 Hz |
| VOL_DOWN | VOL− | | desliza ↓ | volumen − | bajar el valor | |
| MUTE | mantener VOL− | | | silencio | | |
| TOGGLE_OSD | | | toque | OSD fijo | elegir | tono 440 Hz |
| MENU | mantener CH+ | mantener 1 s | mantener 0.8 s | ajustes | salir | volver a ajustes |

- **Clics:** antirrebote de 30 ms, pulsación larga de 1 s. Las teclas actúan al soltar; BOOT espera 350 ms por si hay
  doble clic.
- **LED frontal:** encendido; cada orden lo apaga 80 ms (`App::blinkLed`).
- **Gestos:** un toque es menos de 15 px y 800 ms; un deslizamiento, 40 px o más por el eje dominante.
- **Tiempo:** todo se calcula con restas sin signo, así que funciona cuando `millis()` da la vuelta.

## Persistencia, red y errores

- **NVS:** `volume`, `last_ch` y `bright` se escriben cuando llevan 4 s sin cambiar, y solo las claves que cambian.
  El zapping no desgasta la flash.
- **Wi-Fi:**
  - Primer intento de hasta 8 s, detrás de las pantallas de arranque; si no conecta, LOCAL MODE y reintento cada
    60 s.
  - Registra el motivo del fallo (red no visible o contraseña incorrecta).
  - NTP con zona horaria CET/CEST.
  - La tele no depende del Wi-Fi para nada de la V0.1.
- **SD:** se monta a 40 MHz y, si falla, a 20 MHz. `ensureMounted()` reintenta al arrancar cada canal local. El
  firmware nunca formatea.
  - **Timeouts de lectura (0x107):** con la Wi-Fi buscando redes, la tarjeta deja a veces de responder, y después
    rechaza todas las órdenes hasta que se inicializa de nuevo. A 20 MHz pasa igual. `App::recoverSd()` la vuelve a
    montar (hasta 3 veces cada 30 s) cuando un canal local no arranca o una lectura falla a mitad de capítulo; en
    ese caso vuelve a sintonizar donde va el programa en vez de saltar al capítulo siguiente. Las programaciones se
    rehacen.
- **Log:** prefijos `[BOOT] [DISPLAY] [TOUCH] [INPUT] [SD] [AUDIO] [WIFI] [MEDIA] [CHANNEL] [SETTINGS]`.
  - `Serial.setTxTimeoutMs(0)`: sin ordenador conectado, el log nunca frena la tele.
  - Durante la reproducción solo hay una línea de estadísticas cada 5 s.

## Formato de vídeo: por qué MJPEG + AAC

| Formato | Veredicto |
|---|---|
| H.264 | El ESP32-S3 no tiene decodificador por hardware. Descartado |
| RGB565 sin comprimir | El procesador no decodifica nada, pero son 153.6 KB por fotograma: unos 5 GB por capítulo de 23 min a 24 fps, por encima del límite de 4 GB de FAT32, y 3,6 MB/s de lectura sostenida. Descartado |
| Secuencia de JPEG sueltos | Unos 33.000 archivos por capítulo sobre una FAT lenta. Descartado |
| AVI con MJPEG | El mismo coste de decodificación que MJPEG; trae índice y audio intercalado, pero exige un lector de AVI. Candidato para la V0.2 si hace falta saltar a un punto del capítulo |
| **MJPEG crudo (`.mjpeg`) + AAC ADTS aparte (`.aac`)** | **Elegido.** Ya probado por los proyectos MiniTV de referencia. Separar los fotogramas es trivial (FFD8…FFD9), el ADTS se sincroniza solo y los dos se leen desde un `Stream*` |

Medido con un capítulo real (640x480 4:3, 23 min):
- conversión en 31 s;
- 249 MB de vídeo y 6 MB de audio;
- 7,6 KB por fotograma de media, 19,4 KB de máximo, lejos del buffer de 48 KB;
- se necesitan unos 190 KB/s de lectura de la SD.

## Tests

| Nivel | Qué | Cómo |
|---|---|---|
| Ordenador | Clics, gestos, ejes, curva de volumen, sintetizador, separador MJPEG, reloj A/V, decisión de fotograma, filtro de capítulos, `channels.json`, nombres ASCII, recorte del OSD, índice y posición en emisión, filas, títulos, orden de páginas y guía del teletexto | `tools/run_host_tests.sh` (ASan + UBSan). Se comprobó que cada test detecta al menos una mutación de su lógica |
| Ordenador (red) | URLs de canal remoto, JSON de sesión, cabeceras HTTP, chunked en cualquier corte, anillo con la vuelta de uint32, lectura con espera (datos, fin, cancelación, atasco), reintentos | Igual, en `test/remote_tests.cpp` |
| Servidor | API, sesiones (caducidad, límite), índice y, contra un uvicorn real con sockets: mismo segundo para vídeo y audio, bucle, cierre al colgar, apagado con streams abiertos | `cd server && .venv/bin/python -m pytest` |
| Herramientas | Demo, conversor (16:9, 4:3, anamórfico, acentos, `._*`, `PISTA`) | ffprobe + análisis de fotogramas ([TEST_PLAN.md](TEST_PLAN.md)) |
| Placa | Todo lo demás, fase a fase, más la prueba de estabilidad; automatizado en `tools/device_tests.py` (SD) y `tools/remote_device_tests.py` (red) | [TEST_PLAN.md](TEST_PLAN.md), columna Resultado |

## Hoja de ruta (no implementado)

| Versión | Qué |
|---|---|
| V0.2a | Remote Demo: transporte por red de punta a punta (**hecho**) |
| V0.2b | Adaptador HLS genérico con FFmpeg centralizado (**hecho**) |
| V0.2c | Provider 3Cat desde su API oficial (**hecho**: SX3 en la placa; TV3, 33, 3/24, Esport3 y 3Cat Anime en el servidor; ver [PROVIDERS.md](PROVIDERS.md)) |
| V0.2d | Provider RTVE: igual; si usa URLs temporales, un resolver en el servidor, nunca en la tele |
| V0.2e | Provider Tunarr |
| V0.2 estable | Canales locales y remotos sin diferencia para quien mira |
| V0.3 | EPG y metadatos (el teletexto ya calcula la guía local), canales FAST |

Pendiente de la V0.2 en la tele: líneas de red en la pantalla de diagnóstico (SERVER, CHANNEL, NET RATE, RECONNECTS,
BUFFER, A/V DRIFT). El margen de dibujo se resolvió con vídeo local a 20 fps (80 MHz estropea la imagen).

## Límites conocidos

| Límite | Detalle |
|---|---|
| Dibujo al límite | Dibujar un fotograma cuesta 38 de sus 41,7 ms a 40 MHz |
| arduino-libhelix es GPL-3.0 | Revisar antes de distribuir binarios a terceros |
| Sin exFAT | La tarjeta debe estar en FAT32 (el core 2.0.17 no lee exFAT) |
| Contraseña Wi-Fi en texto plano | En `wifi.json` y en `secrets.h` |
| El S3 no tiene APLL | MCLK sale de un divisor fraccional; suena limpio, pero con algo de jitter |
| Canales de carpeta | Se recorre la carpeta en cada arranque de canal, en tiempo lineal con el número de capítulos |
| Canal remoto que pierde solo el audio | Si cae solo la conexión de audio, el vídeo sigue mudo (reloj de pared) hasta que caiga también; el servidor cierra las dos a la vez, así que solo pasaría con un fallo raro de red |
| Un cliente por canal | Cada sesión lee sus propios archivos o lanza su propio FFmpeg; no se comparte la transcodificación entre teles |
| Directo en la tele | 20 fps como máximo mientras la Wi-Fi recibe (a 24 fps la decodificación del núcleo 0 no llega). Una conexión caliente lleva ~511 KB/s (ventana TCP de 5760 B); los primeros ~10–30 s tras conectar, ~60 KB/s. Medidas en `docs/NETWORK_TUNING.md` |
| Directos de 3Cat | Solo lo que 3Cat sirve a esta conexión (zonas); los canales FAST, Doraemon incluido, no tienen stream en la API pública |
| Táctil sin probar | El código del FT6336 y el mapeo de ejes solo se han probado en el ordenador (la unidad del usuario es una A) |
