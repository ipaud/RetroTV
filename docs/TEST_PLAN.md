# Plan de pruebas — RETROTV

Leyenda:
- **HOST**: se verifica en el ordenador.
- **REQUIRES HARDWARE TEST**: hace falta la placa; no se da por bueno sin probarlo físicamente.

Registra el resultado de cada prueba (fecha, OK/FALLO y notas) en la columna "Resultado". No marques nada que
no se haya comprobado.

## Estado de la versión 0.2.0-alpha2 (revisión del 2026-10-03, FNK0104A)

Resumen por funciones; el detalle y la fecha de cada prueba están en las tablas de abajo.

| Función | Probado en el ordenador | Probado en la placa | Pendiente en hardware |
|---|---|---|---|
| Arranque, pantalla, intro, diagnóstico | compila sin warnings propios | arranque, LCD 320×240, colores, intro, diagnóstico (T1, T2, T20) | sin basura al encender (T1.5), rotación 3 (T1.9) |
| Arrancar sin ordenador | — | con la batería y sin USB, tras una noche en STANDBY VOZ (T1.10) | con un cargador de pared |
| microSD | — | FAT32 a 40 MHz, recuperación de timeouts (T18) | con la carcasa v9 montada |
| Vídeo y audio local | separador MJPEG, reloj A/V, índice, en emisión | 20 fps sin descartes, audio sincronizado, zapeo con estática, OSD, NO SIGNAL | un capítulo completo de 23 min (T6.12), soak de 2 h (T9.6) |
| Teletexto | filas, títulos, páginas, guía | por log y serie | mirarlo en la pantalla (T10.8) |
| Canales por red y directos | protocolo, sesiones; servidor con `pytest` | remoto con `T`, SX3 en directo, NO SIGNAL y reintentos | desde `channels.json` en la SD (T11.15, T11.16, T12.11) |
| Mando web y ajustes | API, emparejamiento, Wi-Fi, canales | mando, guía, logos, ajustes, varias redes Wi-Fi (T14, T15, T17) | — |
| **Cuatro teclas de la carcasa** | lógica de pulsar y mantener (T19.1, T22.1) | — | **con los pulsadores montados** (T19.2) y encender con ellas (T22.3) |
| BOOT con la placa suelta | — | clic, doble clic, mantener, encender desde el reposo (T3.4–T3.6, T22.3) | — |
| LED del frontal | — | destello en STANDBY VOZ (TV10) | encendido y parpadeo con cada orden (T19.3) |
| Reposo AHORRO MAX | lógica de apagado | apagar y encender con BOOT (T22.2, T22.3, TV13) | consumo (T22.4) |
| Batería | curva, avisos, detección de carga | lecturas, carga del 81 al 99 % en ~2 h 45 min (T19.6) | descarga completa y autonomía real, avisos del 15 y 5 % con la LiPo, apagado por batería agotada |
| Voz: micrófono, VU, palmadas | niveles, detector, golpes sordos, envolvente del programa | captura sin coste visible en el vídeo, VU, doble apaga, triple cambia de canal (TV1–TV7b) | — |
| Voz: falsos positivos | escenarios sintéticos | 30 min sin dobles con **una** escena (TV8) | otros programas, volúmenes y salas durante horas |
| STANDBY VOZ | silencio antes y después | encender con dos palmadas, destello (TV9–TV11); ~40 mA estimados (TV12) | consumo con medidor (TV12), falsos encendidos con log (TV13c), batería agotada (TV13b) |
| Grabadora y MENSAJES | remuestreo, nivel de voz, WAV, flujo | grabar con la voz del usuario, reproducir, borrar (TV16, TV17) | — |
| "HEY RETRO" y comandos de voz | — | — | **no implementados** (TV14, TV15) |
| Táctil | gestos y ejes | — | solo con una FNK0104B (la unidad es una A) |

## Resumen: criterios de fin de la V0.1 (2026-09-29, FNK0104A)

| Criterio | Estado | Pruebas |
|---|---|---|
| Compila sin errores ni warnings propios y arranca de forma fiable | ✅ 0 warnings en todo el build; ningún arranque fallido en toda la sesión de pruebas | T1.1–T1.3 |
| LCD en horizontal | ✅ | T1.4, T1.8 |
| Táctil detectado o NOT FOUND, con todo operable por mandos | ✅ NOT FOUND en la A; todo se maneja con CANAL/BOOT y VOLUMEN | T3.3–T3.6 |
| SD, Wi-Fi con modo sin conexión y test de audio | ✅ SD a 40 MHz; LOCAL MODE sin bloquear; pitido y tono de 440 Hz | T4.2, T4.8, T5.3–T5.5 |
| Diagnóstico visible | ✅ | T2.3 |
| Reproduce un clip local con audio | ✅ 24 fps, av_drift_ms ~5 | T6.6, T6.7 |
| Al menos dos canales, cambio con estática y sin reiniciar | ✅ 5 canales; 100 cambios sin fugas | T7.5–T7.8, estabilidad |
| OSD y volumen | ✅ OSD de canal y de volumen, volumen y silencio (con los comandos serie `+ - x`; falta el mando físico) | T7.10, T7.11, T5.7 |
| Recuerda el último canal y el volumen tras reiniciar | ✅ `saved volume=55` → reset → `loaded volume=55` | T4.9, T6.10, T8.6 |
| Tests del ordenador en verde | ✅ 153 checks, con ASan y UBSan | T7.1 |

**Baseline v0.2.0-alpha1** (motor local endurecido). Ver la sección "Baseline v0.2.0-alpha1" más abajo.

**V0.2 — canales por red:** ver las secciones "V0.2 — Canales por red" y "V0.2b — Directo" más abajo.

**Pendiente de probar en la placa**, en orden de utilidad:
1. **Mirar la pantalla:** que al zapear se vea la estática ~0,2 s antes del destello (T9.9) y el aspecto del
   teletexto (T10.8). En el canal remoto, que el destello blanco y el pitido de cada segundo del demo coincidan
   (T11.14).
2. **Canales remotos desde la SD** (T11.15, T12.11): añadir REMOTE DEMO y SX3 a `channels.json`, zapear hasta ellos
   y arrancar con uno como último canal (T11.16). Mirar el audio catalán y la sincronía en SX3.
3. **Soak largo:** `S300` durante 2 h o más, comparando la primera y la última línea `[SOAK]` (T9.6).
4. **Un capítulo completo** de 23 min sin interrupciones (T6.12), dejando un canal hasta que cambie de capítulo.
5. **Montar las cuatro teclas** (GPIO2, 3, 14 y 21 a GND, T19.2) y encender con ellas (T22.3); después, afinar la
   curva de volumen 45..85 con el altavoz montado (T5.9, T7.13–T7.15). Volumen, silencio, su OSD y su memoria ya se
   probaron con los comandos serie y el mando web. (Los antiguos mandos de CANAL y VOLUMEN, T3.7 y T3.8, se sustituyeron
   por estas cuatro teclas.)
6. **LED del frontal** (GPIO43, T19.3).
7. **Arrancar con un `channels.json` roto** (T7.17) y ver la pantalla de error sin SD (T7.16, hasta ahora solo por log).
8. **Rotación 3** (T1.9) y **arranque sin ordenador** (T1.10), ya dentro de la carcasa.
9. ~~**SPI a 80 MHz**~~ probado 2026-10-01: la imagen sale mal. En su lugar, vídeo local a 20 fps (ver HARDWARE.md).
10. **Táctil:** solo en una FNK0104B (T2.6, T3.9, T3.10, T7.12).

## Fase 1 — Arranque, pantalla y estados

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T1.1 | Compila | `platformio run` | `SUCCESS`, 0 warnings en `src/` | HOST | OK 2026-09-29 (0 warnings) |
| T1.2 | Flasheo | `platformio run --target upload` | Sube sin errores por USB-C nativo | REQUIRES HARDWARE TEST | OK 2026-09-29 (/dev/cu.usbmodem101, 303A:1001) |
| T1.3 | Log de arranque | `platformio device monitor` | `[BOOT] RETROTV 0.1.0-dev`, `[DISPLAY] ILI9341 320x240 rotation 1`, PSRAM ≈ 8192 KB | REQUIRES HARDWARE TEST | OK 2026-09-29 (PSRAM 8189 KB) |
| T1.4 | Horizontal | Mirar la pantalla | 320x240 apaisado, sin recortes ni imagen espejada | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T1.5 | Sin basura al encender | Encender en frío | La retroiluminación no se enciende hasta que la pantalla está en negro | REQUIRES HARDWARE TEST | |
| T1.6 | Pantalla de arranque | Encender | "RETROTV / TELEVISION / SYSTEM START" sobre una franja negra, con estática gris animada y líneas de barrido | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T1.7 | Secuencia de estados | Log + pantalla | BOOT (1.5 s) → DIAGNOSTICS (3 s) → HOME (1 s) → PLAYING (NO SIGNAL con estática tenue) | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T1.8 | Colores | Mirar HOME | Marco y título verdes, texto blanco; ERROR con barra roja. Si sale azul, el orden es RGB/BGR | REQUIRES HARDWARE TEST | OK 2026-09-29 (verde correcto: BGR + inversión OK) |
| T1.9 | Rotación 3 | `PAUTV_ROTATION = 3` en `config.h`, flashear | Imagen girada 180°, correcta | REQUIRES HARDWARE TEST | |
| T1.10 | Sin USB | Alimentar con un cargador, sin ordenador | Arranca igual; el log no bloquea (`setTxTimeoutMs(0)`) | REQUIRES HARDWARE TEST | OK 2026-10-03 con la batería y sin USB: tras una noche en STANDBY VOZ, dos palmadas la reiniciaron y reprodujo un canal; respondía por `/api/state`. Con un cargador de pared, sin probar |

## Fase 2 — Diagnóstico y TEST CARD

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T2.1 | Compila | `platformio run` | `SUCCESS`, 0 warnings en `src/` | HOST | OK 2026-09-29 |
| T2.2 | Log de diagnóstico | `platformio device monitor` | Chip, flash física, PSRAM, heap, lista I2C, táctil, códec, batería | REQUIRES HARDWARE TEST | OK 2026-09-29 (flash 16384 KB, PSRAM 8189 KB, heap 343/374 KB, I2C: 0x18, táctil NOT FOUND, 4094 mV) |
| T2.3 | Pantalla de diagnóstico | Mirar durante 3 s tras el arranque | "RETROTV / HARDWARE TEST" + 10 líneas. LCD OK y AUDIO CODEC FOUND en verde; TOUCH, SD y WIFI en gris | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T2.4 | LED RGB | Mirar el LED de la placa | Verde tenue durante el diagnóstico y apagado después (rojo si falta LCD, PSRAM o códec) | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T2.5 | TEST CARD | Esperar a PLAYING | 7 barras (blanco, amarillo, cian, verde, magenta, rojo, azul), fila de castellations y panel negro con `RETROTV 320x240 NNFPS`, `--:--:--` e `IP -- RSSI --`. El FPS se actualiza cada segundo | REQUIRES HARDWARE TEST | OK 2026-09-29 (25 FPS) |
| T2.6 | Táctil en una FNK0104B | Con una unidad con táctil | `TOUCH OK` y 0x38 en la lista I2C | REQUIRES HARDWARE TEST | La unidad del usuario es FNK0104A: conector FPC del táctil vacío (confirmado a la vista) |
| T2.7 | Batería real | Conectar una LiPo de 3.7 V al conector BAT | Entre 3.3 y 4.2 V, coherente con un voltímetro | REQUIRES HARDWARE TEST | |

## Fase 3 — Entrada: mandos, BOOT y táctil

Mientras no haya canales (fase 7), los eventos salen en el log (`[INPUT] CH_NEXT`) y en la segunda línea de la
TEST CARD. MENU abre el diagnóstico y otro MENU vuelve a la TEST CARD.

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T3.1 | Tests en el ordenador | `tools/run_host_tests.sh` | `48 checks, 0 failures` (clic, doble clic, pulsación larga, rebotes, desbordamiento de millis, gestos, mapeo de ejes) | HOST | OK 2026-09-29. Además, 4 mutaciones de la lógica fallan los tests |
| T3.2 | Compila | `platformio run` | `SUCCESS`, 0 warnings en `src/` | HOST | OK 2026-09-29 |
| T3.3 | Detección del táctil | Log de arranque | Pulso en `CTP_RST`, escaneo y `[TOUCH] NOT FOUND, knobs and BOOT only` en una A | REQUIRES HARDWARE TEST | OK 2026-09-29 (FNK0104A) |
| T3.4 | BOOT, un clic | Pulsar BOOT una vez en PLAYING | ~350 ms después: `[INPUT] CH_NEXT` y "CH_NEXT" en la TEST CARD | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T3.5 | BOOT, doble clic | Dos pulsaciones rápidas | `[INPUT] CH_PREV`, sin CH_NEXT | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T3.6 | BOOT, mantener | Mantener ≥1 s | `[INPUT] MENU` y se abre el diagnóstico; mantener otra vez vuelve a la TEST CARD | REQUIRES HARDWARE TEST | OK 2026-09-29 (MENU abre y cierra el diagnóstico) |
| T3.7 | Mando CANAL (GPIO2) | Pulsador entre GPIO2 y GND | Igual que T3.4–T3.6 | REQUIRES HARDWARE TEST | Sustituida por T19.2 (cuatro teclas) |
| T3.8 | Mando VOLUMEN (GPIO14) | Pulsador entre GPIO14 y GND | Clic `VOL_UP`, doble `VOL_DOWN`, mantener `MUTE` | REQUIRES HARDWARE TEST | Sustituida por T19.2 (cuatro teclas) |
| T3.9 | Gestos táctiles | Solo con una FNK0104B | Toque `TOGGLE_OSD`, deslizar izquierda o derecha `CH_PREV`/`CH_NEXT`, arriba o abajo `VOL_UP`/`VOL_DOWN`, mantener `MENU` | REQUIRES HARDWARE TEST | No aplica a la unidad del usuario |
| T3.10 | Mapeo de ejes | FNK0104B: MENU → diagnóstico, dibujar con el dedo | El rastro verde sigue al dedo en rotación 1 y 3. Si no, ajustar `TOUCH_*` en `board_config.h` | REQUIRES HARDWARE TEST | No aplica a la unidad del usuario |

## Fase 4 — SD, Wi-Fi y ajustes

Mientras no haya canales (fase 7), CH_NEXT/CH_PREV cambian un número de canal provisional (CHnn) y VOL_UP/VOL_DOWN
cambian el volumen guardado (VOLnn). Los dos aparecen en la TEST CARD y se guardan en NVS.

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T4.1 | Compila | `platformio run` | `SUCCESS`, 0 warnings en `src/` | HOST | OK 2026-09-29 |
| T4.2 | Montaje de la SD | Log de arranque | `[SD] mounted 4-bit at 40000 kHz`, tipo y tamaño | REQUIRES HARDWARE TEST | OK 2026-09-29 (SDHC de 29817 MB a 40 MHz) |
| T4.3 | Estructura | Primer arranque con la SD vacía y luego un segundo arranque | Primero crea 9 carpetas y `channels.json`; el segundo no toca nada | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T4.4 | Reserva a 20 MHz | Con una tarjeta que falle a 40 MHz | `mount failed at 40000 kHz` ×2 y después `mounted … at 20000 kHz` | REQUIRES HARDWARE TEST | Parcial 2026-09-29: sin tarjeta, el log muestra 2 intentos a 40 MHz y 2 a 20 MHz; montar a 20 MHz con una tarjeta lenta sigue sin probar |
| T4.5 | Sin SD | Sacar la tarjeta y arrancar | `[SD] NOT FOUND…`, "SD NOT FOUND" en rojo en el diagnóstico; la TV sigue con la TEST CARD | REQUIRES HARDWARE TEST | OK 2026-09-29 (log: `SD NOT FOUND` → carta de ajuste) |
| T4.6 | Wi-Fi conectado | Log y pantalla | `[WIFI] ONLINE ip … rssi …`; diagnóstico con WIFI OK e IP; HOME en "ONLINE MODE"; IP y dBm en la TEST CARD | REQUIRES HARDWARE TEST | Log OK 2026-09-29 (192.168.1.174, −57 dBm) |
| T4.7 | Hora por NTP | Log y TEST CARD | `[WIFI] NTP time …` en hora local; reloj HH:MM:SS en la TEST CARD | REQUIRES HARDWARE TEST | Log OK 2026-09-29 |
| T4.8 | Sin Internet no se bloquea | Contraseña mala o router apagado | Tras ≤6 s del primer intento, LOCAL MODE con el motivo; la TV sigue; reintento cada 60 s | REQUIRES HARDWARE TEST | Parcial 2026-09-29: el primer arranque tras grabar superó los 6 s (calibración de radio + DHCP) → LOCAL MODE correcto |
| T4.9 | Persistencia | Cambiar CH con BOOT, esperar 5 s y pulsar RESET | `[SETTINGS] saved … last_ch=N` y, tras el reset, `loaded … last_ch=N` y CHnn en la TEST CARD | REQUIRES HARDWARE TEST | OK 2026-09-29 (3× CH_NEXT → `saved last_ch=4`; tras el reset, `loaded last_ch=4`) |
| T4.10 | Sin desgaste al hacer zapping | Varios clics seguidos | Un único `saved`, 4 s después del último clic | REQUIRES HARDWARE TEST | |
| T4.11 | Prioridad de wifi.json | Crear `/retrotv/config/wifi.json` | `connecting to … (from wifi.json)` | REQUIRES HARDWARE TEST | |

## Fase 5 — Audio

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T5.1 | Tests en el ordenador | `tools/run_host_tests.sh` | Curva de volumen (0 = silencio, 1 → 45, 100 → 85, monótona), 440 Hz = 880 cruces por segundo, fundidos sin chasquido, ruido acotado | HOST | OK 2026-09-29 (66 checks). Además, 3 mutaciones fallan los tests |
| T5.2 | Compila | `platformio run` | `SUCCESS`, 0 warnings en `src/` | HOST | OK 2026-09-29 |
| T5.3 | Arranque del audio | Log y diagnóstico | `[AUDIO] OK (I2S 44100 Hz, MCLK x256 = 11289600 Hz)` y "AUDIO OK" en verde | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T5.4 | Pitido al entrar en el diagnóstico | Arrancar, o MENU desde la TEST CARD | Pitido corto de 1 kHz | REQUIRES HARDWARE TEST | OK 2026-09-29. Al 40 % sonaba flojo; subido al 70 % |
| T5.5 | Tono de 440 Hz | En el diagnóstico: clic de VOLUMEN, un toque, o clic de BOOT/CANAL en una placa suelta | 1 s de tono limpio, sin zumbido ni distorsión | REQUIRES HARDWARE TEST | OK 2026-09-29 (al 90 %) |
| T5.6 | Sin chasquido al arrancar | Escuchar al encender | El amplificador sigue apagado mientras se configura el códec | REQUIRES HARDWARE TEST | |
| T5.7 | Volumen | Clic y doble clic de VOLUMEN en la TEST CARD | `[AUDIO] volume N (codec M)` y cambio audible por pasos de 5 | REQUIRES HARDWARE TEST | Con las teclas VOL (T19.2), pendiente; por serie y mando web, probado |
| T5.8 | Silencio | Mantener VOLUMEN | "MUTE" en la TEST CARD; el tono de prueba no se oye | REQUIRES HARDWARE TEST | Con las teclas VOL (T19.2), pendiente; por serie y mando web, probado |
| T5.9 | Afinar la curva 45..85 | Con un capítulo real (fase 6) | Volumen 100 sin saturar y volumen 1 aún audible | REQUIRES HARDWARE TEST | |

## Fase 6 — Reproducción local

Hasta que existan los canales de verdad (fase 7) hay tres canales provisionales: 1 = demo, 2 = primer capítulo de
`channel01`, 3 = TEST CARD. CANAL/BOOT los recorre con `stop()`/`start()`, sin reiniciar la placa.

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T6.1 | Tests en el ordenador | `tools/run_host_tests.sh` | Separador FFD8…FFD9 (bordes de lectura, basura previa, relleno FF 00, desbordamiento, fotograma cortado), reloj (sin audio, con audio, audio que termina, desbordamiento de millis), decisión esperar/mostrar/descartar, filtro de capítulos y ruta del `.aac` | HOST | OK 2026-09-29 (105 checks). Con ASan, escribir un byte fuera del buffer se detecta |
| T6.2 | Compila | `platformio run` limpio | 0 warnings en total | HOST | OK 2026-09-29 |
| T6.3 | Clip de demo | `tools/make_demo_clip.sh <raíz>` + ffprobe | MJPEG baseline 320x240 yuvj420p con 720 fotogramas en 30 s; AAC-LC mono a 44.1 kHz | HOST | OK 2026-09-29 (5,1 KB/fotograma) |
| T6.4 | Conversor | Entradas sintéticas 16:9, 4:3, DVD anamórfico, nombres con acentos, `._*` | 16:9 → imagen de 320×176 con bandas (antes 320×180; ver T16.5), 4:3 → pantalla completa, anamórfico corregido, 24 fps, nombres ASCII, `._*` ignorados, la 2.ª pasada salta lo convertido, `PISTA` inexistente falla antes del vídeo y sin dejar `.part` | HOST | OK 2026-09-29 |
| T6.5 | Capítulo real | `convert_video.sh` de un capítulo de 23 min, 640x480 4:3 | Un par `.mjpeg` + `.aac`; ningún fotograma supera 48 KB | HOST | OK 2026-09-29 (31 s de conversión, 249 MB; 33.390 fotogramas, media 7,6 KB, máximo 19,4 KB) |
| T6.6 | Reproducción del capítulo | CH2 | Imagen fluida, colores y sonido correctos | REQUIRES HARDWARE TEST | OK 2026-09-29 (usuario). 24,0 fps, 0 descartados, decodificar 13–25 ms, dibujar 38 ms, SD 110–300 KB/s |
| T6.7 | Sincronía A/V | CH1, la demo: destello y pitido cada segundo | Coinciden | REQUIRES HARDWARE TEST | OK 2026-09-29 (usuario). av_drift_ms medio 44 → **5** tras compensar 40 ms de latencia del pipeline |
| T6.8 | Cambio sin reiniciar | CH2 → CH3 → CH1 → CH2 | Cada cambio para y arranca limpio; sin reinicio ni `stop timeout` | REQUIRES HARDWARE TEST | OK 2026-09-29 (heap 170–171 KB y PSRAM 8022 KB estables) |
| T6.9 | Bucle al terminar | Dejar acabar la demo (30 s) | Vuelve a empezar sola (`stop`+`start`) | REQUIRES HARDWARE TEST | |
| T6.10 | Arranque en el último canal | Reset estando en CH2 | Arranca reproduciendo CH2 | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T6.11 | Falta el clip | Borrar `demo.mjpeg` y elegir CH1 | `[MEDIA] … missing` y TEST CARD; sin cuelgue | REQUIRES HARDWARE TEST | |
| T6.12 | Watchdog | Capítulo completo (23 min) | Sin `Task watchdog` ni reinicios; stats cada 5 s hasta el final | REQUIRES HARDWARE TEST | |

## Fase 7 — Canales, cambio de canal, OSD, NO SIGNAL y ajustes

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T7.1 | Tests en el ordenador | `tools/run_host_tests.sh` | channels.json (el de fábrica carga; orden; siguiente/anterior saltando desactivados; números duplicados, campos que faltan, tipo desconocido, ruta relativa, más de 32 canales), nombres a ASCII, recorte del vídeo alrededor del OSD píxel a píxel | HOST | OK 2026-09-29 (153 checks). Además, 4 mutaciones fallan los tests |
| T7.2 | Compila | `platformio run` limpio, gnu++17 | 0 warnings en total | HOST | OK 2026-09-29 |
| T7.3 | Carga de canales | Log de arranque | Una línea `[CHANNEL]` por canal: número, nombre, tipo, origen, (disabled) | REQUIRES HARDWARE TEST | OK 2026-09-29 (5 canales, el 10 desactivado) |
| T7.4 | Último canal | Reset | Arranca en el último canal; un canal de carpeta elige un capítulo al azar | REQUIRES HARDWARE TEST | OK 2026-09-29 (CH02) |
| T7.5 | Cambio de canal | Clic en CANAL/BOOT | Estática ~0.2 s con siseo → línea de destello → canal nuevo → OSD `CH NN` + nombre + hora + Wi-Fi durante 1.5 s | REQUIRES HARDWARE TEST | OK 2026-09-29 (usuario). Corregido después: desde un programa la estática no llegaba a verse, solo el destello mientras se abría el canal (ver T9.9) |
| T7.6 | NO SIGNAL que salta solo | Canal de carpeta vacía (CH03) | "NO SIGNAL" + "NO EPISODES"; a los 3 s, estática y canal siguiente | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T7.7 | Zapping durante la estática | Clic mientras hay estática | Cambia el destino y reinicia la estática sin reproducir el canal intermedio | REQUIRES HARDWARE TEST | OK 2026-09-29 (log) |
| T7.8 | Canal desactivado | Del 09 al siguiente | Salta el 10 (disabled) y vuelve al 01 | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T7.9 | Tipo de V0.2 | Poner `"enabled": true` en el canal 10 | "NO SIGNAL" + "V0.2", sin salto automático | REQUIRES HARDWARE TEST | |
| T7.10 | OSD sobre el vídeo | Mirar el OSD mientras hay vídeo | Se lee bien, no parpadea y el vídeo no lo pisa | REQUIRES HARDWARE TEST | OK 2026-09-29 (usuario) |
| T7.11 | OSD de volumen | Clic o doble clic de VOLUMEN | `VOL NN` + barra de 20 segmentos durante 1 s; mantener VOLUMEN: `MUTE` fijo mientras dura el silencio | REQUIRES HARDWARE TEST | Con las teclas VOL (T19.2), pendiente; por serie y mando web, probado |
| T7.12 | OSD fijo | Un toque (solo en la B) | La info del canal queda fija y la hora se actualiza cada minuto; otro toque la quita | REQUIRES HARDWARE TEST | No aplica a la unidad del usuario |
| T7.13 | Menú de ajustes | Mantener CANAL | `AJUSTES`: clic mueve, VOLUMEN elige o sube, doble VOLUMEN baja, mantener CANAL sale y vuelve al canal | REQUIRES HARDWARE TEST | Abrir, mover y salir OK 2026-09-29; elegir necesita VOLUMEN |
| T7.14 | Brillo | Ajustes → BRILLO, VOLUMEN ± | 10–100 % en pasos de 10, al momento; se conserva tras un reset | REQUIRES HARDWARE TEST | |
| T7.15 | Reiniciar | Ajustes → REINICIAR | Guarda los ajustes y reinicia | REQUIRES HARDWARE TEST | |
| T7.16 | Sin SD | Arrancar sin tarjeta | "ERROR / SD NOT FOUND / USING THE TEST CARD" durante 5 s → HOME "1 CHANNEL" → carta de ajuste | REQUIRES HARDWARE TEST | OK 2026-09-29 por log (ERROR → HOME → CH01 carta de ajuste); la pantalla de error no se comprobó a la vista |
| T7.17 | channels.json roto | Estropear el JSON | Error con el motivo durante 5 s → carta de ajuste | REQUIRES HARDWARE TEST | Validación probada en el ordenador |
| T7.18 | Siguiente capítulo al azar | Carpeta con 2 o más capítulos; dejar acabar uno | Empieza otro, nunca el mismo que acaba de terminar | REQUIRES HARDWARE TEST | |

## En emisión (índice `.idx` y programación del canal)

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T8.1 | Tests en el ordenador | `tools/run_host_tests.sh` | Cabecera y entradas del `.idx`, búsqueda de la entrada, programación en bucle (límites entre capítulos, capítulos de duración 0, epoch real sin desbordar), ruta del `.idx`, autotest de `make_index.py` | HOST | OK 2026-09-29 (198 checks + autotest, incluido el reindexado de un índice obsoleto). Mutaciones cazadas |
| T8.2 | Índice compatible entre lenguajes | `.idx` escrito por Python y leído con `EpisodeIndex.h` | Cada entrada apunta a un `FFD8FF` y a una cabecera ADTS | HOST | OK 2026-09-29 (demo: 30/30; capítulo de 23 min: 1392/1392) |
| T8.3 | Indexar la SD existente | `tools/make_index.py /Volumes/RETROTV/retrotv/media` | Un `.idx` por capítulo; los ya indexados se saltan | HOST | OK 2026-09-29 (3,4 s; 11 KB para 23 min) |
| T8.4 | Posición según la hora | Sintonizar con NTP | Posición = hora UTC mod duración (±1 s por la granularidad del índice) | REQUIRES HARDWARE TEST | OK 2026-09-29: CH01 −1,1 s, CH02 −0,2 s frente al reloj del Mac |
| T8.5 | Continuidad sin NTP | Antes de NTP: CH02 → CH01 → CH02 | Al volver, el capítulo ha avanzado lo que pasó en realidad (reloj de sesión) | REQUIRES HARDWARE TEST | OK 2026-09-29 (13:43 → 13:59 tras ~15 s) |
| T8.6 | Volumen por serie y memoria | `+ + - x x`, esperar 5 s, reset | OSD `VOL`, cambio audible, `MUTE`; `saved volume=55` y tras el reset `loaded volume=55` | REQUIRES HARDWARE TEST | OK 2026-09-29 (usuario + log) |
| T8.7 | Siguiente capítulo en orden | Carpeta con 2 o más capítulos indexados; dejar acabar uno | Empieza el siguiente por nombre, desde el principio | REQUIRES HARDWARE TEST | OK 2026-09-29 con `tools/device_tests.py` (1 → 2 → 1, cada uno desde 0:00) y con capítulos reales: Cowboy Bebop pasó del 5 al 1 desde 0:00 |
| T8.8 | Índice que no coincide | Reconvertir un capítulo sin reindexar | `does not match its episode` y empieza desde 0; nunca imagen rota | REQUIRES HARDWARE TEST | OK 2026-09-29 con `tools/device_tests.py`: `does not match its episode` y empieza en 0:00 |
| T8.10 | Orden natural de capítulos | Series con nombres mezclados (Harlock `-01-`/`_02_`, Hattori `-008-`/`_010_`) | Programación en orden de capítulo | HOST | OK 2026-09-29 (tests con los nombres reales; 2 mutaciones fallan) |
| T8.9 | Carpeta sin índices | Quitar un `.idx` | `no usable .idx … start from the beginning`; comportamiento de la V0.1 (al azar, desde el principio) | REQUIRES HARDWARE TEST | OK 2026-09-29 con `tools/device_tests.py` |

## Baseline v0.2.0-alpha1 — endurecimiento del motor local

| Id | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T9.1 | Tests en el ordenador | `tools/run_host_tests.sh` | 198 checks + autotest de `make_index.py` | HOST | OK 2026-09-29. Mutaciones cazadas: aceptar un índice sin audio teniendo `.aac`, no dar la vuelta al último capítulo y quitar la protección de programación vacía (UBSan: división por cero) |
| T9.2 | Build de desarrollo y de uso normal | `platformio run` y `PLATFORMIO_BUILD_FLAGS=-DPAUTV_DEBUG_STATS=0` | 0 warnings; la versión de uso normal no contiene el código de depuración | HOST | OK 2026-09-29, build limpio (1.025.353 y 1.022.453 B de flash; RAM 102.632 B) |
| T9.3 | Warnings estrictos solo para nuestro código | Meter una variable sin usar en un test | `run_host_tests.sh` falla (`-Werror,-Wunused-variable`); ArduinoJson entra con `-isystem` | HOST | OK 2026-09-29; también con `SANITIZE=0` |
| T9.4 | Comandos de depuración sin SD | `m`, `S0`, `T /retrotv/test/transition`, `s`, `n` | Fotos `[SOAK]` de inicio y fin (la pantalla late +128 en 5 s); sintonía de prueba → `NO SIGNAL: NO SD CARD` sin colgarse; `n` vuelve a `channels.json` | REQUIRES HARDWARE TEST | OK 2026-09-29 |
| T9.5 | Pruebas automáticas en la placa | `tools/make_test_fixtures.sh /Volumes/RETROTV` y después `tools/device_tests.py` | 7 líneas `OK` | REQUIRES HARDWARE TEST | OK 2026-09-29, 7/7 dos veces seguidas: transición 1 → 2 → 1, índice obsoleto detectado, canal sin índices desde el principio, 4 cambios limpios con audio más corto y más largo que el vídeo, archivo vacío sin bucle, `n` vuelve a `channels.json`, un solo arranque. Arreglados dos fallos del propio script: un índice obsoleto también empieza en 0:00 y el script lo tomaba por "cayó en el primer segundo"; y durante la reproducción el log por USB puede llegar hasta ~5 s tarde, así que las esperas llevan margen (`LOG_LAG_S`) |
| T9.6 | Soak largo | `S300` durante 2 h o más | Heap, mínimo, bloque más grande y PSRAM estables; ningún `STALLED`; `aerr` y `dropped_frames` en 0 o estables | REQUIRES HARDWARE TEST | |
| T9.7 | Códec caído con `.aac` presente | Difícil de provocar: por ejemplo, arrancar con el ES8311 desconectado | `audio not ready: playing silent`: el vídeo sigue en lugar de congelarse | REQUIRES HARDWARE TEST | Revisado en código; sin probar |
| T9.8 | Tests con GCC | `SDKROOT=$(xcrun --show-sdk-path) CXX=g++-16 tools/run_host_tests.sh`, también con `SANITIZE=0` | 253 checks; nuestro código sigue con `-Werror` | HOST | OK 2026-09-29 (GCC 16.2 y clang). Antes GCC fallaba por `-Wmaybe-uninitialized` dentro de ArduinoJson y, sin sanitizers, por `-Waggressive-loop-optimizations`: falsos positivos tras el inlining que `-isystem` no oculta. `test/third_party.h` los ignora solo alrededor de la librería. Comprobado que una variable sin usar y un `maybe-uninitialized` metidos en un test siguen fallando con los dos compiladores |
| T9.9 | Estática al cambiar de canal | Zapear desde un programa, con la hora de llegada de cada línea del log | ~0,26 s en CHANNEL_SWITCH (200 ms de estática + 60 ms de destello) | REQUIRES HARDWARE TEST | FALLO encontrado 2026-09-29: 0 s. `App::loop` calculaba el tiempo en el estado antes de leer los mandos, así que un zap saltaba la estática y el destello en la misma vuelta. Arreglado: ~0,2 s medidos desde el ordenador en 5 cambios seguidos. Falta verlo en la pantalla |
| T9.10 | Zapping repetido con la parrilla real | `z` con 19 canales (15 series, teletexto, carta, demo) | 100 cambios sin fugas ni cuelgues | REQUIRES HARDWARE TEST | OK 2026-09-29: 88 arranques de vídeo, 6 teletextos, 6 cartas, 0 NO SIGNAL; heap 161 → 161 KB (mínimo 156); PSRAM 8021 → 8009 KB (la programación de cada canal, una vez por arranque); `aerr` 0; un solo arranque |
| T9.11 | Línea `[SOAK]` de inicio | `S300`, `s`, `z`, `s` | Sin `STALLED` al empezar; intervalo con decimales | REQUIRES HARDWARE TEST | OK 2026-09-29. Antes la línea `start` decía `STALLED` en falso y `zap every 1 s` con 1,5 s |
| T9.12 | Build | `platformio run` y `PLATFORMIO_BUILD_FLAGS=-DPAUTV_DEBUG_STATS=0` | 0 warnings | HOST | OK 2026-09-29 (1.033.969 y 1.030.641 B de flash; RAM 105.544 B) |

## Teletexto — canal `internal` `teletext`

Con la parrilla de 19 canales de la SD (teletexto en el 01, 15 series, carta, demo y remoto desactivado). Las
pruebas en la placa se hicieron por serie (`g<página>`, `+`, `-`, `m`) leyendo el volcado de cada página en el log.

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T10.1 | Tests en el ordenador | `tools/run_host_tests.sh` | Filas (nunca más de 26 celdas ni de 64 bytes, colores hasta el final de la fila, doble altura), minutos redondeados hacia arriba, títulos desde los nombres reales de la SD, orden de páginas, guía (`upcomingAirings`) | HOST | OK 2026-09-29 (253 checks). Mutaciones cazadas: sin redondeo, relleno sin colores, capítulos de duración 0, sin etiquetas de ripeo y sin límite de columnas entre llamadas |
| T10.2 | Build | `platformio run` | 0 warnings | HOST | OK 2026-09-29 (1.033.905 B de flash, RAM 105.544 B: +2,9 KB por los buzones de página) |
| T10.3 | Guía de todos los canales | Entrar al canal 01 | Una programación por vuelta del bucle, sin bloquear la portada | REQUIRES HARDWARE TEST | OK 2026-09-29: las 16 en unos 3 s (7–297 ms cada una) |
| T10.4 | Páginas solas | Esperar en el canal | P100 → P101 1/4 → P101 2/4…, cada 12 s | REQUIRES HARDWARE TEST | OK 2026-09-29: 11,9 s y 12,0 s |
| T10.5 | Páginas a mano | `+`, `-`, `g204`, `g999` | Siguiente, anterior, salto a P204; `no page 999`; la página elegida no pasa sola antes de 60 s | REQUIRES HARDWARE TEST | OK 2026-09-29 (sin salto en 16 s; los 60 s completos no se midieron). Encontrado y arreglado: justo después de pasar a mano saltaba otra página sola |
| T10.6 | La guía dice la verdad | Leer P202 / P204 y zapear a esos canales | Capítulo y minutos que quedan coinciden con la sintonía | REQUIRES HARDWARE TEST | OK 2026-09-29: P204 a las 21:55:52 daba "QUEDEN 2'" y el 01 a las 21:57; al zapear quedaban 3 s y enlazó con el 01 desde 0:00. CH02 y CH03, también |
| T10.7 | Memoria | `m` antes y dentro del teletexto | Sin crecimiento | REQUIRES HARDWARE TEST | OK 2026-09-29: heap 169–170 KB, PSRAM 8009 KB con las 16 programaciones |
| T10.8 | Aspecto en la pantalla | Mirar P100, P101 y una P2NN | Colores, doble altura, sin texto cortado ni restos de la página anterior | REQUIRES HARDWARE TEST | Pendiente de confirmar mirando la pantalla (la vista previa en el ordenador usa el mismo código de dibujo) |
| T10.9 | Hora sin Wi-Fi tras un reinicio | Flashear y arrancar sin conectar al Wi-Fi | Hora local | REQUIRES HARDWARE TEST | OK 2026-09-29 tras el arreglo: sin Wi-Fi, la cabecera dio 22:07:48 y el Mac 22:07:54 unos segundos después. Antes salía en UTC porque la zona horaria solo se fijaba al conectar; ahora se fija al arrancar (afectaba también al reloj del OSD y de la carta) |
| T10.10 | Guía del servidor y todos los canales | `tools/run_host_tests.sh`; `pytest`; en la placa, entrar al 01 y `g101`, `+`…, `g222`, `g229`, `g223`, `g233` | Salen los 32 canales activos (7 subpáginas de P101). SX3, 3Cat Anime y los de Pluto con título, hora y minutos; 24h y los 24h de 3Cat "EN DIRECTE"; carta y mando con su descripción | HOST + REQUIRES HARDWARE TEST | OK 2026-09-30: 473 checks, 99 tests del servidor. En la placa, con la guía ya a los 17 s del arranque; P222 "BOLA DE DRAC Z - UNS QUAR…", "QUEDEN 5'", a continuación 14:33 (coincide con 3Cat); P229 One Piece con 14:39 y 15:05 (coincide con Pluto) |

## V0.2 — Canales por red (RETROTV Server)

Primer hito, Remote Demo: el canal 1 del servidor sirve el clip sintético de `server/run.sh` (30 s, testsrc con un
destello blanco y un pitido cada segundo), con índice. Mac en la misma red que la placa (192.168.1.x).

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T11.1 | Tests en el ordenador | `tools/run_host_tests.sh` (clang + ASan/UBSan; GCC 16 con y sin sanitizers) | URLs, JSON de sesión, cabeceras HTTP, chunked, anillo, lectura con espera, reintentos, validación de `remote` | HOST | OK 2026-09-29 (329 checks en los tres modos). Mutaciones cazadas: anillo sin partir la copia en la vuelta, chunked con un byte de más, cabecera que ignora *chunked*, sin validar `remote`. La carrera "fin de cuerpo" se eliminó del diseño (se lee el fin antes del `pop`) |
| T11.2 | Tests del servidor | `cd server && .venv/bin/python -m pytest` | API, sesiones, índice y streams contra un uvicorn real | HOST | OK 2026-09-29 (17 passed). Mutaciones cazadas: no cerrar la sesión al colgar, audio desde 0 (desfasado), apagar sin `--timeout-graceful-shutdown` |
| T11.3 | Servidor a mano | `curl` a `/health`, `/api/channels`, `/api/status/1`, `POST /api/sessions/1`, los dos streams | `video/x-motion-jpeg` y `audio/aac`; vídeo empieza en `FFD8FF` y audio en `FFF1`; 404 para canal o sesión inexistentes; la sesión desaparece al colgar | HOST | OK 2026-09-29 |
| T11.4 | Build | `platformio run` y la variante sin depuración | 0 warnings | HOST | OK 2026-09-29: 1.081.125 y 1.077.241 B de flash; RAM 108.016 B (+2,5 KB). PSRAM: 272 KB de anillos, reservados al arrancar |
| T11.5 | mDNS | `server/run.sh --lan`; resolver `retrotv-server.local` en el Mac y desde la placa | La IP del Mac | REQUIRES HARDWARE TEST | OK 2026-09-29: 192.168.1.131 en el Mac (`dscacheutil`) y en la placa (sesión creada por `retrotv-server.local`) |
| T11.6 | Canal remoto 60 s | `T http://retrotv-server.local:8080/channel/1` | Imagen y sonido; `[MEDIA]` y `[NET]` estables | REQUIRES HARDWARE TEST | OK 2026-09-29: sintonía en 0,3–0,5 s; 23,8–24,0 fps; 0–2 descartes cada 5 s; desfase A/V 10–22 ms de media (5 ms en local); ~1,0–1,3 Mbps; espera por la red 12–54 ms cada 5 s; búferes llenos; 0 timeouts; heap 146–147 KB |
| T11.7 | Cambios de fuente | `tools/remote_device_tests.py` | remoto → SD (el servidor cierra la sesión), SD → remoto, remoto → remoto (queda 1 sesión) | REQUIRES HARDWARE TEST | OK 2026-09-29, tres ejecuciones seguidas |
| T11.8 | Servidor que cae y vuelve | Parar el servidor con un canal remoto en pantalla y arrancarlo otra vez | NO SIGNAL, reintentos, la imagen vuelve sola, el zapping responde | REQUIRES HARDWARE TEST | OK 2026-09-29 (`SIGNAL LOST`/`NO SERVER`, 3 reintentos, vuelve sola). Encontrado: con la tele conectada, uvicorn no se paraba nunca (esperaba a que acabara el stream); arreglado con `--timeout-graceful-shutdown 1` |
| T11.9 | Sin Wi-Fi | Canal remoto en un arranque sin Wi-Fi | `NO SIGNAL / OFFLINE` y reintentos, sin bloquear | REQUIRES HARDWARE TEST | OK 2026-09-29 (arranque en LOCAL MODE: `OFFLINE`, reintentos a 2, 4 y 8 s) |
| T11.10 | Firewall de macOS | Primera conexión de la placa | `NO SERVER` hasta permitir a Python | REQUIRES HARDWARE TEST | Observado 2026-09-29: los primeros intentos daban `NO SERVER` y el ping funcionaba; tras aceptar el aviso, conectó. Documentado en server/README.md |
| T11.11 | Memoria | `m` antes y después de las pruebas remotas | Sin crecimiento | REQUIRES HARDWARE TEST | OK 2026-09-29: heap 148 → 147 KB (mínimo 143) y PSRAM 7749 → 7748 KB. Frente a la versión anterior, ~13 KB menos de heap interno (tarea `net`, 4 KB de E/S, sockets) |
| T11.12 | Canales locales sin cambios | `tools/device_tests.py` y `z` con el firmware nuevo | 7/7; 100 cambios sin fugas | REQUIRES HARDWARE TEST | OK 2026-09-29: 7/7; 100 cambios, 0 NO SIGNAL, desfase A/V 5 ms, mínimo de heap 143 KB, un solo arranque |
| T11.13 | Errores de lectura de la SD | Buscar `sdmmc_read_blocks failed` en los logs | Ninguno | REQUIRES HARDWARE TEST | Vigilar: 2 timeouts una vez (listando un canal con el Wi-Fi activo tras intentos fallidos de conexión; ese canal salió con 3 de 6 capítulos hasta reiniciar). No se ha repetido en 3 pasadas remotas ni en los 100 cambios |
| T11.14 | Sincronía remota a la vista | Mirar el demo remoto | Destello y pitido a la vez | REQUIRES HARDWARE TEST | Pendiente (solo medida por log) |
| T11.15 | Canal remoto en `channels.json` | Entrada `remote` en la SD y zapping normal | Igual que con `T`, con estática y OSD | REQUIRES HARDWARE TEST | Pendiente (probado solo con `T`) |
| T11.16 | Arrancar en un canal remoto | Último canal = remoto; reiniciar | Si el Wi-Fi tarda, `OFFLINE` y reintentos; después, imagen | REQUIRES HARDWARE TEST | Pendiente |
| T11.17 | Wi-Fi que cae a mitad | Apagar el router con un canal remoto en pantalla | `SIGNAL LOST` a los ~3 s, `OFFLINE` en los reintentos, vuelve con el Wi-Fi | REQUIRES HARDWARE TEST | Pendiente |
| T11.18 | Soak remoto | `S0` en un canal remoto durante 1 h o más | Sin fugas, 0 `network_timeouts` | REQUIRES HARDWARE TEST | Pendiente |

## V0.2b — Directo: HLS genérico, FFmpeg y 3Cat

Servidor en este Mac (FFmpeg 7.1) y placa en el mismo Wi-Fi. Canales del servidor: 2 = HLS TEST (demo en directo de
Unified Streaming) y 10 = SX3 (3Cat). En la placa se sintoniza con `T http://retrotv-server.local:8080/channel/<n>`.

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T12.1 | Tests del servidor | `cd server && .venv/bin/python -m pytest` | Playlists, providers HLS y 3Cat simulados, FfmpegSession con FFmpeg real y falso, sesiones en directo contra un uvicorn real | HOST | OK 2026-09-30 (63 passed, sin Internet). Mutaciones cazadas: el arranque que seguía como `running` tras fallar a mitad (encontrado así, y arreglado), la sesión huérfana si la tele se va durante el arranque |
| T12.2 | Tests HTTP sin depender del transporte | `tests/http_client.py` | El mismo cuerpo con Content-Length, chunked y cierre | HOST | OK 2026-09-30 |
| T12.3 | Directo de punta a punta sin placa | `server/tools/test_hls_pipeline.py` | MJPEG 320×240, ADTS 44,1 kHz mono, tiempo real, sin deriva A/V, FFmpeg muere al colgar, caída y vuelta de la fuente, nada vivo al final | HOST | OK 2026-09-30, 9/9: vídeo ×0,96 y audio ×0,97 tras la ráfaga inicial; 1 reinicio al cerrarse la fuente y vuelta a reproducir al recuperarla |
| T12.4 | Firmware y build | `tools/run_host_tests.sh`, `platformio run` | Sin cambios en la lógica pura; 0 warnings | HOST | OK 2026-09-30: 329 checks con clang y GCC 16; 1.081.365 B de flash (1.077.473 sin depuración), RAM 108.016 B |
| T12.5 | Sesión de directo esperando al servidor | Sintonizar un directo | La tele espera el `POST` (FFmpeg arrancando) sin `SERVER ERROR`, y se puede zapear durante la espera | REQUIRES HARDWARE TEST | OK 2026-09-30. Encontrado: la tele solo esperaba 2 s a la respuesta y un directo tarda 2–8 s. Ahora espera hasta 16 s, cancelable |
| T12.6 | HLS TEST en la placa | `T …/channel/2` | Imagen fluida, sonido, A/V estable | REQUIRES HARDWARE TEST | OK 2026-09-30: 23,6–24,2 fps, 0–1 descartes cada 5 s, desfase A/V 9–18 ms, búfer lleno. Encontrado y arreglado: (1) el modem sleep del Wi-Fi (~100 ms de ping) y (2) empezar con 0,4 s de vídeo dejaba el vídeo tarde 10–20 s; ahora sin modem sleep, `q:v 12` en directo y cebado de 160 KB |
| T12.7 | SX3 (3Cat) en la placa | `T …/channel/10` 60 s y cambios | Imagen, audio catalán, A/V, cambios sin FFmpeg sobrante | REQUIRES HARDWARE TEST | OK 2026-09-30: cebado en 3,0 s; 23,8–24,2 fps; 0–1 descartes; desfase A/V 7–10 ms; 600–950 kbps. SX3 → SD → SX3 → HLS TEST → SX3: en cada paso, una sola sesión en el servidor (ninguna en la SD) |
| T12.8 | Batería de directo en la placa | `tools/remote_device_tests.py --live` | remoto ↔ SD, remoto → remoto sin FFmpeg sobrante, servidor que cae y vuelve, fuente HLS que cae y vuelve, 20 zapeos, memoria estable | REQUIRES HARDWARE TEST | PARCIAL 2026-09-30, cinco pasadas. En todas pasaron: remoto → SD (el servidor cierra la sesión), SD → remoto, remoto → remoto (sin FFmpeg sobrante), servidor que cae y vuelve, fuente HLS que cae y vuelve, **20 zapeos (10/10 directos reproducidos)**, sin reinicios. **Falla la primera sintonía tras arrancar la placa** (ver T12.14). Memoria: heap 148 → 143–144 KB (mínimo 143 → 139) tras ~25 sesiones en directo, PSRAM estable: vigilar. Errores de SD: 0 en cuatro pasadas y 3 en la quinta (ver T12.15) |
| T12.9 | Canales 3Cat en el servidor | Una sesión real de 3 s por canal | MJPEG y ADTS | HOST | OK 2026-09-30: SX3 (1,9 s), TV3 (4,4 s), 33 (4,4 s), Esport3 (2,0 s), 3Cat Anime (1,5 s). 3/24 solo ofrece la zona TOTS: añadida al orden por defecto y probada (sesión real con MJPEG y ADTS desde `directes-tv-int`). 3Cat Doraemon: 503 `3Cat offers no stream for this channel now` |
| T12.10 | Consumo | `ps` durante 20 s | — | HOST | SX3: 2–4 % de un núcleo, ~78 MB. HLS TEST: 4,5–7 %, ~108 MB. Detalle en server/README.md |
| T12.14 | Caudal de la red hacia la placa | `[NET]` en todas las pruebas; ping del Mac a la placa | Margen sobre el caudal del canal | REQUIRES HARDWARE TEST | **Riesgo principal.** Ping en reposo de 4–26 ms sin pérdidas, pero bajo carga la placa ha recibido de ~20 a ~190 KB/s según el momento. Justo después de arrancar suele ir lenta durante 15–20 s: el primer canal (el demo de archivo o SX3) no llega a cebar 160 KB en 8 s (`NO DATA`) y la tele reintenta sola. La causa probable es la ventana TCP de lwIP de 4 segmentos (5760 B, fija en el core de Arduino): con cualquier pérdida no hay retransmisión rápida y se espera al timeout. SX3 ha pesado de ~75 a ~155 KB/s según el programa. Probado sin efecto: desactivar 802.11b (empeoró, revertido). **Medido en la V0.2c (T13.5–T13.8):** la red caliente sobra (511 KB/s por conexión); las esperas venían de la fuente (FFmpeg en el borde del directo) y los descartes, de la CPU a 24 fps. El arranque lento tras conectar se confirma (60 KB/s) |
| T12.15 | Errores de SD con la Wi-Fi cargada | Buscar `sdmmc_read_blocks failed` durante las pruebas remotas | Ninguno | REQUIRES HARDWARE TEST | Vigilar: 3 en una pasada de 25 min con directo y zapeos a la SD; ya se vio 1 vez en la V0.2a. Posible alimentación (radio sin ahorro de energía y SD a 40 MHz). No afectó a la reproducción |
| T12.16 | Tele que zapea mientras arranca un directo | `POST` cerrado a los 0,3 s con un FFmpeg falso que tarda 2,5 s | Ni sesión ni proceso | HOST | OK 2026-09-30. Encontrado: el servidor terminaba de arrancar FFmpeg y creaba una sesión huérfana 15 s. Arreglado |
| T12.17 | Conexión con un SYN perdido | Primera sintonía tras reiniciar el servidor | Conecta | REQUIRES HARDWARE TEST | OK 2026-09-30. Encontrado: con 1,5 s de `connect`, un SYN perdido (lwIP lo reenvía a los 3 s) daba `NO SERVER`. Ahora 4 s |
| T12.11 | Canal de 3Cat desde la SD | Entrada `remote` a `/channel/10` en `channels.json` | Estática al zapear, OSD `CH NN SX3`, imagen | REQUIRES HARDWARE TEST | Pendiente (probado solo con `T`) |
| T12.12 | Directo largo | SX3 1 h o más con `S0` | Sin fugas, sin `network_timeouts` | REQUIRES HARDWARE TEST | OK 2026-09-30, oficina, sintonizado desde el mando web (`20fps-q14`, 3 segmentos atrás): 1 h 00 min, 72.226 fotogramas, **0 descartes, 0 esperas, 0 reconexiones, 0 timeouts**, A/V 1 ms de media (máx. 42); heap 124 → 125 KB, PSRAM estable (el mínimo de 90 KB fue un pico de las pruebas de logos, 6 conexiones a la vez); el mando respondió en las 60 comprobaciones (71 ms de media); 0 errores de SD, un solo arranque |
| T12.13 | Canal 3Cat que no emite | Un código sin stream en la API (p. ej. `PUCFC3`, el código interno de Doraemon; con su código de página, `fc3`, sí emite) en la tele | NO SIGNAL `CHANNEL OFF`, sin bloquear | REQUIRES HARDWARE TEST | Pendiente en la tele (en el servidor: 503 con motivo) |

## V0.2c — Tuning de red del directo

Medidas y conclusiones completas en [NETWORK_TUNING.md](NETWORK_TUNING.md). Herramienta: `tools/stream_profiles.py`,
en dos ejecuciones el 2026-09-30 (placa FNK0104A a −56…−64 dBm, servidor en el Mac, canal 10 = 3Cat SX3).

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T13.1 | Tests en el ordenador | `tools/run_host_tests.sh` | `fps` y `prebuffer_ms` de la sesión (defecto, límites, inválidos), contador de inicios JPEG partido entre lecturas | HOST | OK 2026-09-30: 343 checks |
| T13.2 | Tests del servidor | `pytest`; `server/tools/test_hls_pipeline.py` | Perfiles en el comando de FFmpeg, `fps`/`prebuffer_ms` en la sesión, perfil por canal, validación, `/api/bench` acotado; el directo de punta a punta a los fps del perfil | HOST | OK 2026-09-30: 70 passed; pipeline 9/9 (263 fotogramas a 20 fps, vídeo ×0,97, audio ×1,00) |
| T13.3 | Build | `platformio run`, con y sin `PAUTV_DEBUG_STATS` | 0 warnings | HOST | OK 2026-09-30: 1.085.581 B de flash, RAM 112.444 B con depuración (+4,4 KB: el búfer estático del comando `B`; el de TX del USB, 4 KB, sale del heap); 1.078.901 B y 108.160 B sin ella |
| T13.4 | Métricas en la placa | Sintonizar SX3 y conectar la Wi-Fi | `[TUNE]`, `[NET]` cada segundo 30 s, `[WIFI] +Ns rssi` 30 s, `[STATS]` en el servidor | REQUIRES HARDWARE TEST | OK 2026-09-30. Encontrado: el búfer de TX del USB (256 B) cortaba las líneas largas; ahora 4 KB |
| T13.5 | Caudal bruto | Fase `bench` | — | REQUIRES HARDWARE TEST | 2026-09-30: al conectar, 60 KB/s (SRTT 58 ms, ping p95 289 ms, re-tx 0,85 %). En caliente: 1 conexión 511 KB/s (SRTT 8,6 ms), 2 → 747 KB/s, 4 → 1005 KB/s, re-tx 0 %. La ventana limita cada conexión, pero deja ~5× de margen |
| T13.6 | Perfiles | Fases `profiles` y `livestart` | Perfil sin descartes ni esperas | REQUIRES HARDWARE TEST | 2026-09-30. Empezando en el segmento más nuevo (−1): huecos de la fuente de 1–5 s cada 6 s y esperas en casi todos los perfiles. Tres segmentos atrás (−3): huecos ≤ 1 s. Con −3: `24fps-q12` da 55 descartes/min; `20fps-q14` y `20fps-q18`, 0 descartes y 0 esperas, estables en ~3 s |
| T13.7 | Prebuffer | Fase `prebuffer` (20fps-q14, −3) | Arranque frente a estabilidad | REQUIRES HARDWARE TEST | 2026-09-30: 1,0 / 2,0 / 3,0 s → primer vídeo en 3,0 / 3,2 / 4,4 s; los tres con 0 descartes y 0 esperas |
| T13.8 | Primera sintonía tras conectar | Fase `cold` (20fps-q14, −3) | Imagen sin `NO DATA` | REQUIRES HARDWARE TEST | OK 2026-09-30 (una muestra): imagen a los 8,5 s con el enlace frío (ping p95 223 ms) y después 0 descartes y 0 esperas. Antes (T12.8, T12.14) fallaba con `NO DATA` |
| T13.9 | Errores de SD con Wi-Fi | Fase `sd`: 8 min alternando SX3 y SD, 3 min solo SD con 20 zapeos, más todas las fases | `sdmmc_read_blocks failed` con su contexto | REQUIRES HARDWARE TEST | 2026-09-30: ejecución 2 (58 min, 45 sintonías de la SD, ~50 min de directo): 0 errores. Ejecución 1 (20 min): 1 ráfaga de 3 timeouts (`0x107`) al abrir un capítulo del CH06, 10 s después de conectar la Wi-Fi y justo tras el bench, con la red ya en reposo y RSSI −58…−60. El capítulo salió como `missing` (`NO SIGNAL: FILE ERROR`, siguiente canal a los 3 s); el archivo existe y se reprodujo en la ejecución 2. Como en T11.13 y T12.15: siempre al abrir, nunca reproduciendo, y con la Wi-Fi recién activa |
| T13.10 | Soak de directo | Fase `soak`: 30 min de SX3 con `S0` | Sin fugas, sin esperas ni reconexiones | REQUIRES HARDWARE TEST | OK 2026-09-30 (20fps-q14, −3): heap 144 → 144 KB (mínimo 134 KB, sin cambios durante el soak), bloque más grande 127 KB, PSRAM 7477 → 7481 KB; 36.005 fotogramas, 0 descartes, 0 esperas, 0 reconexiones, A/V 4–5 ms de media (máx. 46), tareas vivas en cada minuto |

## Wi-Fi con varias redes

La tele recuerda hasta 8 redes (`wifi.json` con `networks`, o `PAUTV_WIFI_NETWORKS` en `secrets.h`) y se une sola a
la que tenga a su alcance.

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T14.1 | Lógica pura | `tools/run_host_tests.sh` | `wifi.json` de una red y en lista, entradas malas o repetidas descartadas, máximo 8, contraseña de 0 u 8–63 caracteres, orden: redes vistas de más a menos fuerte y luego las no vistas | HOST | OK 2026-09-30: 363 checks (20 nuevos) |
| T14.2 | Build | `platformio run`, con y sin depuración | 0 warnings | HOST | OK 2026-09-30: 1.089.029 B de flash, RAM 113.316 B (+872 B, la lista de redes); 1.082.417 B y 109.032 B sin depuración |
| T14.3 | Arranque en otra red | Casa + oficina en `secrets.h`, arrancar en la oficina | Escanea, elige la de la oficina sin probar antes la de casa, conecta | REQUIRES HARDWARE TEST | OK 2026-09-30, dos arranques: 14 y 21 redes vistas en 3,0 y 3,6 s, la oficina a −49/−51 dBm en el canal 6, conectada en 1,6 y 3,3 s. Encontrado y arreglado: (1) el escaneo asíncrono lanzado nada más encender la Wi-Fi no terminaba nunca y dejaba la siguiente conexión fallando como "wrong password?"; ahora espera a que la estación esté arrancada y, si un escaneo se atasca, lo para. (2) Con 120 ms por canal, el core daba el escaneo por fallido a los 2,4 s; ahora 300 ms (hasta 6 s) |
| T14.4 | Red de 5 GHz | Añadir la variante `_5G` de la oficina | El escaneo (2.4 GHz) no la ve; se prueba la última y da "network not visible" | REQUIRES HARDWARE TEST | OK 2026-09-30 (en los arranques con el escaneo atascado, antes del arreglo) |
| T14.5 | Una sola red | Solo la de casa, en casa | Sin escaneo, como antes | REQUIRES HARDWARE TEST | Pendiente (el camino no cambió; probar al volver a casa) |
| T14.6 | Cambio de sitio | Encender en casa y luego en la oficina sin tocar nada | Conecta en cada sitio a su red | REQUIRES HARDWARE TEST | OK 2026-09-30: de la oficina a casa sin tocar nada: la búsqueda vio la red de casa (dos puntos de acceso, −56 dBm, canal 2) y conectó. Los canales en directo fallaban: el servidor del Mac seguía anunciando `retrotv-server.local` con la IP de la oficina. Arreglado: el anuncio sigue los cambios de dirección (cada 15 s) |

## Mando a distancia web

La tele sirve la página en `http://retrotv.local` (puerto 80) a los móviles de la misma Wi-Fi.

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T15.1 | Lógica pura | `tools/run_host_tests.sh` | Nombres de tecla válidos e inválidos (`menu` no), número de canal 1–999 (0–999 desde T18.6) solo con dígitos, JSON de estado con nombres escapados, lista solo con canales activos, nunca un JSON cortado | HOST | OK 2026-09-30: 397 checks (34 nuevos) |
| T15.2 | Página | Página con una API falsa en Chrome headless, a 375 y 320 px, claro y oscuro; `node --check` del JS | Sin scroll horizontal, todo legible, contraste ≥ 4,5:1 | HOST | OK 2026-09-30: cabe en 320 px (los nombres largos se cortan con "…"). Contraste: el gris de los nombres pasó de ~4,3:1 a 5,8:1; el resto, 5,1–8,5:1 |
| T15.3 | Build | `platformio run`, con y sin depuración | 0 warnings | HOST | OK 2026-09-30: 1.134.421 B de flash (+45 KB: servidor HTTP, nombre mDNS y página), RAM 113.388 B; 1.127.889 B y 109.104 B sin depuración |
| T15.4 | En la placa, desde el Mac | API y página contra la placa | Responde, el estado sigue a la tele, el salto pasa por la estática | REQUIRES HARDWARE TEST | OK 2026-09-30: página de 7.270 B en 320 ms; `next` llega como `CH_NEXT` en ~200 ms; salto al 20 (Studio Ghibli) y al 22 (SX3: imagen en 3,9 s); canal 99 ignorado; Chrome pinta `CH 22 · SX3 · MUTE` y la lista, por IP y por nombre. **Encontrado y arreglado:** resolver `retrotv.local` tardaba 5 s en cada consulta (el Mac preguntaba también IPv6 y la placa no respondía). Ahora tiene una IPv6 link-local y resuelve en 4–7 ms |
| T15.5 | Sin cabecera | `POST /api/key?k=next` sin `X-RETROTV` | 403 y la tele no cambia | REQUIRES HARDWARE TEST | OK 2026-09-30 (y 400 con una tecla inválida) |
| T15.6 | Desde el móvil | Abrir `http://retrotv.local` en el móvil | Carga y responde al tacto | REQUIRES HARDWARE TEST | 2026-09-30, primera prueba del usuario: la página cargaba pero **cada botón daba SIN SEÑAL**. Dos causas, arregladas: (1) con 3 sockets y keep-alive, el servidor cerraba la conexión más antigua y el navegador mandaba el POST por ella (un GET lo reintenta solo; un POST no). Ahora cada respuesta lleva `Connection: close` y hay hasta 5 sockets; reproducido con 4 conexiones antes y después. (2) **El loop de la tele se congelaba** con el USB enchufado a un ordenador y el puerto cerrado: ver T15.8. **OK tras los arreglos:** el usuario confirma que los botones funcionan en el móvil |
| T15.9 | Logos de los canales | PNG de 360×160 transparente en `/retrotv/logos/<id>.png`; página con una API falsa en Chrome headless; luego en la placa | Logos en los botones, nombre si no hay; sin coste en la reproducción | HOST + REQUIRES HARDWARE TEST | OK 2026-09-30. Host: 400 checks (`logo` en la lista solo para los canales con logo); cabe en 375 y 320 px. Placa: 16 logos, 126 KB en PSRAM; los 16 idénticos a los archivos, `image/png` con `max-age`; 404 para un canal sin logo; servidos uno a uno en 8,8 s mientras Cowboy Bebop sonaba desde la SD a 24 fps sin descartes |
| T15.11 | Diseños del mando | `?skin=` con cada diseño en Chrome headless (API falsa y contra la placa); `node --check`; el ciclo de MANDO simulado; contraste medido | CLÁSICO, NEGRO, PLATA y GRIS con la colocación de su mando de referencia; MANDO y el deslizamiento pasan al siguiente; el móvil recuerda el último; texto pequeño ≥ 4,5:1 | HOST + REQUIRES HARDWARE TEST | OK 2026-10-02 en el ordenador: los cuatro dibujados (CLÁSICO intacto tras arreglar las áreas de la rejilla), ciclo `'' → negro → plata → gris → ''`, un valor desconocido vuelve a CLÁSICO; contraste ≥ 4,5:1 salvo el borde superior de la píldora azul (4,2:1, el texto va en el centro, 5,7:1). En la placa, página servida tras flashear. Pendiente: deslizar en el móvil |
| T15.12 | Logos en una tinta | `/api/logo?n=…&c=black` y `&c=white`; `parseLogoInk` en el ordenador | La versión pedida o, si falta, la de color; nombres exactos (`BLACK` es color) | HOST + REQUIRES HARDWARE TEST | OK 2026-10-02 en el ordenador (541 checks). Placa: pendiente de copiar las 84 imágenes a la SD |
| T15.10 | Órdenes durante el arranque | `mute` por la web mientras la tele arranca | Se aplica al empezar a reproducir | REQUIRES HARDWARE TEST | OK 2026-09-30: enviado en `starting` (204, en cola) y aplicado al llegar a `playing`. Antes se perdía |
| T15.8 | Tele sin nadie leyendo el USB | Enchufada al Mac con el puerto serie cerrado, 90 s, luego órdenes por la API | El loop sigue vivo (un `mute` cambia el estado) | REQUIRES HARDWARE TEST | OK 2026-09-30 tras el arreglo: vivo a los 90 s, 12/12 órdenes, vivo otros 90 s después. Antes: congelado al minuto (la cola de órdenes llena, 503). Causa: `setTxTimeoutMs(0)` en el core 2.0.17 hace que `HWCDC::write` cuente sus reintentos desde 0 hacia abajo, el contador da la vuelta y espera para siempre cuando el búfer del log se llena y nadie lo lee. Ahora 1 ms. Afectaba también a los mandos y al zapping, no solo a la web |
| T15.7 | Mando y reproducción | Usar el mando durante un directo | Sin descartes ni esperas nuevas | REQUIRES HARDWARE TEST | Pendiente |

## V0.2d — RTVE

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T16.1 | Provider con respuestas simuladas | `pytest tests/test_rtve.py` | 24h resuelve el manifiesto oficial (360p, audio castellano); DRM o login dejan el canal offline sin pedir el stream; página cambiada, dato de login ausente o redirección fuera de `rtve.es`: offline | HOST | OK 2026-09-30: 12 passed (82 en total) |
| T16.2 | RTVE real | Resolver 24h, Clan y La 1 contra RTVE | 24h resuelve; Clan pide login; La 1 lleva DRM | HOST | OK 2026-09-30, tal cual |
| T16.3 | Sesión real de 24h | Servidor en otro puerto, `POST /api/sessions/11` | Vídeo y audio | HOST | OK 2026-09-30: lista en 1,5 s (primer vídeo a los 1,2 s). 150 s leyendo como la tele (perfil `20fps-q14`, 3 segmentos atrás): 1237 kbps de media, p95 1409, sin ningún segundo sin datos, sin reinicios. Pesa más que SX3 (~870 kbps): informativo con rótulos |
| T16.5 | Bandas negras limpias | Foto del usuario: rayas de colores en las bandas de SX3. Medido en 20 s del MJPEG del servidor | Bandas a 0 | HOST + REQUIRES HARDWARE TEST | Causa: el borde de la banda (fila 30) caía a mitad de un bloque JPEG de 16 filas y el ruido de la imagen se colaba (hasta 46/255 en las filas 16–29 y 210–223; los bloques enteros de banda, ≤ 5). Arreglo en el servidor y en `convert_video.sh`: imagen cortada a bloques enteros y colocada en sus bordes (16:9 → filas 32–207). Medido tras el arreglo: bandas a 0 decodificando como la tele. **En la placa: OK**, el usuario confirma que las bandas de SX3 se ven limpias. Los capítulos 16:9 ya convertidos en la SD conservan el problema hasta reconvertirlos |
| T16.6 | Canales 24 h de 3Cat y 24h en la parrilla | SD: 23 24h, 24 3Cat Doraemon, 25 3Cat Anime, 26 Plats bruts, 27 Joc de cartes, 28 Vinagre(ta), todos `remote` a `retrotv-server.local:8080/channel/11–16`, con logo | En la lista del mando con logo; Doraemon se ve desde el mando | REQUIRES HARDWARE TEST | OK 2026-09-30: 28 canales, 22 logos (163 KB en PSRAM); Doraemon desde el mando: imagen en 7,7 s, 19,3 fps, 0 descartes, 0 esperas, A/V 0 ms. Ghibli reconvertida con las bandas limpias: 24 fps, 0 descartes |
| T16.7 | Provider Pluto TV | `pytest tests/test_pluto.py tests/test_hls_playlist.py tests/test_ffmpeg.py` | Sesión anónima reutilizada hasta `refreshInSec`; guía con token; solo AES-128 (no SAMPLE-AES) y solo en Pluto; hosts fuera de `pluto.tv` rechazados; variantes sin códecs con audio; `crop_4_3` antes de escalar | HOST | OK 2026-09-30: 94 passed en total |
| T16.8 | Pluto TV real | Sesiones reales de 8 s: Dragon Ball, One Piece, Pluto TV Anime, Lupin, Érase una vez… | Vídeo y audio descifrados | HOST | OK 2026-09-30: listas en 4,0–8,9 s, sin reinicios; fotogramas válidos (22/22 decodificados en Dragon Ball). Dragon Ball y Lupin llegan en 4:3 con bandas laterales: con `crop_4_3` llenan la pantalla |
| T16.4 | 24h en la placa | `T http://retrotv-server.local:8080/channel/11`, 60 s | Imagen, sonido y el perfil sin descartes | REQUIRES HARDWARE TEST | OK 2026-09-30: imagen en 3,9 s, 19,3 fps, 0 descartes, 0 esperas, A/V 7 ms (máx. 50), 1,2–1,4 Mbps |

## Ajustes desde el mando web

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T17.1 | Lógica pura | `tools/run_host_tests.sh` | Código de 4 cifras, 60 s, un solo uso, 5 intentos; token de 32 hex que se prolonga con el uso y caduca a los 30 min; cuerpos validados; `wifi.json` pasa al formato de lista, misma red = nueva contraseña, máximo 8; la lista para el móvil nunca lleva contraseñas; `channels.json` cambia solo `enabled` y conserva el resto, un canal por línea; canal desactivado saltado al zapear | HOST | OK 2026-09-30: 456 checks |
| T17.2 | En la placa | Script contra la placa (sin nadie mirando la pantalla: el código se lee del log USB en la versión de depuración) | Todo lo de abajo | REQUIRES HARDWARE TEST | OK 2026-09-30, 25/25: sin emparejar 401; `pair/start` sin cabecera 403; código mal 403, bien 200 con token, repetido 403; token falso 401; info, redes, pantalla y canales; añadir y borrar una red de prueba; contraseña corta 400; borrar una red de `secrets.h` 409; búsqueda con 14 redes; brillo 5 → 400; desactivar el 28 lo quita del mando y activarlo lo devuelve; canal inexistente 404; reinicio 202 y la tele vuelve a arrancar. Ninguna respuesta llevó una contraseña |
| T17.3 | Página | Chrome headless con datos simulados, 320 y 375 px | Emparejar y panel legibles, sin scroll horizontal | HOST | OK 2026-09-30 |
| T17.5 | Canal del mando (QR) | Canal `internal` `mando`; mirarlo y escanearlo con el móvil | QR grande legible, la dirección escrita, abre el mando | REQUIRES HARDWARE TEST | OK 2026-09-30, escaneado por el usuario. Antes del arreglo, en silencio, la insignia MUTE tapaba una esquina del QR (foto del usuario) y el móvil no lo leía; ahora en este canal MUTE solo sale 1 s al pulsar |
| T17.6 | 48 canales | `MAX_CHANNELS` 32 → 48, máscara de logos de 64 bits | Tests verdes; RAM +3,4 KB | HOST | OK 2026-09-30: 456 checks; RAM 117.244 B |
| T17.4 | Desde el móvil | El usuario empareja y cambia algo | Funciona | REQUIRES HARDWARE TEST | OK 2026-09-30, hecha por el usuario. El código no sale en el teletexto ni en el menú de ajustes de la tele (ahí no hay OSD): emparejar desde otro canal |

## SD bajo la Wi-Fi, código en el teletexto y canal 0

Comando serie `D <s> <carpeta> [<url de /api/bench>] [scan]`: lee un capítulo a tope, 4 KB cada vez como el
reproductor, mientras la Wi-Fi recibe y/o busca redes cada 10 s; cada fallo vuelve a montar la tarjeta.

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T18.1 | SD + Wi-Fi recibiendo | `D 600 …/studio_ghibli http://<mac>:8080/api/bench?seconds=30`, 40 MHz | Sin fallos | REQUIRES HARDWARE TEST | OK 2026-09-30: 600 s a 5,2 MB/s con 344 KB/s de red, 0 fallos |
| T18.2 | SD + búsquedas de redes | Lo mismo con `scan` | Reproducir los timeouts vistos desde la V0.2 | REQUIRES HARDWARE TEST | Reproducido 2026-09-30: sin recuperación, un timeout a los 431 s dejó la SD muerta (después todos los canales FILE ERROR / NO EPISODES hasta la carta). Con recuperación, 1200 s a 40 MHz: 22 fallos, 22 recuperados al volver a montarla |
| T18.3 | 20 MHz | Lo mismo con la SD a 20 MHz | Menos fallos | REQUIRES HARDWARE TEST | No ayuda: 7 fallos en 275 s. Una vez falló otra vez justo después de volver a montarla, así que ahora se permiten 3 seguidos. Se queda a 40 MHz |
| T18.4 | Recuperación en la reproducción | Fallo a mitad de capítulo o al abrir | Vuelve a montar la SD y sintoniza donde va el programa | REQUIRES HARDWARE TEST | Pendiente de verlo en la reproducción normal (no se puede provocar a voluntad; el camino es el mismo que en T18.2) |
| T18.5 | Código de emparejar en el teletexto | Pedir el código con la tele en el 01 | Franja roja "CODI DEL MANDO NNNN" sobre el pie, 60 s o hasta emparejar | REQUIRES HARDWARE TEST | Pendiente de verlo en la pantalla |
| T18.6 | Canal 0 | `number: 0` en `channels.json`, tutorial de 60 s convertido con `convert_video.sh` | Carga, primero en el zapping, `?n=0` en el mando, "CH 00" | HOST + REQUIRES HARDWARE TEST | OK 2026-09-30: 481 checks; en la placa 34 canales, el 00 en la lista del mando, sintonizado desde el mando: 24 fps, 0 descartes |

## Carcasa tele90: cuatro teclas y LED

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T19.1 | Lógica de las teclas | `tools/run_host_tests.sh` | Pulsar actúa al soltar, sin esperar el doble clic; dos pulsaciones son dos órdenes; mantener actúa una vez; CH+ mantenida = ajustes, VOL− mantenida = silencio | HOST | OK 2026-09-30: 489 checks |
| T19.2 | Teclas en la placa | Pulsadores entre GPIO2/3/14/21 y GND | `[INPUT] CH_PREV`, `CH_NEXT`, `VOL_DOWN`, `VOL_UP`; mantener CH+ `MENU`, mantener VOL− `MUTE` | REQUIRES HARDWARE TEST | Pendiente de cablear |
| T19.3 | LED | LED + 1K en GPIO43 | Encendido; se apaga un instante con cada orden (teclas y mando web) | REQUIRES HARDWARE TEST | Pendiente. En STANDBY VOZ el usuario vio su destello (TV10) |
| T19.5 | Batería: lógica | `tools/run_host_tests.sh` | Curva LiPo monótona, 3825 mV = 50 %; un aviso al pasar a baja y otro a crítica, ninguno con lecturas que oscilan en el 15 %; baja al arrancar también avisa; JSON `battery` / `battery_low` | HOST | OK 2026-09-30: 504 checks |
| T19.6 | Batería en la placa | Sin LiPo, con USB | `[BOOT] battery ~4.1 V`, `/api/state` con `battery` ~90 y `battery_low: false`; pila en la barra del canal | REQUIRES HARDWARE TEST | OK 2026-09-30: 4136 mV, `battery: 92`; la pila en la barra del canal, vista por el usuario. Con la LiPo: comprobar el porcentaje, el aviso al 15 % y la carga |
| T19.4 | microSD con alargador | `D 1200 … scan` con la carcasa montada | Timeouts recuperados; comparar con T18.2 (22 en 20 min) | REQUIRES HARDWARE TEST | Solo si se monta un alargador (era de la v5; el modelo v9 no describe ninguno) |

## Vídeo al encender

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T20.1 | Conversión sin audio | `convert_video.sh` con un MP4 sin pista de audio | Solo `.mjpeg` (+ `.idx`), aviso "NO AUDIO in the source"; con audio y PISTA equivocada sigue fallando | HOST | OK 2026-09-30: `retrotv-intro.mp4` (5 s, sin audio) → `intro.mjpeg` 448 KB |
| T20.2 | Intro en la placa | `/retrotv/system/intro.mjpeg` en la SD, encender | Negro al encender (nunca la tarjeta de arranque), la intro con su sonido, y al acabar la transición de canal: `BOOT -> CHANNEL_SWITCH -> PLAYING` y el último canal. Sin el archivo, arranque como antes | REQUIRES HARDWARE TEST | OK en el log 2026-09-30 (intro 4:3 sin barras, con audio): `intro /retrotv/system/intro.mjpeg`, `BOOT -> CHANNEL_SWITCH -> PLAYING`, CH05 en emisión. Verla y oírla: pendiente del usuario |
| T20.3 | Saltar y silenciar | Una tecla durante la intro; MUTE durante la intro | Cualquier tecla la salta; MUTE la silencia y sigue | REQUIRES HARDWARE TEST | OK 2026-09-30 por serie: MUTE no la corta; CH+ 1 s después la corta (canal a los 1,1 s) |

## Transición siempre entre canales

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T21.1 | Zapeo a un canal en directo | Mando web → 22 (SX3) | Estática con siseo desde el zapeo hasta que hay señal, destello y la imagen, sin estática muda | REQUIRES HARDWARE TEST | OK en el log 2026-09-30 (en silencio): estática 0,2 s → sintonizando → señal lista a los 6,9 s → destello → `playback started` a los 7,0 s. Oír el siseo: pendiente del usuario |
| T21.2 | Primer canal al encender | Con intro y sin ella | `BOOT -> CHANNEL_SWITCH` / `HOME -> CHANNEL_SWITCH` | REQUIRES HARDWARE TEST | Con intro OK 2026-09-30; sin intro, pendiente |

## Reposo (encender y apagar con batería)

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| T22.1 | Lógica | `tools/run_host_tests.sh` | CH− mantenida 2 s = POWER (1,5 s sigue siendo canal anterior); `power` en el mando web | HOST | OK 2026-09-30: 508 checks |
| T22.2 | Apagar | Serie `q` (= mantener CH−) | `[POWER] standby`, `sleeping until a key is pressed`; el USB desaparece, la web no responde; pantalla negra y sin retroiluminación | REQUIRES HARDWARE TEST | OK 2026-09-30: en el log (USB y web desaparecen) y en la pantalla, comprobado por el usuario |
| T22.3 | Encender | BOOT (luego las teclas) | Arranque con intro y el último canal; la tecla no cuenta como orden | REQUIRES HARDWARE TEST | OK 2026-09-30 con BOOT (usuario). Con las teclas, pendiente de soldarlas |
| T22.4 | Consumo en reposo | Amperímetro en la batería | Unos pocos mA como mucho (la placa tiene partes que no se apagan) | REQUIRES HARDWARE TEST | Pendiente de medir |

## RETROTV Voice (docs/VOICE.md) — firmware `pio run -e voice`

| # | Prueba | Cómo | Esperado | Tipo | Resultado |
|---|---|---|---|---|---|
| TV0 | Lógica | `tools/run_host_tests.sh` (`test/voice_tests.cpp`) | Niveles, detector de palmadas (silencio, ruido, sonido largo, eco, doble, triple, ventana, 160 ms no es doble, fondo dinámico, sensibilidad, sonidos propios, envolvente del programa, paso alto, vuelta de `millis()`) | HOST | OK 2026-10-02: 591 checks (+50) |
| TV0b | Firmware normal intacto | `pio run` con todo OFF | Compila sin warnings; mismo comportamiento | HOST | OK 2026-10-02: RAM +40 B, flash +1 KB (pantalla MICROFONO sin usar) |
| TV1 | Captura del micro | Serie `v` (MIC TEST) | `[MIC] ready …`; bloques sin `overrun` ni `clip`; los dos slots iguales | REQUIRES HARDWARE TEST | OK 2026-10-02: 86 bloques/s, 0 errores. Con REG16 a 0 dB la sala daba −80/−89 dBFS; a +24 dB (valor de fábrica) −58/−67 |
| TV2 | Medidor VU | AJUSTES → DIAGNOSTICO → CH− | La barra sigue a la voz y a las palmadas | REQUIRES HARDWARE TEST | OK 2026-10-02: "se mueve claramente la barra" (usuario), abierta por serie (`M,n,n,n,n,+` y `p`) porque aún no hay teclas |
| TV3 | Ruido de fondo | MIC TEST con la sala en silencio | Fondo estable, sin recortes | REQUIRES HARDWARE TEST | OK 2026-10-02: −58 a −72 dBFS en silencio; −27 a −50 con la tele sonando |
| TV4 | Detector | Palmadas sueltas, dobles y triples a 0,5–1 m | `[CLAP] …` con hueco y pico | REQUIRES HARDWARE TEST | OK 2026-10-02: 43 secuencias reales, huecos 210–691 ms, picos −11 a −46 dBFS |
| TV5 | Doble palmada | Viendo un canal | Apagado CRT y STANDBY VOZ; dos palmadas más la encienden | REQUIRES HARDWARE TEST | Antes silenciaba: OK 2026-10-02. Apagar: **FALLO** 2026-10-02 con el límite de brillo del 30 % (la segunda palmada de cada doble, 21–28 %, se descartaba como golpe sordo) → límite 18 %: OK 2026-10-02, la doble del usuario (384 ms, 22 %) la apagó y dos palmadas con 12 s de silencio antes la encendieron |
| TV6 | Triple palmada | Viendo un canal | Canal siguiente, sin silenciar antes | REQUIRES HARDWARE TEST | OK 2026-10-02 |
| TV7 | Palmadas con la tele sonando | Doble palmada con sonido | Funciona; los chasquidos propios no cuentan | REQUIRES HARDWARE TEST | OK con límites (2026-10-02): funciona con fondo de hasta ~−40 dB; con el programa muy alto (fondo −27 dB) hace falta una palmada muy fuerte. El chasquido al silenciar daba palmadas falsas (una doble a 161 ms, una triple a 144 ms): corregido (350 ms tras silenciar/volumen, 180 ms mínimos) |
| TV8 | Falsos positivos 30 min, tele sonando | Canal con sonido, sin tocar nada | 0 dobles/triples | REQUIRES HARDWARE TEST | 2026-10-02, Samurai Champloo al 75 %: 1.ª versión, 1 doble falsa en 3 min (puñetazos a 624 ms) → descarte por la envolvente del programa; 2.ª versión, 30 min: 1 doble falsa (la canción de la intro: caja sobre bajo), 168 golpes propios descartados → envolvente medida por encima de 400 Hz; repetida solo esa intro desde 0:00 (`T !ruta`): 0 dobles, 9 golpes descartados. **Pendiente** una prueba larga completa con la versión final; límite de brillo 18 % (la doble apaga la tele): la misma intro desde 0:00 al 75 %, 3 min 50 s, 0 dobles, 1 palmada suelta, 26 golpes sordos (3–18 %), 20 fps sin pérdidas; prueba de 30 min 2026-10-03: el capítulo entero y su vuelta, 30 min al 75 %: **0 dobles, 0 apagados**, 3 palmadas sueltas del programa (28–38 %), 146 golpes sordos (3–18 %), 20 fps, 0 fotogramas perdidos, sin reinicios. Es **una escena**: falta validar con otros programas, volúmenes y salas durante horas |
| TV9 | Standby por voz | AJUSTES → VOZ → APAGADO = STANDBY VOZ; ⏻ en el mando web (o serie `q`) | Apagado CRT; pantalla, retroiluminación, piloto, altavoz y Wi-Fi apagados; `[STANDBY] voice standby: listening` | REQUIRES HARDWARE TEST | OK 2026-10-02: por serie y por el usuario; serie `W` despierta (prueba sin teclas) |
| TV10 | Una palmada en standby → LED | Una palmada a 0,5–1 m | Destello del piloto (120 ms); sigue apagada | REQUIRES HARDWARE TEST | OK 2026-10-02: dos palmadas sueltas, destello visto por el usuario, la tele siguió apagada. El micro funciona con la CPU a 80 MHz |
| TV11 | Palmadas en standby → encender | Silencio, dos palmadas, silencio | Se enciende ~1,2 s después: intro, último canal, volumen guardado | REQUIRES HARDWARE TEST | 1.ª versión (dos golpes, al momento): OK 3 de 3, pero los ruidos también la encendían (TV13c). 3.ª versión (dos palmadas comprobadas, 2,5 s de silencio antes y 1,2 s después): OK 2026-10-02, encendió con la doble del usuario (2,7 s de silencio antes) tras 5 golpes sueltos que no la encendieron; el usuario la dio por buena |
| TV12 | Consumo en standby por voz | Medidor USB o amperímetro en la batería; o, sin medidor, la batería por `/api/state` antes y después de una noche en STANDBY VOZ | mA en STANDBY VOZ | REQUIRES HARDWARE TEST | **Estimado** 2026-10-03, sin medidor: sin USB y con un canal puesto, 72 % (3980 mV) a las 02:22 → noche en STANDBY VOZ → 64 % (3921 mV) a las 08:28, igual de cargada. 8 % de 3000 mAh en ~6 h = **~40 mA** (30–50 mA por la resolución del 1 %): ~3 días desde llena. Falta un medidor para el número exacto y para PLAYING y deep sleep |
| TV13 | Apagado AHORRO MÁXIMO (deep sleep) | APAGADO = AHORRO MAX (por defecto) | Igual que siempre (T22) | REQUIRES HARDWARE TEST | Mismo código que T22 tras separarlo en `powerDown()` + `deepSleep()`; sin volver a probar en la placa; OK 2026-10-03: con APAGADO = AHORRO MAX, ⏻ (serie `q`) → apagado CRT, `sleeping until a key is pressed`, el USB se suelta; BOOT la despierta: intro, CH10, volumen 60 |
| TV13b | Standby por voz con la batería agotada | — | Deep sleep, no se queda escuchando | REQUIRES HARDWARE TEST | Lógica en el código (`battery_.empty()` en el bucle); sin probar |
| TV7b | Golpes en la mesa con la tele encendida | Nudillos en la mesa, sueltos | Ninguno cuenta como palmada | REQUIRES HARDWARE TEST | OK 2026-10-02 (límite 18 %): 14 golpes sordos medidos entre 4 y 15 % de agudos, todos descartados; 4 golpes sueltos muy agudos (66–91 %) contaron como palmada suelta, sin efecto; ninguna doble |
| TV13c | Falsos encendidos en standby | STANDBY VOZ con golpes, alarmas y ruidos de casa | No se enciende | REQUIRES HARDWARE TEST | **FALLO** 2026-10-02 (1.ª versión): "se enciende con cualquier golpe, alarma o ruido" (usuario). 2.ª versión (doble comprobada: ritmo, fuerza, brillo, −36 dBFS), probada: paró golpes, una pareja desigual y sueltos, pero **un ruido de casa la encendió** (pareja −13/−17 dB, 52/66 % de agudos). 3.ª versión: además, 2,5 s sin golpes antes y ~1,2 s después (la pareja falsa llegó 1,6 s tras otro golpe); el usuario descartó las tres palmadas. Probada en una sesión corta: 5 ruidos sueltos, ningún encendido. Falta una prueba larga con ruidos de casa. 2026-10-03: una noche (~6 h) en STANDBY VOZ sin encenderse sola, deducido de la batería (72 → 64 %), sin log |
| TV14 | Wake word | — | — | REQUIRES HARDWARE TEST | Bloqueado: ESP-SR no está en Arduino 2.0.17; integrarlo cambia el sistema de compilación (sin permiso, no). Ver docs/VOICE.md |
| TV15 | Comandos de voz | — | — | REQUIRES HARDWARE TEST | Bloqueado: MultiNet no tiene español; ver docs/VOICE.md |
| TV16 | Grabadora | AJUSTES → VOZ → GRABAR MENSAJE (o serie `R`), hablar, una tecla | 3-2-1, ● REC con barra, MENSAJE GUARDADO #N; el WAV suena en MENSAJES | REQUIRES HARDWARE TEST | Parcial 2026-10-02: guardado probado con mensajes sintéticos (serie `Y`): `msg_0001…0005.wav`, números seguidos, ~1,2–1,5 s por 2 s; la pantalla 3-2-1 se abre y una tecla la cancela. **Pendiente** grabar de verdad con el micro (el usuario no estaba: no se grabó la sala sin él); 2026-10-02 con el usuario: msg_0006 grabado y guardado (15 s, 1,3 s) pero **no se oía** al reproducirlo (los ding-dong sí) → subida a pico −3 dBFS: msg_0007 (pico −23,6 dBFS, +20,5 dB) se oía "muy bajo" → paso alto de 300 Hz + voz a −14 dBFS (máx. +36 dB) + limitador: msg_0008 (voz −56,5 dBFS, +36 dB, el tope) "se oye mejor" al 70 % de volumen; **OK 2026-10-02**, el usuario lo da por bueno. La voz llega muy floja al micro: más cerca (20–30 cm) se oiría mejor; el tope de +36 dB se queda |
| TV16b | Guardado y nombres | Host: cabecera WAV, parser (no RIFF, no PCM, 8 bits, cortado), `msg_NNNN.wav`, flujo 3-2-1/REC/límite/cancelar/error, remuestreo | — | HOST | OK 2026-10-02 (636 checks) |
| TV17 | Canal de mensajes | `T messages` (o un canal `internal`/`messages` en channels.json) | Los mensajes en orden, en bucle; número, tiempo, barra; cambiar de canal lo para | REQUIRES HARDWARE TEST | OK 2026-10-02: 5 mensajes cada 3,47 s, oídos por el micro (−29/−37 dB, silencio −72 entre ellos); cambiar de canal cerró la tarea limpia |
| TV18 | 2 h con la voz activa | — | — | REQUIRES HARDWARE TEST | Pendiente |
| TV19 | Reproducción con micro | `[MEDIA]` con la captura ON / pausa (`V`) / firmware normal | Sin pérdida visible | REQUIRES HARDWARE TEST | OK 2026-10-02: 19,94 fps, 0 perdidos y 0 errores de audio en los tres; dibujo 39,0 / 39,4 / 39,8 ms; deriva máx. 25 ms en los tres; −16 KB de heap interno |
| TV20 | Arrancar silenciada | Reiniciar con la tele en MUTE | Vuelve con sonido | REQUIRES HARDWARE TEST | FALLO encontrado 2026-10-02 (el ES8311 guarda el MUTE al reiniciar el ESP32; la app creía que sonaba) → corregido: el códec se desmutea al arrancar. Comprobado: el micro volvió a oír la tele (−43 dB frente a −76) |

## Prueba de estabilidad (a partir de la fase 7) — REQUIRES HARDWARE TEST

Objetivo: demostrar que el zapping no pierde memoria ni deja tareas colgadas. `PAUTV_DEBUG_STATS 1`.

1. Arranca e inicia la reproducción de un canal local con audio.
2. Espera 30 s y anota `heap libre`, `psram libre` y el mínimo de heap (de las stats serie).
3. Haz **100 cambios de canal**, alternando siguiente y anterior y pasando por canales con vídeo, TEST CARD y NO SIGNAL.
4. Vuelve al canal inicial, espera 30 s y anota de nuevo heap y PSRAM.
5. Criterios:
   - no hay crecimiento sostenido: la diferencia de heap y PSRAM entre el paso 2 y el 4 es menor de 4 KB y no
     aumenta si se repite la prueba;
   - ningún `stop()` supera su timeout (no aparece ERROR "MEDIA STUCK" ni `[MEDIA] stop timeout` en el log);
   - las marcas de vida de cada tarea (vídeo, pantalla y audio) avanzan en todas las stats;
   - no hay reinicios: no vuelve a aparecer la línea `[BOOT] RETROTV …` en el log durante la prueba.
6. **stop/start repetido:** 50 veces seguidas sobre el mismo canal. Los mismos criterios, sin underruns de audio
   audibles tras el último start.
7. Anota también `av_drift_ms` (media y máximo) y `dropped_frames` antes y después.

**Automatizada:** con `PAUTV_DEBUG_STATS 1`, enviar `z` por el monitor serie hace 100 cambios de canal (uno cada
1.5 s, dos hacia delante por cada uno hacia atrás), con líneas `[SOAK] start` y `[SOAK] done`. `S<segundos>` es la
versión larga (soak test): cambia de canal cada N segundos (`S0` = nunca) y escribe una línea `[SOAK]` por minuto,
con memoria, fotogramas descartados, desfase A/V, errores de audio y marcas de vida (`STALLED` si una tarea deja de
avanzar mientras hay vídeo). `s` la para y `m` hace una foto suelta de memoria.

### Resultado 2026-09-29 (FNK0104A, canales de fábrica + 1 capítulo real) — OK

| | Antes | Después de 100 cambios |
|---|---|---|
| Heap libre | 173 KB | 173 KB |
| Mínimo histórico de heap | 164 KB | 164 KB |
| Bloque libre más grande | 159 KB | 159 KB |
| PSRAM libre | 8022 KB | 8022 KB |

- 100 cambios de canal durante la prueba: 52 arranques y paradas de vídeo, 28 cartas de ajuste y 28 NO SIGNAL con
  salto automático.
- Ningún `stop timeout`, `MEDIA STUCK`, watchdog, pánico ni reinicio (un único arranque en todo el log).
- Pendiente: stop/start repetido sobre el mismo canal durante horas, y un capítulo completo de 23 min (T6.12).
