# RETROTV Voice

Una capa **opcional, local y offline** para escuchar con el micrófono de la placa. RETROTV sigue siendo una tele:
la voz solo añade otra forma de dar las mismas órdenes que los botones. No es un asistente, no habla, no usa
Internet y **no guarda audio**.

## Estado

| Función | Estado | Notas |
|---|---|---|
| Captura del micrófono | **implementada, probada en la placa** | ES8311 MIC1 por I2S RX, 44,1 kHz |
| Medidor VU (pantalla MICROFONO) | **implementado, probado** | el usuario vio la barra moverse con voz y palmadas |
| MIC TEST por serie (`v`) | implementado, probado | niveles cada 250 ms durante 10 s |
| Detector de palmadas | **implementado, probado** | 43 secuencias reales en el log; ver calibración |
| Doble palmada → apagar a STANDBY VOZ | **implementado, probado** | antes silenciaba; el usuario prefiere encender y apagar con dos |
| Triple palmada → canal siguiente | **implementado, probado** | |
| Ajustes AJUSTES → VOZ | implementado | MICROFONO, PALMADAS, SENSIBLE, en NVS |
| Descartar el sonido de la propia tele | **implementado, probado** | envolvente del programa por encima de 400 Hz |
| Falsos positivos con la tele sonando | **mejorado, sin cerrar** | 30 min: 1 doble falsa (corregida y comprobada con esa escena); falta una prueba larga |
| STANDBY VOZ: cada palmada = destello del piloto | **implementado, probado** (v0.2) | |
| STANDBY VOZ: dos palmadas con silencio alrededor = encender | **implementado, probado** | la primera versión se encendía con ruidos de casa; falta una prueba larga |
| Selector APAGADO: STANDBY VOZ / AHORRO MAX | implementado (v0.2) | por defecto AHORRO MAX, como siempre |
| Consumo de STANDBY VOZ | **estimado ~40 mA** (30–50) | una noche: 72 → 64 % en ~6 h; ~3 días desde llena. Falta un medidor |
| Falsos encendidos en standby | sin medir | |
| Grabadora (GRABADORA, 3-2-1, ● REC, WAV 16 kHz) | **implementada, probada** con la voz del usuario (v0.3) | se guarda subida al nivel de voz; la voz llega floja al micro, mejor a 20–30 cm |
| Canal MENSAJES | **implementado, probado** (v0.3) | el micro oyó los mensajes por el altavoz, en orden y en bucle |
| Wake word, comandos offline (ESP-SR) | **investigado, bloqueado** (fase M) | ver "Wake word y comandos" |
| "HEY RETRO" | **no existe** | un modelo propio exige el servicio de pago de Espressif |

## Cómo se activa

El firmware normal **no lleva nada de esto** (se compila igual que antes). La versión con voz:

```bash
pio run -e voice -t upload     # = -DPAUTV_VOICE_ENABLED=1
```

Cada parte se puede quitar por separado con su flag (`include/config.h`):

| Flag | Por defecto | Qué hace |
|---|---|---|
| `PAUTV_VOICE_ENABLED` | 0 (1 en `-e voice`) | la capa entera |
| `PAUTV_MIC_ENABLED` | = VOICE | I2S RX, micrófono del códec, VU, MIC TEST |
| `PAUTV_CLAP_ENABLED` | = MIC | detector y órdenes por palmadas |
| `PAUTV_CLAP_WAKE_ENABLED` | 0 | encender con palmadas (v0.2) |
| `PAUTV_WAKEWORD_ENABLED` | 0 | wake word (v0.4) |
| `PAUTV_VOICE_COMMANDS_ENABLED` | 0 | comandos de voz (v0.4) |
| `PAUTV_RECORDER_ENABLED` | 0 | grabadora (v0.3) |

Dentro de la versión con voz, **AJUSTES → VOZ** enciende y apaga el micrófono y las palmadas sin recompilar.

## Arquitectura

```
ES8311 MIC1 (MEMS analógico) ─ PGA +30 dB ─ ADC (+24 dB, +4,5 dB)
        │ I2S RX (GPIO6), mismo puerto, relojes y formato que la reproducción
        ▼
tarea "mic" (núcleo 1, prioridad 4) ── duerme en i2s_read, bloques de 512 muestras (11,6 ms)
        │  canal izquierdo (el ES8311 pone la misma muestra en los dos)
        ├── MicMeter ──── RMS y pico en dBFS ─────────────► pantalla MICROFONO, MIC TEST
        └── ClapDetector ─ secuencia terminada (1, 2, 3) ─► App::pollClaps()
                                                              │ solo con un programa en marcha
                                                              ▼
                                       doble: apagar a STANDBY VOZ / triple: InputEvent::ChNext (= botones)
```

- **Mismos caminos que las teclas.** Doble = `powerDown()` + `voiceStandby()`, el apagado de ⏻ pero siempre a
  STANDBY VOZ (lo que apagan dos palmadas lo encienden dos palmadas, diga lo que diga APAGADO); triple =
  `InputEvent::ChNext`, tratado por `App::onInput()` igual que una tecla o el mando web. Sin
  `PAUTV_CLAP_WAKE_ENABLED`, la doble silencia (`InputEvent::Mute`), como antes.
- **Archivos:** `src/voice/AudioCapture.*` (solo obtiene PCM y mide), `src/voice/MicMeter.h` y
  `src/voice/ClapDetector.h` (C++ puro, con tests), `src/app/AppVoice.cpp` (pantalla, ajustes, palmadas → eventos).
  Fuera de la versión con voz, `AppVoice.cpp` son funciones vacías.

### I2S full duplex

La reproducción ya usaba `I2S_NUM_0` en TX (driver legacy, 44,1 kHz, 16 bits estéreo, MCLK ×256). El micrófono
añade **RX en el mismo puerto** (`I2S_MODE_TX | I2S_MODE_RX`): comparte BCLK, WS y MCLK, así que el formato, el
anillo DMA y la latencia de la reproducción no cambian. Lectura y escritura tienen sus propios semáforos en el
driver; el bloqueo de `AudioManager` sigue siendo solo para los dos escritores (sintetizador y vídeo).

Medido en la placa (canal local, 60–90 s por caso, 2026-10-02):

| | fps | dibujo | deriva A/V media / máx | perdidos | errores audio | heap interno libre |
|---|---|---|---|---|---|---|
| Firmware normal | 19,94 | 39,0 ms | 1,4 / 25 ms | 0 | 0 | 88 KB (bloque 75) |
| Voz, captura en pausa (`V`) | 19,94 | 39,4 ms | 2,1 / 25 ms | 0 | 0 | 71 KB |
| Voz, capturando | 19,94 | 39,8 ms | 3,7 / 25 ms | 0 | 0 | 72 KB (bloque 61) |

Coste: **16 KB de heap interno** (DMA de RX y la pila de la tarea) y +4 KB de RAM estática; unos 2 ms más de
deriva media sin cambiar la máxima. La tarea en sí es ruido de medida (39,4 frente a 39,8 ms de dibujo).

### Por qué una tarea

Hace falta leer el DMA de RX de forma continua (~46 ms de colchón) o se pierden muestras, y el loop de la app
puede tardar más que eso. La tarea duerme en `i2s_read`, despierta ~86 veces por segundo y gasta <1 % de un
núcleo. Va por encima de la pantalla (prioridad 3), cuyos fotogramas de 38 ms vaciarían el colchón, y por debajo
del audio (5).

### El códec

| Registro | Valor | Por qué |
|---|---|---|
| REG14 | `0x1A` | entrada MIC1P/N y PGA analógico +30 dB (como el ejemplo de eco de Freenove) |
| REG16 | `4` (+24 dB) | escala del ADC; es el valor de fábrica. A 0 dB la sala se leía a −80/−89 dBFS |
| REG17 | `0xC8` (+4,5 dB) | volumen digital del ADC |

Con eso, una sala tranquila lee **−58 a −67 dBFS** y una palmada a 0,5–1 m **−11 a −38 dBFS**, sin recortes.

## Palmadas

`ClapDetector` no usa aprendizaje automático:

1. quita la continua y mide la energía en ventanas de 5 ms;
2. sigue el **ruido de fondo** con una media lenta en dB (~1,5 s) en todas las ventanas menos las de una posible
   palmada, así que un ruido constante (la propia tele, una conversación larga) sube el umbral solo;
3. **umbral** = fondo + margen (SENSIBLE 60 → 18 dB; 100 → 10 dB; 0 → 30 dB), nunca por debajo de −46 dBFS,
   y +6 dB mientras la tele suena;
4. una palmada es una **subida brusca** (≥ 9 dB en una o dos ventanas) que **cae** ≥ 10 dB en menos de 100 ms;
   un sonido fuerte que dura más se descarta (`long`);
5. y es **brillante**: ≥ 18 % de su energía por encima de 2 kHz. Un golpe en la madera o una puerta es sordo y no
   cuenta (`dull`; cada uno deja `[CLAP] too dull (bright N%)` en el log). Empezó en 30 %, pero las palmadas del
   usuario dan 21–35 %: con la tele encendida la segunda de cada doble se perdía (21, 23, 25 y 28 %) y dos
   palmadas no la apagaban. Con el 18 %, 14 golpes de nudillos en la mesa midieron 4–15 % y se descartaron todos;
6. tras una palmada se ignora todo **180 ms** (su eco: unas manos nunca van más rápido);
7. las palmadas a menos de **700 ms** forman una secuencia, que se comprueba y se informa **al cerrarse la
   ventana**: 2 o 3 palmadas con **ritmo de manos** (≤ 600 ms entre ellas) y de **fuerza parecida** (±9 dB).
   **4 o más** (una alarma que pita, algo que traquetea) no son nada. Con dos palmadas la tele espera por si llega la
   tercera, así que una triple nunca apaga antes de cambiar de canal.

| Secuencia | Con un programa en marcha | En menús y diagnóstico |
|---|---|---|
| 1 palmada | nada (reservada al standby de v0.2) | nada |
| 2 palmadas | apagado CRT y STANDBY VOZ; dos palmadas la vuelven a encender | se registra, no actúa |
| 3 o más | `InputEvent::ChNext`: la estática y el OSD del canal | se registra, no actúa |

**La tele se oye a sí misma.** Tres defensas, además del fondo dinámico y los +6 dB con sonido:

1. **Sus sonidos propios:** no acepta palmadas durante la estática y los pitidos del sintetizador ni 350 ms
   después, y tampoco tras **cada silencio o cambio de volumen**, porque el altavoz da un chasquido al silenciar
   el DAC. En la placa, ese chasquido (pico −16 dBFS) aparecía como palmada suelta justo al silenciar, una vez con
   su eco a 161 ms como una doble que devolvía el sonido, y otra como una "triple" a 144 ms que cambiaba de canal.
2. **180 ms mínimos** entre dos palmadas: unas manos nunca van más rápido (las dobles reales: 210–691 ms).
3. **Lo que suena por su altavoz** (`voice/PlaybackEnvelope.h`): `AudioManager::writePcm` guarda el nivel de cada
   bloque de 256 muestras del programa y cuándo se oirá (+46 ms de colchón DMA). Una palmada cuya subida coincide
   (−80 a +30 ms) con una subida de ≥ 8 dB del programa es la tele oyéndose. El nivel se mide **por encima de
   400 Hz**: el altavoz pequeño casi no da graves, así que una caja sobre un bajo apenas cambia el nivel de banda
   completa pero el micro la oye como un golpe seco. Cada palmada aceptada deja en el log cuánto subió el programa
   a su alrededor (`tv rise`), para afinar los 8 dB.

### Calibración (datos reales, 2026-10-02)

43 secuencias de palmadas reales del usuario a 0,5–1 m, con la tele en silencio y sonando:

| | mínimo | máximo |
|---|---|---|
| Separación entre palmadas | 210 ms (144 y 161 eran el chasquido) | 691 ms |
| Pico de la palmada | −45,7 dBFS | −11,3 dBFS |
| Ruido de fondo al detectarla | −71,7 dBFS (sala en silencio) | −40,1 dBFS (tele sonando) |

Con la tele sonando, el propio programa produce **palmadas sueltas** de vez en cuando (golpes del dibujo
animado, picos −14 a −24 dBFS); no hacen nada porque una sola palmada no es una orden.

### Falsos positivos (Samurai Champloo, peleas y una intro de hip-hop, sonido al 75 %)

| Versión | Duración | Dobles/triples falsas | Golpes propios descartados | Causa |
|---|---|---|---|---|
| Solo fondo dinámico + 6 dB | 3 min | 1 doble | — | dos puñetazos a 624 ms |
| + envolvente del programa | 30 min | 1 doble | 168 | la canción de la intro (caja sobre bajo) |
| + envolvente por encima de 400 Hz | esa intro desde 0:00 (`T !ruta`), 2 min | 0 | 9 | — |
| + brillo 18 % (la doble ya apaga) | esa intro desde 0:00, 3 min 50 s | 0 (1 suelta) | 26 sordos, 3–18 % | — |
| la misma versión, prueba larga | el capítulo entero desde 0:00 y su vuelta, 30 min | 0 (3 sueltas) | 146 sordos, 3–18 % | — |

La prueba larga (30 min, 2026-10-03) no dio ninguna doble ni apagado. Las 3 sueltas eran golpes del programa de
28–38 % de agudos cuya subida en el programa (6,6–7,5 dB) quedó justo bajo los 8 dB del descarte; los sordos llegaron
al 18 %, en el límite. Si aparece una doble falsa, el log dirá con qué brillo y con qué subida del programa.

## STANDBY VOZ (v0.2)

**AJUSTES → VOZ → APAGADO** elige qué hace ⏻ (o mantener CH− 2 s). Dos palmadas con un programa en marcha apagan
siempre a STANDBY VOZ:

| | AHORRO MAX (por defecto) | STANDBY VOZ |
|---|---|---|
| Apagado CRT, pantalla, retroiluminación, piloto, altavoz y Wi-Fi | apagados | apagados |
| Chip | deep sleep | despierto a 80 MHz, con la captura del micrófono |
| Enciende con | una tecla | **dos palmadas con silencio alrededor** o una tecla |
| Cada palmada oída | — | destello de 120 ms del piloto ("te he oído"), si LED ESCUCHA está en ON |
| Batería agotada | deep sleep | deep sleep (deja de escuchar) |
| Consumo | el del deep sleep (T22.4) | ~40 mA estimados (una noche, sin medidor): ~3 días desde llena |

- `enterStandby()` se partió en `powerDown()` (todo lo que se ve y se oye) y `deepSleep()`; STANDBY VOZ usa el
  primero y luego `voiceStandby()`.
- **Encender es reiniciar** (`ESP.restart()`): exactamente el mismo arranque que al despertar del deep sleep, con
  la intro, el último canal y el volumen guardado. No hay un arranque especial.
- En standby enciende una secuencia de **exactamente dos palmadas** que pasó las comprobaciones, con cada palmada a
  **−36 dBFS** o más (cerca de la tele) y **silencio alrededor**: ningún golpe en los 2,5 s anteriores ni en los
  ~1,2 s posteriores (la ventana de 0,7 s más 0,5 s de espera). Los ruidos de casa suelen venir en grupo; una persona
  da las palmadas tras un momento de silencio. El piloto destella con cada palmada que oye
  (`voice/VoiceStandby.h`, con tests). Puede quedar algún falso encendido de dos golpes aislados que suenen a
  palmada; si molesta, AHORRO MAX lo quita.
- Historia: la primera versión encendía con el segundo golpe al momento, y el usuario vio que cualquier golpe,
  alarma o ruido la encendía. La segunda exigía una doble comprobada (ritmo, fuerza, brillo); paró la mayoría,
  pero un ruido de casa dio una pareja que pasaba todas las comprobaciones (−13/−17 dB, 52/66 % de agudos, más
  brillante que las palmadas del usuario, 32–35 %), aunque llegó 1,6 s después de otro golpe. Ninguna regla de
  sonido separa con fiabilidad dos golpes al azar de dos palmadas. Tres palmadas a ritmo serían más seguras, pero
  el usuario prefirió dos; la tercera versión exige silencio antes y después.
- No hay sonido propio que filtrar (el amplificador está apagado), así que el umbral es el de la sala.
- Sin Wi-Fi el mando web no responde, igual que con el deep sleep.
- Por serie (sin teclas): `q` apaga y `W` enciende desde STANDBY VOZ.

## Grabadora y canal MENSAJES (v0.3)

**Grabar** (solo a propósito y con **● REC** en pantalla):

1. **AJUSTES → VOZ → GRABAR MENSAJE** (VOL+), o serie `R`. Cuando la carcasa tenga teclas, también CH− + VOL−.
   A propósito **no hay botón en el mando web**: no pide PIN, y cualquiera en la misma Wi-Fi podría grabar la sala.
2. **GRABADORA** · 3 · 2 · 1 (una tecla aquí cancela sin grabar nada).
3. **● REC 00:05 / 00:15** con una barra roja hasta el límite de 15 s. Cualquier tecla para.
4. **GUARDANDO** (~1 s) y **MENSAJE GUARDADO #004**, o **ERROR AL GUARDAR** / **NO SD**. Vuelve al canal.

- Durante REC el programa se para (para no grabar la propia tele). La tarea del micro pasa cada bloque a
  16 kHz (filtro de 6 kHz y interpolación) y lo deja en un búfer de PSRAM de 480 KB, reservado la primera vez.
- **Se guarda más fuerte.** Una voz a un metro llega al micro 25–40 dB por debajo de plena escala, y buena parte es
  grave que el altavoz pequeño no da. La primera grabación real (msg_0006) no se oía; subirla hasta un pico de
  −3 dBFS (msg_0007: pico −23,6 dBFS, +20,5 dB) se oía "muy bajo": un pico no es lo fuerte que suena una voz.
  Ahora `normalizeRecording()` quita los graves por debajo de 300 Hz (y la continua), mide la voz por sus bloques
  de 20 ms fuertes (el percentil 90: las pausas y un clic suelto no cuentan), la sube a −14 dBFS con +36 dB como
  máximo (una sala en silencio no se convierte en soplido) y redondea con un limitador suave los picos que
  pasarían de plena escala. El log dice el nivel de la voz y la ganancia de cada mensaje. msg_0008: voz −56,5 dBFS
  (casi el ruido de la sala), +36 dB, y el usuario la oyó mejor que la anterior.
- El archivo es `/retrotv/voice/messages/msg_NNNN.wav` (PCM 16 bits mono, 16 kHz, ~32 KB por segundo), con el
  número siguiente al mayor de la carpeta. Se escribe como `.tmp` y se renombra: un corte de luz deja, como
  mucho, un `.tmp` que no cuenta como mensaje y que el siguiente guardado sobrescribe.
- Medido: guardar 2 s tarda ~1,2–1,5 s (carpeta 0,2 s, listado 0,35 s, escritura). No se comprueba el espacio
  libre antes: en una tarjeta FAT32 de 64 GB eso recorre toda la FAT (~7 s); una tarjeta llena falla al escribir.
- Serie `Y` guarda un **mensaje sintético** (un ding-dong de 2 s) por el mismo camino, sin micrófono: sirve para
  probar la tarjeta y el canal sin grabar a nadie.
- Serie `E` **borra todos los mensajes** (solo los `msg_NNNN.wav`; nada más de la carpeta). No lo hace con
  MENSAJES en pantalla, que podría estar leyendo uno: primero se cambia de canal.

**El canal MENSAJES** es un canal interno más; se añade a `channels.json` con el número que se quiera:

```json
{ "id": "mensajes", "number": 98, "name": "MENSAJES", "type": "internal", "source": "messages" }
```

- Reproduce los `msg_NNNN.wav` del más antiguo al más nuevo, con 1,5 s entre ellos, y vuelve a empezar. Pantalla:
  el número grande, el tiempo con una barra verde y "2 DE 5"; **SIN MENSAJES** si no hay.
- `MessagePlayer` (`src/voice/MessagePlayer.*`) tiene una tarea **solo mientras el canal está en pantalla**: ocupa el
  sitio de la tarea de audio del vídeo (parada entonces), en el mismo núcleo y prioridad, y como ella duerme en
  `writePcm`. Lee el archivo a trozos de 32 ms con `SdFile`, valida la cabecera con `parseWav` (un archivo que no
  es PCM 16 bits se salta) y pasa de 16 a 44,1 kHz. CH−/CH+ cambian de canal como siempre; las palmadas también
  funcionan aquí.
- En un firmware sin voz, un canal `messages` enseña **SIN VOZ** en vez de fallar.
- Medido en la placa: los mensajes empiezan cada 3,47 s (2 s + 1,5 s), el micro los oyó a −29/−37 dB con silencio
  (−72 dB) entre ellos; al cambiar de canal la tarea se cierra limpia (heap 70 KB, bloque 59 KB).

## Wake word y comandos: investigación (fase M, 2026-10-02)

| Pregunta | Respuesta | Fuente |
|---|---|---|
| ¿Trae ESP-SR el core que usamos? | **No.** `framework-arduinoespressif32` 3.20017 (Arduino 2.0.17, ESP-IDF 4.4) solo trae `esp-dl` para el S3: ni WakeNet, ni MultiNet, ni el front-end de audio (AFE) | comprobado en `tools/sdk/esp32s3/` |
| ¿Hay un ESP-SR para ESP-IDF 4.4? | Sí: desde la 1.3.0 es compatible con IDF 4.4 y 5.0. Pero es un componente de ESP-IDF (bibliotecas precompiladas y una **partición de modelos** aparte) | [changelog 1.9.x](https://components.espressif.com/components/espressif/esp-sr/versions/1.9.5/changelog) |
| ¿Comandos en español? | **No.** MultiNet solo tiene modelos en chino e inglés; el español está "planificado" | [espressif/esp-sr](https://github.com/espressif/esp-sr) |
| ¿Un "HEY RETRO" propio? | Servicio de Espressif **de pago**: ≥ 20 000 muestras (500+ personas, 100+ niños) o un corpus que recoge Espressif, y 2–3 semanas de entrenamiento | [Custom wake word](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html) |
| ¿Cabe en la CPU? | Mientras se ve un canal, no hay sitio: el decodificador ocupa ~90 % del núcleo 0 y la pantalla ~76 % del 1. En STANDBY VOZ, sí | medidas de este proyecto |

**Conclusión:** integrar ESP-SR exige cambiar el sistema de compilación (componente de ESP-IDF con su partición de
modelos, o pasar a Arduino 3.x), y eso **no se hace sin permiso**. Aun hecho, solo daría palabras de activación
genéricas en inglés y comandos en inglés o chino; "HEY RETRO" y los comandos en español no son posibles hoy sin
pagar un modelo. Lo que sí sería razonable, si se decide: una prueba técnica de WakeNet con una palabra de serie y
**solo en STANDBY VOZ**, donde sobra CPU. Por eso la v0.4 queda en pausa y la voz se limita a palmadas, standby y
mensajes.

## Pantallas y órdenes

- **MICROFONO:** AJUSTES → DIAGNOSTICO → **CH−**. Barra VU segmentada (−60 a 0 dB, verde, amarillo, rojo, con
  el pico retenido 1 s en blanco), RMS, PEAK, recortes, errores de lectura y la última secuencia oída. MENU
  (mantener CH+) vuelve.
- **AJUSTES → VOZ:** MICROFONO ON/OFF (pausa la captura), PALMADAS ON/OFF, SENSIBLE 0–100 de 10 en 10.
- **Serie** (115200, compilación con `PAUTV_DEBUG_STATS`): `v` = MIC TEST, 10 s de
  `[MIC] rms=… peak=… clip=… overrun=… | clap floor=… thr=… claps=… long=… own=…`; `V` = pausa o reanuda la
  captura (para medir su coste). Cada secuencia deja `[CLAP] double (gap 296 ms, peak -11.3 dB, floor -58.3 dB,
  tv rise 2.1 dB) in PLAYING`. Sin botones, los menús se recorren por serie con `M` (menú), `n`/`p` (CH) y
  `+`/`-` (VOL). `T !/retrotv/media/…mjpeg` pone un capítulo **desde 0:00** (para repetir una escena concreta
  contra el detector; `T ruta` sin `!` lo pone "en emisión").

## Privacidad

- RETROTV **no almacena ni envía audio** por su cuenta. Cada bloque de 11,6 ms se mide y se sobrescribe; solo salen
  niveles en dB y el número de palmadas.
- **Lo único que se guarda** son los mensajes de la grabadora: a petición, con **● REC** en pantalla, como mucho
  15 s, y solo en la SD de la tele. No hay forma de empezar una grabación desde la red.
- Todo es local: ni nube, ni servicios de reconocimiento.

## Tests

- **En el ordenador** (`tools/run_host_tests.sh`), `test/voice_tests.cpp`: niveles en dB, silencio, continua,
  seno, recortes; y para las palmadas: silencio y ruido constante (sin palmadas), palmada corta, sonido largo
  (no), eco a 50 ms (ignorado), doble y triple, la doble espera a que cierre la ventana, dos golpes a 160 ms (no es
  doble), secuencia lenta = dos sueltas, la segunda palmada no se pierde por la cola de la primera, fondo
  dinámico en una sala ruidosa, sensibilidad, margen extra con sonido, sonidos propios suprimidos, vuelta de
  `millis()`.
- **En la placa:** TV1–TV18 en [TEST_PLAN.md](TEST_PLAN.md), sección *RETROTV Voice*. Nada se marca OK solo
  porque compile.

## Un fallo encontrado por el camino

El ES8311 **guarda sus registros cuando el ESP32 se reinicia** (sigue alimentado, y el reset de `es8311_init` solo
reinicia sus máquinas de estado). Una tele reiniciada estando en silencio volvía muda, mientras la app y el mando
web decían que sonaba. No era de la voz (pasaba igual al reiniciar desde AJUSTES); el micro lo delató: oía la sala
a −76 dB con un capítulo en marcha. Ahora `AudioManager::begin` quita el silencio del códec al arrancar.

## Limitaciones conocidas

- Sin cancelación de eco (AEC): con el programa muy alto (fondo −27 dB) hace falta una palmada muy fuerte, porque
  el umbral queda casi en 0 dBFS. Bajar el margen ahora que existe el descarte por envolvente es la siguiente
  calibración.
- Una palmada real que coincide con una subida fuerte del programa se descarta (hay que repetirla).
- Falta una prueba larga de falsos positivos con la versión final.
- El micrófono cuesta 16 KB de heap interno; con todo lo demás quedan ~70 KB libres (bloque mayor 61 KB).
- Wake word y comandos dependen de ESP-SR, cuya compatibilidad con espressif32 6.9.0 / Arduino 2.0.17 no se ha
  comprobado. Si exigiera migrar de framework, no se hará sin permiso.
