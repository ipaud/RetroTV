# RETROTV Voice

Una capa **opcional, local y offline** para escuchar con el micrófono de la placa. RETROTV sigue siendo una tele:
la voz solo añade otra forma de dar las mismas órdenes que los botones. No es un asistente, no habla y no usa
Internet.

**Qué pasa con el sonido de la sala:**
- **Las palmadas no graban nada.** El detector mira cada trozo de 11,6 ms, calcula su nivel y lo descarta. No se
  guarda ni se envía audio: solo quedan números (niveles en dB, cuántas palmadas) en el log.
- **La grabadora sí guarda audio,** pero solo cuando tú la pones en marcha: sale **● REC** en pantalla, graba
  como mucho 15 s y lo guarda como archivo WAV en la microSD de la tele (`/retrotv/voice/messages/`). No sale de
  la tele, y no se puede empezar una grabación desde la red ni desde el mando web.
- Sin el firmware `voice`, el micrófono ni siquiera se activa.

## Estado

Las etiquetas v0.1–v0.4 de esta página son **fases de la capa de voz**, no versiones de RETROTV: el firmware sigue
siendo la 0.2.0-alpha2 (`PAUTV_VERSION` en `include/config.h`). "Probado" quiere decir comprobado en la placa del
usuario, con su sala y su voz; el detalle de cada prueba está en [TEST_PLAN.md](TEST_PLAN.md).

| Función | Estado | Notas |
|---|---|---|
| Captura del micrófono | **implementada, probada en la placa** | ES8311 MIC1 por I2S RX, 44,1 kHz |
| Medidor VU (pantalla MICROFONO) | **implementado, probado** | el usuario vio la barra moverse con voz y palmadas |
| MIC TEST por serie (`v`) | implementado, probado | niveles cada 250 ms durante 10 s |
| Detector de palmadas | **implementado, probado** | 43 secuencias reales en el log; ver calibración |
| Doble palmada | **no hace nada** (con el firmware `voice`) | apagaba (probado 2026-10-02); se colaban demasiados pares de ruidos |
| Triple palmada → apagar a STANDBY VOZ | implementado, **pendiente de probar** | antes cambiaba de canal (probado); el usuario pasó de dos a tres palmadas para encender y apagar (2026-10-03) |
| Ajustes AJUSTES → VOZ | **implementado, probado** | MICROFONO, PALMADAS, SENSIBLE, APAGADO, LED ESCUCHA y GRABAR MENSAJE, en NVS; recorridos por serie (sin teclas montadas) |
| Descartar el sonido de la propia tele | **implementado, probado** | envolvente del programa por encima de 400 Hz |
| Falsos positivos con la tele sonando | **probado con una escena, sin validar en general** | 30 min con un capítulo concreto al 75 %: 0 dobles (2026-10-03). No se ha probado con otros programas, volúmenes ni salas |
| STANDBY VOZ: cada palmada = destello del piloto | **implementado, probado** (v0.2) | |
| STANDBY VOZ: tres palmadas con silencio alrededor = encender | implementado; con dos, probado; con tres, **pendiente** | la primera versión se encendía con ruidos de casa; no es infalible (ver más abajo) |
| Selector APAGADO: STANDBY VOZ / AHORRO MAX | **implementado, probado** (v0.2) | por defecto AHORRO MAX; las dos ramas probadas en la placa (TV9, TV13) |
| Consumo de STANDBY VOZ | **estimado ~40 mA** (30–50) | una noche: 72 → 64 % en ~6 h; ~3 días desde llena. Falta un medidor |
| Falsos encendidos en standby | **sin medir con log** | una sesión corta: 5 golpes sueltos sin encender; una noche (~6 h) en STANDBY VOZ sin encenderse, deducido de la batería (sin log) |
| Grabadora (GRABADORA, 3-2-1, ● REC, WAV 16 kHz) | **implementada, probada** con la voz del usuario (v0.3) | se guarda subida al nivel de voz; la voz llega floja al micro, mejor a 20–30 cm |
| Canal MENSAJES | **implementado, probado** (v0.3) | el micro oyó los mensajes por el altavoz, en orden y en bucle |
| Wake word, comandos offline (ESP-SR) | **investigado, bloqueado** en este firmware (fase M) | ver "Wake word y comandos" |
| «Hola ESP» en STANDBY VOZ | **prueba experimental** (`voice_ww`, `voice_nokeys_ww`), probada en la placa | una app de standby aparte con ESP-IDF 5 + ESP-SR 2.5.5: [WAKEWORD.md](WAKEWORD.md) |
| «Hey Retro» en STANDBY VOZ | **prueba experimental**, probada en la placa | modelo propio con microWakeWord, entrenado en el Mac y fuera del repo: [WAKEWORD.md](WAKEWORD.md) |
| Comandos de voz | **no implementados** | MultiNet no tiene español |

## Cómo se activa

Hay dos compilaciones (`platformio.ini`):

| Entorno | Cómo | Qué trae |
|---|---|---|
| `pautv` (el normal, por defecto) | `pio run -t upload` | Nada de voz: el micrófono, el I2S de entrada, las palmadas y la grabadora no se compilan. Un canal MENSAJES enseña SIN VOZ |
| `voice` | `pio run -e voice -t upload` | Lo mismo más `-DPAUTV_VOICE_ENABLED=1`: micrófono, VU, palmadas, STANDBY VOZ, grabadora y canal MENSAJES |

Las flags de `include/config.h` **heredan** de la de arriba: encender `PAUTV_VOICE_ENABLED` enciende las demás,
salvo la palabra de activación y los comandos, que no existen.

| Flag | Valor si no se define | En `pautv` | En `voice` | Qué hace |
|---|---|---|---|---|
| `PAUTV_VOICE_ENABLED` | 0 | 0 | 1 | la capa entera |
| `PAUTV_MIC_ENABLED` | = `PAUTV_VOICE_ENABLED` | 0 | 1 | I2S RX, micrófono del códec, VU, MIC TEST |
| `PAUTV_CLAP_ENABLED` | = `PAUTV_MIC_ENABLED` | 0 | 1 | detector y órdenes por palmadas |
| `PAUTV_CLAP_WAKE_ENABLED` | = `PAUTV_CLAP_ENABLED` | 0 | 1 | apagar a STANDBY VOZ y encender con palmadas (sin ella, la doble silencia) |
| `PAUTV_RECORDER_ENABLED` | = `PAUTV_MIC_ENABLED` | 0 | 1 | grabadora y borrado de mensajes |
| `PAUTV_WAKEWORD_ENABLED` | 0 | 0 | 0 | experimental (`voice_ww`, `voice_nokeys_ww`): STANDBY VOZ pasa a la app «Hola ESP» de `app1` ([WAKEWORD.md](WAKEWORD.md)) |
| `PAUTV_VOICE_COMMANDS_ENABLED` | 0 | 0 | 0 | reservada: los comandos de voz no están implementados |

Para la carcasa sin botones hay un tercer entorno, `voice_nokeys` = `voice` + `-DPAUTV_HAS_KEYS=0` (ver STANDBY VOZ).

Cada parte se puede quitar por separado añadiendo su flag a 0 en `build_flags` del entorno `voice` (por ejemplo
`-DPAUTV_RECORDER_ENABLED=0` deja palmadas sin grabadora). Compilado y comprobado (2026-10-03): `voice` sin
grabadora, sin encender con palmadas y sin palmadas. Una combinación imposible (palmadas sin micrófono, encender con
palmadas sin palmadas, grabadora sin micrófono) para la compilación con un `#error`, y también
poner a 1 las dos reservadas, para que nadie crea que activó algo que no existe.

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
                                       triple: apagar a STANDBY VOZ / doble: nada
```

- **Mismos caminos que las teclas.** Triple (`CLAP_POWER_CLAPS`) = `powerDown()` + `voiceStandby()`, el apagado de
  ⏻ pero siempre a STANDBY VOZ (lo que apagan tres palmadas lo encienden tres palmadas, diga lo que diga APAGADO).
  La doble no hace nada. Sin `PAUTV_CLAP_WAKE_ENABLED` vale lo de antes: doble = silencio (`InputEvent::Mute`),
  triple = canal siguiente (`InputEvent::ChNext`).
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
3. **umbral** = fondo + margen (SENSIBLE 80, el valor por defecto → 14 dB; 60 → 18 dB; 100 → 10 dB; 0 → 30 dB; era 60,
   pero dentro de la carcasa y con el programa subiendo el fondo se perdían palmadas), nunca por debajo de −46 dBFS,
   y +6 dB mientras la tele suena;
4. una palmada es una **subida brusca** (≥ 9 dB en una o dos ventanas) que **cae** ≥ 10 dB en menos de 100 ms;
   un sonido fuerte que dura más se descarta (`long`);
5. y es **brillante**: ≥ 10 % de su energía por encima de 2 kHz. Un golpe en la madera o una puerta es sordo y no
   cuenta (`dull`; cada uno deja `[CLAP] too dull (bright N%)` en el log). Empezó en 30 %, pero las palmadas del
   usuario dan 21–35 %: con la tele encendida la segunda de cada doble se perdía (21, 23, 25 y 28 %) y dos
   palmadas no la apagaban. Con el 18 %, 14 golpes de nudillos en la mesa midieron 4–15 % y se descartaron todos; **dentro de la carcasa** el plástico apaga los agudos y las mismas palmadas dan 11–17 %, así que el límite
   bajó al 10 % (2026-10-03); con tres palmadas obligatorias, a ritmo y de fuerza parecida, el brillo pesa menos;
6. tras una palmada se ignora todo **180 ms** (su eco: unas manos nunca van más rápido);
7. las palmadas a menos de **700 ms** forman una secuencia, que se comprueba y se informa **al cerrarse la
   ventana**: 2 o 3 palmadas con **ritmo de manos** (≤ 600 ms entre ellas) y de **fuerza parecida** (±12 dB; era
   ±9, y una triple real del usuario tuvo 10,1 dB de diferencia).
   **4 o más** (una alarma que pita, algo que traquetea) no son nada. La tele espera a que se cierre la ventana, así
   que cuatro palmadas nunca cuentan como tres.

| Secuencia | Con un programa en marcha | En menús y diagnóstico |
|---|---|---|
| 1 palmada | nada (reservada al standby de v0.2) | nada |
| 2 palmadas | nada (se registra) | se registra, no actúa |
| 3 palmadas | apagado CRT y STANDBY VOZ; tres palmadas la vuelven a encender | se registra, no actúa |
| 4 o más | nada | nada |

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
   **Pero no a una palmada fuerte:** en un programa con diálogo o música hay subidas así a cada momento, y la
   mitad de las palmadas del usuario caían en una por casualidad (2026-10-03, en la carcasa). Un golpe a −30 dBFS o
   más **y** 28 dB o más por encima del fondo cuenta como palmada aunque el programa suba: las palmadas del usuario
   midieron −10 a −25 dBFS y 30,5–45 dB sobre el fondo; los golpes del propio programa, 24–32 dB sobre el fondo pero
   a −38/−44 dBFS, y los puñetazos a volumen 75, −14 a −24 dBFS pero como mucho 26 dB sobre el fondo. Cada descarte
   deja `[CLAP] taken for the TV's own sound: peak …, floor …, the programme rose …` en el log. Con la tele en
   silencio (o volumen 0) este filtro no actúa: el altavoz no puede imitar una palmada.

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
| la misma versión, 30 min | el capítulo entero desde 0:00 y su vuelta, 30 min | 0 (3 sueltas) | 146 sordos, 3–18 % | — |

La prueba de 30 min (2026-10-03) no dio ninguna doble ni apagado. Las 3 sueltas eran golpes del programa de
28–38 % de agudos cuya subida en el programa (6,6–7,5 dB) quedó justo bajo los 8 dB del descarte; los sordos llegaron
al 18 %, en el límite. Si aparece una doble falsa, el log dirá con qué brillo y con qué subida del programa.

**Qué demuestra y qué no.** Todas estas pruebas son **la misma escena** (un capítulo con peleas y una intro de
hip-hop, al 75 %, en la misma sala, con el mismo micrófono). Dicen que esa escena, que antes fallaba, ya no apaga la
tele. No son una validación prolongada: faltan otros programas (música, deportes, directos), otros volúmenes, otras
salas y muchas horas de uso normal. El margen es pequeño (sordos hasta 18 %, límite 18 %; subidas del programa de
7,5 dB, límite 8 dB), así que una secuencia falsa sigue siendo posible. Desde el 2026-10-03 apagar exige **tres** palmadas y las dobles
no hacen nada: una doble falsa ya no apaga; una triple falsa sí, pero es mucho más rara (en estas pruebas, ninguna).

## STANDBY VOZ (v0.2)

**AJUSTES → VOZ → APAGADO** elige qué hace ⏻ (o mantener CH− 2 s). Tres palmadas con un programa en marcha apagan
siempre a STANDBY VOZ:

| | AHORRO MAX (por defecto) | STANDBY VOZ |
|---|---|---|
| Apagado CRT, pantalla, retroiluminación, piloto, altavoz y Wi-Fi | apagados | apagados |
| Chip | deep sleep | despierto a 80 MHz, con la captura del micrófono |
| Enciende con | una tecla | **tres palmadas con silencio alrededor** o una tecla |
| Cada palmada oída | — | destello de 120 ms del piloto ("te he oído"), si LED ESCUCHA está en ON |
| Batería agotada | deep sleep | deep sleep (deja de escuchar) |
| Consumo | el del deep sleep (T22.4) | ~40 mA estimados (una noche, sin medidor): ~3 días desde llena |

- `enterStandby()` se partió en `powerDown()` (todo lo que se ve y se oye) y `deepSleep()`; STANDBY VOZ usa el
  primero y luego `voiceStandby()`.
- **Encender es reiniciar** (`ESP.restart()`): exactamente el mismo arranque que al despertar del deep sleep, con
  la intro, el último canal y el volumen guardado. No hay un arranque especial.
- En standby enciende una secuencia de **exactamente tres palmadas** que pasó las comprobaciones, con cada palmada a
  **−36 dBFS** o más (cerca de la tele) y **silencio alrededor**: ningún golpe en los 2,5 s anteriores ni en los
  ~1,2 s posteriores (la ventana de 0,7 s más 0,5 s de espera). Los ruidos de casa suelen venir en grupo; una persona
  da las palmadas tras un momento de silencio. El piloto destella con cada palmada que oye
  (`voice/VoiceStandby.h`, con tests). **No es infalible:** tres golpes al ritmo de unas manos y con silencio
  alrededor la encenderían igual, aunque es mucho más raro que con dos; y unas palmadas flojas, lejos o justo
  después de otro ruido no la encienden. Con la regla de dos palmadas se probó: 5 golpes sueltos que no la
  encendieron y la doble del usuario que sí, y una noche sin encenderse sola (deducido de la batería, sin log). Si los falsos encendidos molestan, AHORRO MAX
  los quita (pero entonces solo enciende una tecla).
- Historia: la primera versión encendía con el segundo golpe al momento, y el usuario vio que cualquier golpe,
  alarma o ruido la encendía. La segunda exigía una doble comprobada (ritmo, fuerza, brillo); paró la mayoría,
  pero un ruido de casa dio una pareja que pasaba todas las comprobaciones (−13/−17 dB, 52/66 % de agudos, más
  brillante que las palmadas del usuario, 32–35 %), aunque llegó 1,6 s después de otro golpe. Ninguna regla de
  sonido separa con fiabilidad dos golpes al azar de dos palmadas. El usuario prefirió dos y la tercera versión exige
  silencio antes y después; con el uso, "con dos se cuelan muchas cosas aún" (2026-10-03), y apagar y encender pasó
  a **tres palmadas**, con el mismo silencio alrededor.
- No hay sonido propio que filtrar (el amplificador está apagado), así que el umbral es el de la sala.
- Sin Wi-Fi el mando web no responde, igual que con el deep sleep.
- **Carcasa sin botones** (`pio run -e voice_nokeys`, `PAUTV_HAS_KEYS 0`): apagar es STANDBY VOZ, porque del deep
  sleep solo despertaría una tecla. Si las palmadas o el micrófono están desactivados, ⏻ apaga a STANDBY WI-FI (la
  Wi-Fi sigue y el mando web la enciende); sin Wi-Fi tampoco, no se apaga ("NO SE APAGA"). Con la batería agotada, el deep sleep se despierta cada 5 min y arranca en cuanto la
  batería lee 3,7 V o más (cargando). Lógica en `src/power/Standby.h`, con tests; serie `F` simula ese apagado.
- Por serie (sin teclas): `q` apaga y `W` enciende desde STANDBY VOZ.

## Grabadora y canal MENSAJES (v0.3)

**Grabar** (solo a propósito y con **● REC** en pantalla):

1. **AJUSTES → VOZ → GRABAR MENSAJE** (VOL+), o serie `R`. No hay atajo de teclas (una combinación como CH− + VOL−
   está pensada, pero **no implementada**).
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

**El canal MENSAJES** es un canal interno más. El firmware con voz **lo añade solo** a `channels.json` la primera vez
que arranca, con el número siguiente al más alto (o el primero libre si el 999 está cogido), para que salga en el
mando web y en el zapeo sin tocar la tarjeta. Si ya hay uno (o un canal con id `mensajes`), no añade nada. Una marca
en NVS recuerda que se añadió: si lo borras de `channels.json`, no vuelve. También se puede escribir a mano:

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

Después (2026-10-03): «Hola ESP» y «Hey Retro» van en una app de standby aparte con ESP-IDF 5; «Hey Retro» sin
pagar, con microWakeWord (TFLite Micro) y un modelo entrenado en el Mac. Ver [WAKEWORD.md](WAKEWORD.md).

## Pantallas y órdenes

- **MICROFONO:** AJUSTES → DIAGNOSTICO → **CH−**. Barra VU segmentada (−60 a 0 dB, verde, amarillo, rojo, con
  el pico retenido 1 s en blanco), RMS, PEAK, recortes, errores de lectura y la última secuencia oída. MENU
  (mantener CH+) vuelve.
- **AJUSTES → VOZ:** MICROFONO ON/OFF (pausa la captura), PALMADAS ON/OFF, SENSIBLE 0–100 de 10 en 10, APAGADO
  (AHORRO MAX o STANDBY VOZ), LED ESCUCHA ON/OFF (el destello en STANDBY VOZ) y GRABAR MENSAJE.
- **Mando web → AJUSTES → VOZ:** los mismos ajustes salvo GRABAR MENSAJE (`/api/config/voice`, con el móvil
  emparejado). Un cambio desde el móvil se ve al momento si el menú VOZ está abierto en la tele. Sin teclas, APAGADO
  sale fijo en STANDBY VOZ.
- **Serie** (115200, compilación con `PAUTV_DEBUG_STATS`): `R` = grabar (o parar), `Y` = mensaje sintético, `E` =
  borrar los mensajes, `q` = apagar, `W` = encender desde STANDBY VOZ; `v` = MIC TEST, 10 s de
  `[MIC] rms=… peak=… clip=… overrun=… | clap floor=… thr=… claps=… long=… own=…`; `V` = pausa o reanuda la
  captura (para medir su coste). Cada secuencia deja `[CLAP] double (gap 296 ms, peak -11.3 dB, floor -58.3 dB,
  tv rise 2.1 dB) in PLAYING`. Sin botones, los menús se recorren por serie con `M` (menú), `n`/`p` (CH) y
  `+`/`-` (VOL). `T !/retrotv/media/…mjpeg` pone un capítulo **desde 0:00** (para repetir una escena concreta
  contra el detector; `T ruta` sin `!` lo pone "en emisión").

## Privacidad

- RETROTV **no almacena ni envía audio** por su cuenta: ni para las palmadas, ni en STANDBY VOZ. Cada bloque de
  11,6 ms se mide y se sobrescribe; solo salen niveles en dB y el número de palmadas.
- **Lo único que se guarda** son los mensajes de la grabadora: a petición, con **● REC** en pantalla, como mucho
  15 s, y solo en la SD de la tele. No hay forma de empezar una grabación desde la red.
- Todo es local: ni nube, ni servicios de reconocimiento.

## Tests

- **En el ordenador** (`tools/run_host_tests.sh`), `test/voice_tests.cpp`: niveles en dB, silencio, continua,
  seno, recortes; y para las palmadas: silencio y ruido constante (sin palmadas), palmada corta, sonido largo
  (no), eco a 50 ms (ignorado), doble y triple, la doble espera a que cierre la ventana, dos golpes a 160 ms (no es
  doble), secuencia lenta = dos sueltas, la segunda palmada no se pierde por la cola de la primera, fondo
  dinámico en una sala ruidosa, sensibilidad, margen extra con sonido, sonidos propios suprimidos, vuelta de
  `millis()`; golpes sordos, parejas desiguales o lentas, alarmas, la envolvente del programa; STANDBY VOZ (silencio
  antes y después); grabadora (remuestreo, nivel de voz, cabecera WAV, nombres, flujo 3-2-1/REC).
- **En la placa:** TV1–TV20 (y TV7b, TV13c) en [TEST_PLAN.md](TEST_PLAN.md), sección *RETROTV Voice*. Nada se
  marca OK solo porque compile.

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
- Falsos positivos: 30 min sin fallos con una escena concreta; falta una validación larga con otros programas,
  volúmenes y salas. En STANDBY VOZ, falta medir los falsos encendidos con log durante horas.
- Consumo de STANDBY VOZ estimado (~40 mA), no medido con un medidor.
- El micrófono cuesta 16 KB de heap interno; con todo lo demás quedan ~70 KB libres (bloque mayor 61 KB).
- Los comandos de voz **no están implementados**: MultiNet no tiene español. «Hola ESP» y «Hey Retro» van, en prueba,
  en una app de standby aparte: [WAKEWORD.md](WAKEWORD.md).
