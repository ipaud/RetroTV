# Tuning de red del directo (SX3)

Fase de diagnóstico del 2026-09-30, antes de RTVE. La pregunta: ¿qué limita el directo en la tele? Las opciones
eran el caudal Wi-Fi, la ventana TCP, las pérdidas, una lectura demasiado irregular o un búfer insuficiente.
Aquí están las medidas y la recomendación que sale de ellas. No se tocó lwIP ni el core de Arduino.

Montaje:
- Placa FNK0104A a −56…−64 dBm del router de casa (2,4 GHz), sin modem sleep.
- RETROTV Server en el Mac (FFmpeg 7.1), en la misma red.
- Canal 10 = 3Cat SX3: HLS de 480p con segmentos de 6 s, transcodificado a MJPEG 320×240 + AAC mono.
- SX3 emite programación real, así que el caudal cambia con el programa. Cada ejecución lo mide en su momento.

## Cómo se mide

`tools/stream_profiles.py` hace todas las medidas. Maneja la placa por serie y arranca su propio servidor, una vez
por ejecución con su configuración.

| Fase | Qué hace |
|---|---|
| `cold` | Sintoniza en cuanto la Wi-Fi conecta (60 s): la primera sintonía tras encender |
| `bench` | Caudal TCP bruto hacia la placa con 1, 2 y 4 conexiones (comando serie `B`), al conectar y con el enlace ya caliente. Mide a la vez el ping y las retransmisiones del servidor |
| `profiles` | Cada perfil 2,5 min en SX3 |
| `livestart` | El mismo perfil empezando en el segmento más nuevo (−1) y tres segmentos atrás (−3) |
| `prebuffer` | 1,0, 2,0 y 3,0 s de prebuffer |
| `sd` | Errores de SD: directo y SD alternados (8 min), luego solo la SD (3 min) |
| `soak` | 30 min en directo con `S0`: memoria cada minuto |

Fuentes de cada dato:

| Dato | De dónde sale |
|---|---|
| `mean kbps`, `p95 kbps` | Lo que produjo FFmpeg por segundo (vídeo + audio, línea `[STATS]` del servidor), sin los 3 primeros segundos (la ráfaga inicial) |
| `first video ms` | `[TUNE] time_to_first_video_ms`: desde la orden de sintonizar hasta el primer fotograma en pantalla |
| `stable ms` | `[TUNE] time_to_stable_playback_ms`: inicio de la primera racha de 5 s limpios (sin descartes, sin esperas de lectura, ≥ fps−2 fotogramas por segundo). Va a segundos del reproductor, así que su precisión es de ±1 s |
| `dropped`, `fps`, `drift` | `[NET]`: descartes, fotogramas mostrados por segundo y desfase A/V (media y máximo) |
| `stalls`, `stall ms` | `[NET] read_stalls`: veces que el reproductor encontró el búfer vacío; `wait_ms`, el tiempo que esperó datos en total |
| `gap max ms` | La espera más larga de la tarea de red por un byte de vídeo, con sitio en el anillo |
| `buf min KB` | El mínimo del búfer de vídeo de la tele |
| `ping` | Ping del Mac a la placa cada 0,2 s durante la prueba |
| `re-tx %` | Bytes retransmitidos por el servidor en sus conexiones a la placa (`nettop`, por socket). Los contadores TCP globales de macOS salen a 0 sin privilegios |

`[NET]` sale cada segundo los primeros 30 s tras sintonizar y después cada 5 s. `[WIFI] +Ns rssi` sale cada segundo
los primeros 30 s tras conectar. El servidor escribe `[STATS]` cada segundo los primeros 30 s y después cada
`PAUTV_STATS_PERIOD_S` (5 s por defecto; la herramienta pone 1).

## Resultados

### Caudal bruto hacia la placa

`B <n>` abre n conexiones a `/api/bench`. El servidor envía bytes de relleno tan rápido como TCP los acepta, y la
placa no decodifica nada.

| Momento | Conexiones | Total KB/s | Por conexión KB/s | Ping p50 / p95 ms | Re-tx % | SRTT ms |
|---|---|---|---|---|---|---|
| Nada más conectar | 1 | **60** | 60 | 72 / 289 | 0,85 | 58 |
| Enlace caliente | 1 | **511** | 511 | 10 / 16 | 0 | 8,6 |
| Enlace caliente | 2 | 747 | 375 + 372 | 11 / 23 | 0 | 13,0 |
| Enlace caliente | 4 | 1005 | 250 + 253 + 251 + 249 | 16 / 28 | 0 | 16,8 |

- **Una conexión va limitada por la ventana:** 5760 B / 8,6 ms ≈ 670 KB/s, del orden de los 511 medidos. Con más
  conexiones el total sube (×1,5 con 2 y ×2,0 con 4), así que el límite de cada una es la ventana y no el aire.
- **El margen es grande:** 511 KB/s por conexión caliente, frente a ~100–140 KB/s que pide el directo.
- **Al conectar, el enlace es lento:** en sus primeros ~10 s dio entre 20 y 165 KB/s por segundo, con el RSSI
  estable (−58…−61 dBm). No es falta de señal: el RTT sube a 58–72 ms (p95 de 289 ms) y, con una ventana fija,
  el caudal cae en proporción. En la ejecución 1 el enlace ya iba caliente a los ~40 s de conectar.

### Perfiles, empezando en el segmento más nuevo (`-live_start_index -1`, como hasta ahora)

Ejecución 1, 2,5 min por perfil:

| Perfil | Media kbps | p95 kbps | Primer vídeo ms | Estable ms | fps | Descartes/min | Esperas (ms) | Hueco máx. ms | Búfer mín. KB | Re-tx % |
|---|---|---|---|---|---|---|---|---|---|---|
| 24fps-q12 | 1226 | **4655** | 2776 | nunca | 18,8 | 262,3 | 346 (21215) | 3798 | 0 | 0,00 |
| 24fps-q16 | 1051 | 1325 | 8061 | 40018 | 23,1 | 25,8 | 0 (1066) | 266 | 381 | 0,02 |
| 20fps-q14 | 919 | 2822 | 2559 | 2543 | 18,3 | 66,1 | 130 (11449) | 3345 | 0 | 0,01 |
| 20fps-q18 | 872 | 1187 | 2949 | 3009 | 19,2 | 9,5 | 21 (2371) | 4946 | 0 | 0,00 |
| 18fps-q16 | 798 | 3335 | 2664 | 2665 | 16,2 | 60,8 | 123 (13477) | 4126 | 0 | 0,01 |

Esta tabla no ordena los perfiles; mide la fuente. Las esperas no siguen al caudal del perfil sino a los huecos:
- **Salida del servidor:** el propio FFmpeg deja de producir durante 1–5 s, más o menos cada 6 s (la duración de
  un segmento de 3Cat), y luego recupera en ráfaga. En la ejecución de prueba, `[STATS]` dio 0 kbps en t=5, 11,
  17, 23 y 29 s, y ~2000 kbps en el segundo siguiente. La tele leía todo lo que llegaba: backlog 0.
- **Causa:** con `-live_start_index -1` y `-readrate 1`, FFmpeg lee pegado al borde del directo. Al acabar cada
  segmento, espera a que 3Cat publique el siguiente.
- **Por qué varía entre perfiles:** depende de en qué punto del ciclo de 6 s arranca cada sesión. 24fps-q16 cayó
  en un buen momento: huecos de 266 ms, p95 casi igual a la media y 0 esperas.

Descartes por segundo según el caudal de ese segundo (1 Hz, primeros 30 s de cada perfil, 174 muestras):

| Caudal de ese segundo | Segundos | Descartes/s | Esperas/s |
|---|---|---|---|
| < 600 kbps (hueco) | 31 | 0,10 | 0,29 |
| 600–1400 kbps (normal) | 107 | 0,46 | 0,00 |
| 1400–2200 kbps (ráfaga) | 25 | 3,16 | 1,64 |
| ≥ 2200 kbps (ráfaga) | 11 | 8,64 | 4,45 |

Los descartes llegan con la ráfaga que sigue a cada hueco. En ese momento, el reloj de audio ya ha avanzado y los
fotogramas llegan tarde. Además, la tarea de red y lwIP cargan el núcleo 0, que es el que decodifica.

### Empezar tres segmentos atrás (`-live_start_index -3`)

| Ejecución | Inicio | Media kbps | p95 kbps | Primer vídeo ms | Estable ms | Descartes/min | Esperas | Hueco máx. ms |
|---|---|---|---|---|---|---|---|---|
| 1 | −1 | 1062 | 1523 | 3139 | 8824 | 55,5 | 0 | **3573** |
| 1 | −3 | 1120 | 1411 | 5393 | nunca | 66,2 | 0 | **301** |

Con −3 FFmpeg tiene siempre un segmento publicado por delante. Los huecos de la fuente desaparecen y el p95 del
caudal se queda cerca de la media. Cuesta ~12 s más de retraso frente a la emisión, y nada más. Esta muestra de −1
no tuvo esperas, porque le tocó un buen punto del ciclo, pero sí tuvo el hueco de 3,6 s.

### Perfiles con la fuente regular (−3)

Ejecución 2, 2,5 min por perfil:

| Perfil | Media kbps | p95 kbps | Primer vídeo ms | Estable ms | fps | Descartes/min | Esperas | Drift medio / máx. ms | Hueco máx. ms | Re-tx % |
|---|---|---|---|---|---|---|---|---|---|---|
| 24fps-q12 | 1073 | 1372 | 3515 | nunca | 22,6 | **54,7** | 0 | 23 / 62 | 987 | 0,00 |
| **20fps-q14** | 866 | 1162 | 3109 | 2951 | 19,5 | **0** | 0 | 4 / 29 | 355 | 0,00 |
| 20fps-q18 | 733 | 905 | 3209 | 3269 | 19,5 | **0** | 0 | 2 / 46 | 1004 | 0,00 |

A 24 fps se descartan fotogramas aunque el búfer esté lleno y la red sobre. La causa es el tiempo por fotograma:

| | Decodificar | Dibujar | Presupuesto por fotograma |
|---|---|---|---|
| SD, 24 fps (sin Wi-Fi activa) | 13–16 ms | 36–37 ms | 41,7 ms |
| Directo, 24 fps | 19–31 ms | 40–41 ms | 41,7 ms |
| Directo, 20 fps | 19–28 ms | 39–40 ms | 50 ms |

Con la Wi-Fi recibiendo, la decodificación del núcleo 0 tarda casi el doble (compite con lwIP y la tarea de red). A
24 fps el dibujo no deja holgura. A 20 fps sobran ~10 ms por fotograma, y los descartes pasan a 0.

### Prebuffer (20fps-q14, −3)

| Prebuffer | Primer vídeo ms | Estable ms | Descartes | Esperas | Búfer mín. KB | Hueco máx. ms |
|---|---|---|---|---|---|---|
| 1,0 s | 2993 | 3029 | 0 | 0 | 91 | 1025 |
| 2,0 s | 3237 | 3129 | 0 | 0 | 74 | 917 |
| 3,0 s | 4414 | 4319 | 0 | 0 | 130 | 954 |

- **Con la fuente regular, 1 s ya basta**, y pasar de 1 a 2 s apenas retrasa el arranque (+0,2 s): los primeros
  2 s llegan en la ráfaga inicial del servidor. Con 3 s se espera ~1,2 s más.
- **Con la fuente irregular (−1) no se midió el prebuffer.** Con huecos de hasta 4,9 s, lo más probable es que
  ni 3 s bastaran. El arreglo está en la fuente, no en el colchón.

### Primera sintonía tras conectar (`cold`, 20fps-q14, −3)

| Primer vídeo ms | Estable ms | Descartes | Esperas | Ping p95 ms | Re-tx % |
|---|---|---|---|---|---|
| 8462 | 8375 | 0 | 0 | 223 | 0,08 |

- **Arranca bien, pero tarde:** SX3 llegó a imagen a los 8,5 s, con el enlace aún frío (ping p95 de 223 ms), y
  después no tuvo ni descartes ni esperas.
- **Antes fallaba:** en la V0.2b, la primera sintonía tras arrancar daba `NO DATA` (T12.14). La causa era
  24 fps a q12 con un cebado fijo de 160 KB y 8 s de tope.
- **Qué la arregla:** ahora el cebado se cuenta en fotogramas (el `prebuffer_ms` de la sesión), su tope crece con
  él (6 s + 2 × prebuffer, hasta 14 s) y el perfil pesa menos.

### Errores de SD con la Wi-Fi cargada

| Ejecución | Tiempo de placa | Sintonías de la SD | Errores |
|---|---|---|---|
| 1 | 20 min | 21 | **3, en una sola ráfaga** |
| 2 (incluye la fase `sd`: 8 min alternando SX3 y SD, y 3 min solo SD con 20 zapeos) | 58 min | 45 | 0 |

La ráfaga de la ejecución 1:
- **Qué fue:** a los ~30 s del arranque, `sdmmc_read_sectors_dma` dio timeout (`0x107`) tres veces en 1 s
  (`sdmmc_read_blocks failed (263)`).
- **Qué hacía la placa:** la Wi-Fi llevaba 10 s conectada y el bench en frío acababa de terminar. El CH06 abría
  un capítulo: acababa de construir su programación leyendo 5 `.idx`.
- **Contexto:** RSSI de −58…−60 dBm, la red ya en reposo, nada reproduciéndose.
- **Efecto:** el capítulo salió como `missing` y el canal dio `NO SIGNAL: FILE ERROR`, y 3 s después pasó al
  siguiente. El archivo existe: en la ejecución 2 ese mismo capítulo se reprodujo sin problema.

Lo que coincide en las cuatro veces vistas hasta ahora (T11.13, T12.15 y esta):
- los errores llegan en ráfagas al **abrir** un canal o capítulo (listado, `.idx`, apertura), nunca durante la
  reproducción continua;
- siempre con la Wi-Fi recién conectada o con tráfico reciente.

La RSSI no varía en esos momentos. Con tan pocos casos no se puede separar la alimentación (picos de TX de la
radio sin ahorro de energía con la SD a 40 MHz) del reloj de la SD. Lo que sí está medido: con la Wi-Fi cargada
(~50 min de directo en la ejecución 2), la SD no dio ningún error.

### Memoria tras el soak (20fps-q14, −3, 30 min con `S0`)

| | Inicio | Fin |
|---|---|---|
| Heap libre | 144 KB | 144 KB (entre 143 y 144 en las 30 instantáneas) |
| Heap mínimo | 134 KB | 134 KB |
| Bloque más grande | 127 KB | 127 KB |
| PSRAM libre | 7477 KB | 7481 KB |

Resultado de los 30 min:
- **Reproducción:** 36.005 fotogramas, 0 descartes, 0 esperas, 0 reconexiones, 0 errores de audio.
- **Desfase A/V:** 4–5 ms de media, 46 ms de máximo.
- **Tareas:** todas vivas en cada minuto.
- **Memoria:** sin fugas.

El mínimo de 134 KB se alcanzó antes del soak y no bajó durante él. Es ~5 KB menos que en la V0.2b (139 KB),
porque el búfer de TX del USB pasa de 256 B a 4 KB y sale del heap.

## Dónde está el cuello de botella

| Hipótesis | Veredicto | Evidencia |
|---|---|---|
| Caudal Wi-Fi | **No** con el enlace caliente. **Sí** los primeros ~10–30 s tras conectar | 511 KB/s por conexión y 1005 KB/s con 4 frente a ~110 KB/s del perfil. Al conectar, 60 KB/s |
| Ventana TCP (5760 B) | Limita cada conexión a ventana/RTT, pero **no es el cuello hoy** | El total sube con más conexiones (511 → 747 → 1005). El directo usa ~20 % de lo que da una conexión. Solo aprieta cuando sube el RTT (al conectar: 58 ms → ~100 KB/s) |
| Pérdidas y retransmisiones | **No** | Re-tx 0–0,08 % en todas las pruebas; 0,85 % solo al conectar. Sin pérdidas de ping |
| Lectura irregular | **Sí, la causa principal de las esperas.** Viene de la fuente, no de la red | Con −1, FFmpeg para 1–5 s cada segmento de 6 s y recupera en ráfaga. Con −3, hueco máx. ≤ 1 s y 0 esperas en todas las pruebas |
| Búfer insuficiente | **No** con la fuente regular | 1 s de prebuffer ya da 0 esperas. Con los huecos de −1 (hasta 4,9 s) harían falta más de 3 s (no medido) |
| (No estaba en la lista) CPU a 24 fps | **Sí, la causa de los descartes** | Decodificar 19–31 ms con Wi-Fi activa. A 24 fps hay 25–65 descartes/min aunque la red sobre; a 20 fps, 0 |

## Recomendación

- **Perfil `20fps-q14`**, empezando tres segmentos atrás (`PAUTV_LIVE_START_INDEX=-3`):
  - 0 descartes y 0 esperas en todas las pruebas con fuente regular, también en la primera sintonía tras conectar;
  - A/V de 4 ms de media;
  - ~0,9 Mbps en SX3, ~20 % de lo que lleva una conexión caliente.
  - `20fps-q18` también va limpio con ~15 % menos caudal, pero se ve peor. Solo compensa si un canal más
    pesado aprieta.
- **Prebuffer 1,5 s** (sin cambios): entre 1 y 2 s no hay diferencia medible de estabilidad, y el arranque solo
  cambia 0,2 s.
- **Aplicado como valor por defecto del servidor:** `DEFAULT_PROFILE = "20fps-q14"` y `live_start_index = -3`. El
  firmware no cambia: la tele recibe los fps en la sesión. Cada valor se puede cambiar con `PAUTV_LIVE_PROFILE`,
  `PAUTV_LIVE_START_INDEX` y `PAUTV_PREBUFFER_MS`, o por canal con `profile` y `prebuffer_ms`.
- **Coste:** ~12 s más de retraso frente a la emisión (dos segmentos más atrás).

## ¿Hay que tocar lwIP?

**No, hoy no.**
- Con el enlace caliente, una conexión con la ventana de 5760 B lleva ~5 veces lo que pide el directo, y no hay
  pérdidas.
- Los dos problemas medidos (huecos de la fuente y CPU a 24 fps) no se arreglan con una ventana más grande.
- Una ventana mayor (core propio o `esp-idf` con `TCP_WND` más grande) solo ayudaría en los primeros ~10–30 s tras
  conectar, cuando el RTT sube. Aun así, `20fps-q14` ya arranca ahí sin fallar, aunque tarde 8,5 s.
- Tendría sentido volver a mirarlo si:
  - se quiere más calidad (24 fps con otro decodificador, más resolución);
  - una red peor da un RTT alto de forma sostenida;
  - dos streams pesados tienen que compartir el enlace.

## Siguiente paso

1. **Mañana, sin código:**
   - añadir SX3 a `channels.json` de la SD (T12.11) con estos valores y verlo a ojo;
   - hacer un soak de 1 h o más (T12.12).
2. **Errores de SD:** si vuelven, probar la SD a 20 MHz (`SDMMC_FREQ_DEFAULT`, ya es el valor de reserva)
   durante una ejecución de `--phases sd`. Si desaparecen, el problema es el reloj y no la alimentación. No se
   cambió la arquitectura de la SD en esta fase.
3. **Antes de RTVE:** RTVE puede tener segmentos de otra duración. Hay que medir su hueco máximo con la misma
   herramienta (`--channel <n> --phases profiles --profiles 20fps-q14`) antes de fiar el perfil.
4. **Solo si hace falta más calidad:** el límite es la CPU del núcleo 0, no la red. Las opciones serían mover la
   decodificación o la tarea de red de núcleo, o reducir el coste de dibujar. Eso es arquitectura del reproductor,
   fuera de esta fase.

## Repetir las medidas

```bash
server/run.sh   # una vez, para crear .venv
tools/stream_profiles.py --phases bench,profiles,livestart                  # ~20 min
tools/stream_profiles.py --phases cold,profiles,prebuffer,sd,soak \
    --profiles 24fps-q12,20fps-q14,20fps-q18 --profile 20fps-q14 --live-start -3   # ~60 min
```

Cada ejecución deja en `--out` el log serie con la hora de llegada de cada línea, el log del servidor y
`results.json`.
