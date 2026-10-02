# RETROTV Server

Servidor en el ordenador de casa que da a la tele **canales por red** en el único formato que reproduce: MJPEG
320×240 + AAC ADTS mono, por HTTP normal. La tele no sabe qué hay detrás. Para ella, un canal remoto es solo una URL
de este servidor.

```
fuente (archivos · HLS oficial · 3Cat; más adelante RTVE, Tunarr)
  → provider (encuentra el medio: docs/PROVIDERS.md)
  → sesión (fija el instante t0; en directo, un FFmpeg: HLS → MJPEG + AAC)
  → HTTP: /session/<id>/video + /session/<id>/audio
  → ESP32 → el mismo MediaPlayer que lee de la SD
```

Uso personal. Por defecto solo escucha en el propio ordenador. En la red de casa (`--lan`) no hay que abrir nunca el
puerto hacia Internet: no hay autenticación y no es un relay público.

## Arrancar

```sh
server/run.sh          # 127.0.0.1:8080: solo este ordenador (curl, pruebas)
server/run.sh --lan    # 0.0.0.0:8080 y anuncia retrotv-server.local para que llegue la tele
```

El primer arranque crea `server/.venv`, instala `requirements.txt` y copia `config/channels.example.json` a
`config/channels.json`, que es tu configuración y no va al repo. También genera el clip de demo en `media/demo/`: es
sintético, hecho con ffmpeg, así que no incluye contenido de terceros. Requisitos: `python3` (3.10 o superior) y
`ffmpeg`, y funciona en macOS y en Linux.

Para pararlo: Ctrl+C. Si la tele está conectada, sus streams se cortan al cabo de 1 s: la tele muestra NO SIGNAL y
reintenta sola, así que al arrancarlo otra vez la imagen vuelve.

**macOS:** la primera vez que la tele se conecta, el firewall pregunta si Python puede aceptar conexiones entrantes.
Hay que permitirlo. Si no, la tele se queda en `NO SERVER` aunque el ping funcione. Se cambia en Ajustes del Sistema
→ Red → Firewall → Opciones.

Comprobar que responde:

```sh
curl http://127.0.0.1:8080/health          # {"status":"ok"}
curl http://retrotv-server.local:8080/api/channels # desde otro equipo de la red, con --lan
```

## En un NAS (Docker)

Para que los canales de red funcionen sin el Mac, el servidor puede vivir en un NAS o en cualquier Linux siempre
encendido. La imagen (`server/Dockerfile`) lleva el servidor, FFmpeg y el clip de demo; la lista de canales se monta
desde fuera. Pesa unos 840 MB (casi todo FFmpeg) y usa unos 50 MB de RAM. Solo red de casa: nunca abras el puerto
a Internet.

- **Red `host`:** el contenedor anuncia `retrotv-server.local` por mDNS como el Mac; con la red por defecto de Docker no
  podría. Si el NAS no deja anunciarlo, los canales de la SD pueden usar la IP fija del NAS.
- **Un solo servidor:** para el del Mac antes de arrancar el del NAS; dos anunciando `retrotv-server.local` chocan.
- **Con Docker Compose y el repositorio en la máquina:** `docker compose -f server/docker-compose.yml up -d --build`.

### QNAP (Container Station)

El panel de QNAP ya usa el puerto 8080: aquí el servidor va en el **8090**, y los canales de red de la SD pasan a
`http://retrotv-server.local:8090/channel/<n>`.

1. En el Mac, construye la imagen para la CPU del NAS (`amd64` en los Intel/AMD, `arm64` en los ARM) y guárdala:
   ```
   docker buildx build --platform linux/amd64 -f server/Dockerfile -t retrotv-server:latest --load .
   docker save retrotv-server:latest | gzip > retrotv-server.tar.gz
   ```
2. Copia `retrotv-server.tar.gz` y `server/config/channels.json` a una carpeta compartida del NAS, por ejemplo
   `Container/retrotv/`.
3. Container Station → Imágenes → Importar → `retrotv-server.tar.gz`.
4. Container Station → Aplicaciones → Crear, con este YAML:
   ```yaml
   services:
     retrotv-server:
       image: retrotv-server:latest
       network_mode: host
       restart: unless-stopped
       environment:
         PAUTV_ANNOUNCE: "1"
         PAUTV_PORT: "8090"
       volumes:
         - /share/Container/retrotv/channels.json:/app/server/config/channels.json:ro
   ```
5. Para el servidor del Mac y comprueba desde el Mac: `curl http://retrotv-server.local:8090/health` → `{"status": "ok"}`.
6. Cambia el puerto de los canales de red en el `channels.json` de la SD (`:8080` → `:8090`).

Para cambiar la lista de canales del servidor: edita el `channels.json` del NAS y reinicia la aplicación.

## Por qué FastAPI

Python ya hace falta para las herramientas del repo. FastAPI da streaming con contrapresión (la tele lee a velocidad
de reproducción y TCP frena la lectura del archivo, así que no se acumula nada en memoria) y APIs JSON. Además, el
`TestClient` sirve para los tests. Llamar a FFmpeg desde Python (HLS, V0.2b) será un `subprocess` controlado desde
un solo sitio.

## API

| Método y ruta | Respuesta |
|---|---|
| `GET /health` | `{"status": "ok"}` |
| `GET /api/channels` | `{"channels": [{"id", "number", "name", "provider", "online"}]}` |
| `GET /api/status/<n>` | Estado del canal. Archivos: sesiones, `position_ms`, `indexed`, `duration_ms`. En directo: `provider`, `source: "hls"`, `ffmpeg` (`idle`, `running`, `restarting`, `failed`), `pid`, `uptime_s`, `restarts`, `profile`, `video_output_kbps`, `video_backlog_bytes`, `audio_output_kbps`, `source_latency_ms` y, en 3Cat, `now_playing`. Offline: `{"online": false, "reason": "…"}` |
| `GET /api/guide` | Lo que emiten ahora y después los canales cuya fuente lo publica (3Cat: `arafem`/`despresfem`; Pluto TV: la parrilla del canal): `{"now_ms", "channels": [{"number", "airings": [{"title", "start_ms", "end_ms"}]}]}`, como mucho 3 programas por canal y títulos de 64 caracteres. Cada fuente se consulta como mucho una vez por minuto; si falla, ese canal no sale. Lo usa el teletexto de la tele |
| `POST /api/sessions/<n>` | Sintoniza: `{"session_id", "channel", "position_ms", "video", "audio", "fps", "prebuffer_ms"}` (`audio` es `null` si el canal no tiene). `fps` es el ritmo del vídeo (24 en archivos, el del perfil en directo); `prebuffer_ms`, cuánto vídeo reúne la tele antes de empezar. En directo responde solo cuando FFmpeg ya produce vídeo y audio (máximo 10 s) |
| `GET /api/debug/sessions` | Sesiones abiertas: canal, provider, edad, clientes de vídeo y audio, PID de FFmpeg. Para desarrollo, en la LAN |
| `GET /api/bench?seconds=N` | N s (1–30) de bytes de relleno tan rápido como TCP los acepta, para medir el caudal hacia la tele (comando serie `B`). Máximo 4 a la vez. Para desarrollo, en la LAN |
| `GET /session/<id>/video` | MJPEG crudo desde t0, `video/x-motion-jpeg` |
| `GET /session/<id>/audio` | AAC ADTS desde t0, `audio/aac` |

Errores:
- **404:** el canal o la sesión no existen.
- **409:** un stream de la sesión ya se abrió.
- **503:** el canal está offline, con el motivo en `detail`. Por ejemplo: falta el archivo, `manifest unavailable`,
  `encrypted (DRM): unsupported`, `3Cat offers no stream for this channel now`, o FFmpeg no arrancó.

### Directo: HLS → FFmpeg → la tele

Cada sesión en directo es **un** proceso FFmpeg (`app/ffmpeg.py`, el único sitio que lanza procesos):
- **Una entrada y dos salidas.** El vídeo y el audio salen por dos pipes extra que el proceso recibe como
  descriptores (`pipe:<fd>`). Es POSIX, igual en macOS y Linux, y no deja nada en disco.
- **Mismo instante:** comparten el reloj de FFmpeg y los dos empiezan en el tiempo 0. Se rellena con la primera
  imagen o con silencio lo que empiece más tarde.
- **Imagen:** la misma que `tools/convert_video.sh`. 320×240 en MJPEG `yuvj420p`, a los fps del perfil; el 4:3 llena la pantalla
  y el 16:9 va a 320×176 con bandas negras, sin deformar. La imagen se corta a bloques JPEG enteros de 16 píxeles
  (2 filas arriba y 2 abajo, como el overscan de una tele): con el borde de la banda a mitad de bloque, el ruido
  del JPEG manchaba la banda y en la tele salían rayas de colores.
- **Sonido:** AAC ADTS mono a 44,1 kHz y 32 kbps. `loudnorm` es opcional (`PAUTV_LOUDNORM=1`); en directo va
  apagado, porque añade CPU y ~3 s de retardo.
- **Ritmo:** tiempo real (`-readrate 1`) empezando tres segmentos por detrás del más nuevo, con 2 s de ventaja que
  acaban de colchón en la tele.
  - **Por qué no el más nuevo:** FFmpeg iría pegado al borde del directo y pararía 1–5 s en cada segmento esperando
    al siguiente.
  - **Coste:** ~12 s más de retraso en SX3.
  - **Cambiarlo:** `PAUTV_LIVE_START_INDEX` (−1…−6).
- **Memoria acotada:** los dos pipes se vacían siempre a búferes limitados (~1 MB de vídeo). Una tele que se queda
  más atrás corta la sesión y lo deja en el log.
- **Fuente que cae:** hasta 3 reinicios (a los 1, 2 y 4 s), también si deja de publicar (8 s sin vídeo). Después la
  sesión acaba y la tele reintenta por su cuenta.
- **Parada:** SIGTERM, SIGKILL a los 2 s y siempre se espera al proceso. No quedan zombis, y ninguna sesión vive más
  que su FFmpeg ni al revés: cuelgue, caducidad, sustitución o apagado del servidor.
- **Perfil:** fps y calidad MJPEG del directo. Por defecto `20fps-q14`, el mejor en las medidas de
  `docs/NETWORK_TUNING.md` (0 descartes, ~0,9 Mbps en SX3). A 24 fps la tele descarta fotogramas mientras recibe
  por Wi-Fi.

  | Perfil | fps | `q:v` |
  |---|---|---|
  | `24fps-q12` | 24 | 12 |
  | `24fps-q16` | 24 | 16 |
  | `20fps-q14` (defecto) | 20 | 14 |
  | `20fps-q18` | 20 | 18 |
  | `18fps-q16` | 18 | 16 |

  Se elige con `PAUTV_LIVE_PROFILE` o por canal (`"profile"` en `channels.json`). `video_quality` por canal y
  `PAUTV_LIVE_QUALITY` cambian solo la calidad. La tele recibe los fps en la sesión, así que el firmware no cambia.
- **Prebuffer:** vídeo que reúne la tele antes de empezar: 1,5 s por defecto. `PAUTV_PREBUFFER_MS` (1000–3000) o
  `"prebuffer_ms"` por canal. Entre 1 y 2 s no se midió diferencia de estabilidad; 3 s arranca ~1,2 s más tarde.
- **Estadísticas:** una línea `[STATS]` por sesión:
  - qué produjo FFmpeg y qué se llevó la tele (kbps);
  - lo que espera en el búfer (backlog).

  Sale cada segundo los primeros 30 s y después cada `PAUTV_STATS_PERIOD_S` (5 s). Un backlog que crece indica
  que la tele lee más despacio que el directo.

Consumo medido en este Mac:

| Canal | Entrada | Salida | CPU de FFmpeg | Memoria | Listo en |
|---|---|---|---|---|---|
| 3Cat SX3 | 854×480 25 fps H.264 + AAC estéreo | 320×240 24 fps, ~570 kbps de vídeo + 37 kbps de audio | 2–4 % de un núcleo | ~78 MB | ~1,9 s |
| HLS TEST | 1280×720 H.264 + AAC | ~950 kbps de vídeo (q12) + 38 kbps | 4,5–7 % | ~108 MB | 5–7 s |

**Latencia:**
- La fuente de SX3 va ~9 s por detrás de su segmento más nuevo (`source_latency_ms`).
- En la tele se suman ~2–3 s: la ventaja de 2 s y el colchón de ~1,3 s que espera la tele antes de empezar.

**Varias teles:** la V0.2 es para una sola. Si llega otra, funciona, pero con su propio FFmpeg; hay un máximo de 8
sesiones y se descarta la más antigua. No hay difusión compartida.

### Sesiones: vídeo y audio en el mismo instante

Vídeo y audio van por dos conexiones. Si cada una calculara su propia posición, podrían empezar en momentos
distintos (t=120 y t=121,5 s) y quedar desfasadas para siempre. Por eso la tele pide primero una sesión:
- **t0:** el servidor calcula una vez dónde va el canal (`hora mod duración`, como hace la tele con la SD) y toma la
  entrada del `.idx` de ese segundo.
- **Mismo punto de partida:** esa entrada da el byte de vídeo y el de audio del mismo segundo, y los dos streams de
  la sesión empiezan ahí.
- **Validación:** si el `.idx` no coincide con el archivo, los dos streams empiezan desde 0.
- **Cierre:** la sesión termina cuando la tele cierra sus conexiones. Si queda sin ningún stream abierto durante
  15 s (nunca conectó, o colgó a medias), caduca. Máximo 8 sesiones: si hay más, se descarta la más antigua.
- **Ritmo:** sin pausas artificiales. Con `loop`, al acabar el archivo vuelve a empezar y los fotogramas MJPEG y
  ADTS siguen uno detrás de otro. El audio es el reloj de la tele, así que una pequeña diferencia de duración entre
  pistas se corrige sola en cada vuelta.

## Canales (`config/channels.json`)

```json
{ "channels": [
  { "id": 1, "number": 1, "name": "REMOTE DEMO", "provider": "local", "source_type": "file",
    "video": "media/demo/demo.mjpeg", "audio": "media/demo/demo.aac", "loop": true },
  { "id": 2, "number": 2, "name": "HLS TEST", "provider": "hls", "source_type": "hls",
    "source": "https://demo.unified-streaming.com/k8s/live/stable/scte35.isml/.m3u8", "audio_language": "en" },
  { "id": 10, "number": 10, "name": "SX3", "provider": "3cat", "source_type": "live", "provider_channel": "sx3" }
] }
```

- **Formato de los archivos:** los de `provider: "local"` ya van en el formato de la tele; los hace
  `tools/convert_video.sh`, igual que para la SD.
- **Índice:** con el `.idx` al lado (`tools/make_index.py`), el canal está **en emisión**: al sintonizar entra por
  donde va. Sin índice, empieza desde el principio.
- **Rutas:** las relativas parten de `server/`. Para cambiar el archivo de canales o la carpeta base: `PAUTV_CHANNELS`
  y `PAUTV_MEDIA_ROOT`.
- **`hls`:** la URL de un HLS público y oficial en `source`, con `audio_language` opcional. Solo se reproducen URLs
  de este archivo.
- **`3cat`:** el código del canal en `provider_channel`: `sx3`, `tv3`, `c33`, `324`, `esport3`, `tem2` (3Cat Anime) y los
  canales 24 h `fc3` (Doraemon), `fc1` (Plats bruts), `fc2` (Joc de cartes) y `fc4` (Vinagre(ta)). El
  manifiesto se pide en cada sintonía a la API oficial de 3Cat; no hay URLs que mantener. Canales, zonas y
  limitaciones en [docs/PROVIDERS.md](../docs/PROVIDERS.md).
- **`pluto`:** `provider_channel` es el *slug* del canal en la guía de Pluto TV España (`one-piece-es`,
  `lupin-es`…). Pluto cifra sus streams con AES-128; por decisión del usuario es la única excepción al "cifrado =
  no soportado" (ver docs/PROVIDERS.md). `"crop_4_3": true` para series 4:3 emitidas en un cuadro 16:9.
- **`rtve`:** `provider_channel` es `24h`, `clan`, `la1`, `la2` o `tdp`. En cada sintonía se comprueba en la página
  oficial de RTVE si el canal lleva DRM o pide iniciar sesión; si es así, sale offline con el motivo. Hoy solo
  24h está disponible.
- **`enabled: false`** lista el canal pero lo deja offline. En directo, `profile`, `video_quality` y `prebuffer_ms`
  cambian el perfil, la calidad y el prebuffer de ese canal. Un perfil desconocido o un prebuffer fuera de
  1000–3000 dejan el canal con error.
- **Otros providers** (`rtve`, `tunarr`): se aceptan en el archivo y salen como offline hasta que existan.
- **Un error en una entrada** la deja offline con el motivo, sin tumbar el servidor.

## En la tele

En `channels.json` de la SD:

```json
{ "id": "remote_demo", "number": 20, "name": "Remote Demo", "type": "remote",
  "source": "http://retrotv-server.local:8080/channel/1", "enabled": true },
{ "id": "sx3", "number": 21, "name": "SX3", "type": "remote",
  "source": "http://retrotv-server.local:8080/channel/10", "enabled": true }
```

Al sintonizar un directo, la tele muestra estática ~3 s (hasta ~8,5 s si la Wi-Fi acaba de conectar). En ese
tiempo el servidor arranca FFmpeg y la tele reúne `prebuffer_ms` de vídeo antes de empezar.

- **La URL:** es la del servidor más `/channel/<n>`, donde `<n>` es el número del canal en este servidor.
- **Si `retrotv-server.local` no resuelve:** pon la IP del ordenador. La ve `run.sh --lan` en su log (`announced retrotv-server.local
  -> 192.168.1.x`).

## Diagnóstico y errores comunes

| Síntoma | Causa probable |
|---|---|
| La tele muestra `NO SIGNAL / OFFLINE` | La tele no tiene Wi-Fi. Los canales locales siguen funcionando |
| `NO SIGNAL / NO SERVER` | Servidor parado, `run.sh` sin `--lan`, firewall de macOS bloqueando Python, o `retrotv-server.local` sin resolver (pon la IP) |
| `NO SIGNAL / SIGNAL LOST` | Se cortó a mitad: el servidor se paró o el Wi-Fi dejó de llegar 3 s. La tele reintenta sola |
| `NO SIGNAL / NO CHANNEL` | Ese número no existe en `config/channels.json` |
| `NO SIGNAL / CHANNEL OFF` | El canal existe pero está offline (fuente caída, DRM, 3Cat no lo ofrece aquí): mira `curl .../api/status/<n>` y su `reason` |
| `NO SIGNAL / NO DATA` | Conectó pero no llegó el prebuffer a tiempo (6 s + 2 × prebuffer): Wi-Fi lenta en ese momento (frecuente los primeros ~30 s tras encender la tele) o el Mac muy cargado. La tele reintenta sola |
| Imagen a saltos en un directo | Mira `[NET]` en la tele y `[STATS]` aquí. Huecos en la salida de FFmpeg (0 kbps unos segundos): sube `PAUTV_LIVE_START_INDEX` hacia −4. Descartes con el búfer lleno: un perfil más ligero (`20fps-q18`) |
| `mDNS announce failed` en el log | Usa la IP en lugar de `retrotv-server.local` |

El log del servidor dice cada sesión creada, conectada y cerrada (`[SESSION]`), cómo arranca y termina cada FFmpeg
(`[FFMPEG]`: primeros bytes, reinicios, motivo) y qué resuelve cada provider (`[3CAT]`, `[HLS]`).

## Tests

```sh
cd server && .venv/bin/pip install -r requirements-dev.txt && .venv/bin/python -m pytest
```

- **API:** health, lista de canales, estado, canal inexistente u offline, sesión inexistente, caducidad y límite de
  sesiones, e índice.
- **Playlists y providers**, sin red: elección de variante y audio, variantes solo de audio, cifrado, hora de 3Cat,
  URL inválida, fuente caída, API de 3Cat simulada (zonas, host no oficial, http, no HLS, API caída o con basura).
- **FFmpeg:**
  - **con FFmpeg real** sobre una fuente HLS local de 16:9: salida MJPEG 320×240 y ADTS 44,1 kHz mono, parada doble
    y proceso recogido;
  - **con uno falso:** fallo al arrancar (404), timeout de arranque, un proceso que ignora SIGTERM, caída a mitad
    con reinicios, fuente que deja de publicar, lector que se queda atrás y binario que falta.
- **Sesiones en directo contra un uvicorn real:**
  - solo hay sesión cuando hay datos;
  - colgar o cambiar de canal mata el FFmpeg anterior;
  - una fuente caída da un 503 con motivo;
  - apagar el servidor no deja FFmpeg vivos.
- **Contra un uvicorn real** leído con sockets, como la tele:
  - vídeo y audio empiezan en el mismo segundo;
  - el bucle vuelve al principio;
  - cada stream solo se abre una vez;
  - si la tele cuelga a mitad, la sesión se cierra;
  - parar el servidor con un stream abierto lo corta y el proceso termina.

Todo el directo de punta a punta, sin placa ni Internet:

```sh
server/.venv/bin/python server/tools/test_hls_pipeline.py
```

Monta una fuente HLS local en directo y comprueba:
- el formato de salida;
- que vídeo y audio van a tiempo real al mismo ritmo;
- que FFmpeg muere al colgar;
- la caída de la fuente y su recuperación;
- que no queda ningún FFmpeg al terminar.

Con la placa: `tools/remote_device_tests.py` (archivo) y `tools/remote_device_tests.py --live` (HLS: fuente que cae
y vuelve y 20 zapeos). Ver el README principal.
