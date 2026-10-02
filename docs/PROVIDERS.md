# Providers de RETROTV Server

Un *provider* resuelve un canal del servidor en una fuente que se puede reproducir. No transcodifica (eso lo hace
`FfmpegSession`) ni sirve nada a la tele (eso lo hace la sesión). La tele nunca sabe qué provider hay detrás.

| Provider | `channels.json` | Qué resuelve | Estado |
|---|---|---|---|
| `local` | `video`, `audio` | Archivos ya en el formato de la tele, con `.idx` para "en emisión" | V0.2a |
| `hls` | `source`, `audio_language`, `video_quality` | Un HLS en directo cuya URL está en la configuración (nunca en la API) | V0.2b |
| `3cat` | `provider_channel`, `regions`, `video_quality` | Un canal en directo de 3Cat, desde su API oficial | V0.2b |
| `rtve` | `provider_channel` (`24h`) | Un canal en directo de RTVE, solo si RTVE lo ofrece sin DRM y sin iniciar sesión | V0.2d |
| `pluto` | `provider_channel` (slug, p. ej. `one-piece-es`), `crop_4_3` | Un canal de Pluto TV, por el mismo camino que su web pública sin cuenta | V0.2d |
| `tunarr` | — | — | No implementado. Sale offline |

Reglas comunes a todos los providers:
- **Solo fuentes oficiales y públicas.** Sin DRM: un stream cifrado (`EXT-X-KEY` o `EXT-X-SESSION-KEY` con método
  distinto de `NONE`) sale como `encrypted (DRM): unsupported`.
- **Nada de saltarse restricciones:** ni regiones, ni tokens ajenos, ni listas IPTV de terceros.
- **Sin sustituciones:** un canal que no se puede resolver queda offline con su motivo y la tele muestra NO SIGNAL.
  Nunca se cambia por otro.
- **Ninguna URL entra por la API:** las fuentes salen de `config/channels.json` o de la API oficial del provider.
  No hay proxy abierto ni riesgo de SSRF.

## HLS genérico

- **Variante:** la más pequeña de al menos 240 líneas (la tele muestra 320×240); nunca una variante solo de audio.
- **Audio:** la pista en `audio_language` si existe; si no, la predeterminada.
- **Latencia de la fuente:** se calcula con `EXT-X-PROGRAM-DATE-TIME` cuando el valor es creíble (0–10 min).
- **Salud:** se comprueba en red como mucho cada 30 s.

Canal de prueba **HLS TEST**: la demo en directo de Unified Streaming,
`https://demo.unified-streaming.com/k8s/live/stable/scte35.isml/.m3u8`. Es pública, de un fabricante de software de
streaming y pensada para pruebas: 720p con audio en inglés en pistas separadas.

El stream de prueba de Akamai (`cph-p2p-msl.akamaized.net/.../test/master.m3u8`) se descartó el 2026-09-29: su
master anuncia una variante que responde 404.

## 3Cat

### Cómo se resuelve

La web oficial (`https://www.3cat.cat/3cat/directes/`) declara en su configuración la API de medios
`api-media.3cat.cat`. Es la que usa su reproductor para cada directo:

```
https://api-media.3cat.cat/pvideo/media.jsp?media=video&versio=vast&idint=<código>&profile=pc_3cat&broadcast=false&format=dm
```

La respuesta trae:
- `informacio.arafem` y `informacio.despresfem`: el programa en emisión y el siguiente, con hora de inicio y fin
  (`data_emissio`, `data_caducitat`). El primero sale en `/api/status`; los dos, en `/api/guide` (el teletexto). Los
  canales 24h (`fc1`–`fc4`) no traen títulos: el teletexto los muestra "EN DIRECTE";
- `media`: una lista de `{geo, format, url}` con manifiestos HLS por zona.

El provider hace esto en cada sintonía, sin guardar nunca el manifiesto:
1. Pregunta a la API.
2. Elige la primera zona ofrecida en el orden `CATALUNYA`, `ESPANYA`, `TOTS`. `TOTS` es el feed internacional
   oficial; 3/24 solo ofrece ese.
3. Acepta solo `https` en hosts propios (`*.3catdirectes.cat`, `*.3cat.cat`, `*.ccma.cat`).
4. Aplica la resolución HLS genérica: variante de 480p y audio `ca`.

Lo observado el 2026-09-29/30, desde una conexión en Cataluña:

| Aspecto | Observación |
|---|---|
| Hosts | `directes-tv-{cat,es,int}.3catdirectes.cat` y `directes3-tv-{cat,es}.3catdirectes.cat` |
| Tokens | Ninguno en las URLs, que fueron las mismas en ambos días. Aun así se piden en cada sintonía y nunca se guardan |
| DRM | Ninguno en los canales lineales: sin `EXT-X-KEY` |
| Formato | MPEG-TS de 6 s; 480p, 720p y 1080p a 25 fps (H.264); ventana de 2 h |
| Audio | Pistas separadas: `ca` (predeterminada) y `qaa` (versión original); subtítulos WebVTT en catalán |
| Hora | `EXT-X-PROGRAM-DATE-TIME` va en UTC pero etiquetada `+02:00`. El provider lo tiene en cuenta |
| Zonas | Los segmentos se llaman `geo-br1_…`: los reparte según la zona. Fuera de las zonas ofrecidas, el canal saldría offline |

### Canales investigados

La columna "Probado" dice hasta dónde se ha comprobado cada canal:
- "API" = la API oficial ofrece un manifiesto;
- "Mac" = una sesión real del servidor dio vídeo MJPEG y audio ADTS;
- "ESP32" = se vio en la placa.

| Canal | Página oficial | Código | Tipo | Host del manifiesto | DRM | Token | Zonas | Probado | Estado |
|---|---|---|---|---|---|---|---|---|---|
| **SX3** | [3cat.cat/3cat/directes/sx3](https://www.3cat.cat/3cat/directes/sx3/) | `sx3` | HLS en directo | directes-tv-cat/-es | No | No | CAT, ES | API, Mac, **ESP32** | **Funciona** (canal 10 del ejemplo) |
| TV3 | [3cat.cat/3cat/directes/tv3](https://www.3cat.cat/3cat/directes/tv3/) | `tv3` | HLS en directo | directes3-tv-cat (`tv3-hls`), directes3-tv-es (`tvc-hls`) | No | No | CAT, ES | API, Mac | Funciona en el servidor; en la tele, sin probar |
| 33 | [3cat.cat/3cat/directes/c33](https://www.3cat.cat/3cat/directes/c33/) | `c33` | HLS en directo (`c33-super3-hls`) | directes-tv-cat/-es/-int | No | No | CAT, ES, TOTS | API, Mac | Funciona en el servidor; en la tele, sin probar |
| 3/24 | [3cat.cat/3cat/directes/324](https://www.3cat.cat/3cat/directes/324/) | `324` | HLS en directo | directes-tv-int | No | No | TOTS | API, Mac | Funciona en el servidor por la zona `TOTS` (el feed internacional oficial) |
| Esport3 | 3cat.cat/3cat/directes | `esport3` | HLS en directo | directes-tv-cat/-es | No | No | CAT, ES | API, Mac | Funciona en el servidor (no estaba en la lista, apareció en la página) |
| 3Cat Anime | 3cat.cat/3cat/directes | `TEM2` | HLS en directo (temático) | directes-tv-es | No | No | ES | API, Mac | Funciona en el servidor. El audio no lleva etiqueta de idioma: se usa la única pista |
| 3Cat Verdi Clàssics | 3cat.cat/3cat/directes/tem1 | `tem1` | HLS en directo (temático, cine) | directes-tv-es | No | No | ES | API | Resuelve; no se transcodificó |
| **3Cat Doraemon** ("Doraemon 24h") | [3cat.cat/3cat/directes/fc3](https://www.3cat.cat/3cat/directes/fc3/) | `fc3` | Canal FAST 24 h | fast-tailor.3catdirectes.cat | No | No | TOTS | API, Mac | **Funciona** en el servidor: sesión en 1,9–2,3 s, audio **catalán**, 360p (1080p/720p/360p) |
| 3Cat Plats bruts | …/directes/fc1 | `fc1` | Canal FAST 24 h | fast-tailor | No | No | TOTS | API, Mac | Funciona en el servidor (2,2 s) |
| 3Cat Joc de cartes | …/directes/fc2 | `fc2` | Canal FAST 24 h | fast-tailor | No | No | TOTS | API, Mac | Funciona en el servidor (2,6 s) |
| 3Cat Vinagre(ta) | …/directes/fc4 | `fc4` | Canal FAST 24 h | fast-tailor | No | No | TOTS | API, Mac | Funciona en el servidor (2,6 s) |
| 3Cat: càmeres del temps | …/directes/beauties | `beauties` | Cámaras del tiempo | directes-tv-int | No | No | TOTS | API | Resuelve; no se transcodificó |

Sobre los canales FAST (Doraemon incluido), corregido el 2026-09-30:
- Con su **código de página** (`fc1`–`fc4`, `beauties`) la API oficial sí da el stream, en un host propio de 3Cat
  (`fast-tailor.3catdirectes.cat`, con la publicidad insertada en el propio stream). Es lo que pide el reproductor
  de la web oficial para la página `/3cat/directes/fc3/`.
- Con el código interno del canal (`PUCFC3`…) la API no devuelve ningún stream. Por eso, en la primera
  investigación, parecían no disponibles.
- Sin DRM ni login. La zona es `TOTS` (el feed para todo el mundo), que ya está en el orden por defecto.

Estabilidad:
- **URLs:** las de los canales lineales son estables, pero no se escriben en `channels.json` porque 3Cat las puede
  cambiar. Se piden a la API en cada sintonía.
- **Si la API o el manifiesto fallan:** canal offline con el motivo (`3Cat media API unavailable: …`,
  `manifest unavailable: HTTP 404`…).

### Limitaciones

- **Zonas:** solo se reproduce lo que 3Cat sirve a esta conexión. Fuera de Cataluña o de España pueden cambiar las
  zonas ofrecidas y algunos canales quedarían offline.
- **Latencia:** el directo va ~9 s por detrás del segmento más nuevo (la fuente), más ~2–3 s de colchón en el
  servidor y la tele. Además está el retraso propio del HLS respecto a la emisión en TDT, que no se ha medido.
- **Caudal:** a `q:v 12`, SX3 ha pesado de ~75 a ~155 KB/s según el programa. Eso está cerca de lo que una conexión
  TCP lleva a la tele. Detalle en TEST_PLAN, T12.14.
- **Versión original:** siempre se toma el catalán (`ca`). Otra pista requeriría un campo por canal; hoy solo lo
  tiene el provider `hls` (`audio_language`).
- **Pruebas:** no dependen de Internet; la API y los manifiestos se simulan (`tests/test_providers.py`). Los
  canales reales se comprueban a mano.

## RTVE

### Cómo se resuelve

El provider hace esto en cada sintonía, sin guardar nada:
1. **Pide la página oficial del directo** (`https://www.rtve.es/play/videos/directo/canales-lineales/<canal>/`).
   Allí el reproductor de RTVE lleva, para cada stream, su configuración (`data-setup`), con dos datos:
   - `hasDRM`: si RTVE lo protege con DRM;
   - `requireLogged`: si pide iniciar sesión.
2. **Comprueba esos dos datos.** Con cualquiera de los dos, el canal sale **offline con el motivo**. No se intenta
   rodear ni el DRM ni el login.
   - Si la página ya no describe el stream (cambio de diseño), también offline. Falla del lado seguro.
   - Si falta el dato de login, se trata como si lo pidiera.
3. **Pide el stream al localizador oficial**, `https://ztnr.rtve.es/ztnr/<asset>.m3u8`. Responde con una redirección
   al manifiesto en el CDN de RTVE (`rtvelivestream.rtve.es`). La redirección se lee sin seguirla y solo se acepta
   `https` en hosts `*.rtve.es`.
4. **Aplica la resolución HLS genérica:** la variante más pequeña de al menos 240 líneas (360p) y el audio en
   castellano.

### Canales

Comprobado el 2026-09-30, desde una conexión en España:

| Canal | `provider_channel` | Asset | DRM | Login | Estado |
|---|---|---|---|---|---|
| **24h** | `24h` | 1694255 | No | No (en su página) | **Funciona** en el servidor: sesión lista en 1,5 s. Segmentos de 5 s, 360p, audio castellano; también `qaa` (original) y subtítulos. Con `20fps-q14`: ~1,24 Mbps (p95 1,41), llegada regular |
| Clan | `clan` | 5466990 | No | **Sí** | Offline: `RTVE requires signing in for this channel: unsupported` |
| La 1 | `la1` | 1688877 | **Sí** | Sí | Offline: `RTVE protects this channel with DRM: unsupported` |
| La 2 | `la2` | 1688885 | **Sí** | Sí | Offline (DRM) |
| Teledeporte | `tdp` | 1712295 | **Sí** | Sí | Offline (DRM) |

Notas:
- **La API de vídeos se contradice:** `api.rtve.es/api/videos/1694255.json` marca 24h con `requireLogged: true`,
  mientras que su página de directo lo reproduce sin sesión. Para un directo manda lo que hace la página oficial,
  y es lo que se comprueba en cada sintonía.
- **Clan** es el canal infantil y el que más encajaría, pero RTVE pide iniciar sesión. Iniciar sesión con la
  cuenta del usuario sería legítimo, pero no está implementado.

## Pluto TV

### Excepción al cifrado (decisión del usuario, 2026-09-30)

**Qué hace Pluto:** cifra todos los segmentos de sus canales con **AES-128 de HLS** y da la clave a cualquier sesión
anónima, porque su reproductor web la necesita. No es un DRM como Widevine, pero sí una protección del contenido.

**La decisión:** con la regla general ("cifrado = no soportado"), Pluto quedaba fuera. El usuario decidió hacer una
**excepción solo para Pluto y solo para AES-128**, sabiendo que:
- choca con la regla de no extraer claves ni rodear protecciones;
- es una zona gris legal (art. 196 LPI) y va contra las condiciones de uso de Pluto;
- **si da problemas, se quita.**

**Cómo queda acotada en el código:**
- `resolve_hls(..., allow_aes128=True)` solo acepta el método `AES-128`. `SAMPLE-AES` o cualquier otro método se
  sigue rechazando.
- Solo `PlutoProvider` lo pide. El resto de providers rechaza también AES-128.
- **Para quitarla:** borrar `allow_aes128=True` en `app/providers/pluto.py`.

### Cómo se resuelve

Por el mismo camino que la web pública de Pluto TV, sin cuenta:
1. **Sesión anónima** (`boot.pluto.tv/v4/start`), con un identificador de cliente aleatorio por arranque del
   servidor.
   - Pluto responde con el país que detecta (`ES`), sus servidores, un token de sesión y los parámetros del
     *stitcher*, el servicio que monta el stream con los anuncios.
   - La sesión se reutiliza hasta que Pluto pide renovarla (`refreshInSec`: 8 h; el token dura 24 h).
2. **Guía de canales de ese país**, con el token. El canal se busca por su *slug* (`one-piece-es`). Si Pluto no lo
   ofrece aquí, sale offline.
3. **Manifiesto del stitcher**, con los anuncios que Pluto inserta: no se quitan.
4. Solo se aceptan hosts `https` bajo `pluto.tv`.
5. Las variantes no declaran códecs. Se asume audio mezclado con el vídeo y se toma la más ligera.

### Canales

Probados el 2026-09-30 en el servidor, 8 s de sesión real cada uno:

| Canal | Slug | Lista en | Imagen | Nota |
|---|---|---|---|---|
| Dragon Ball | `dragon-ball-es` | 4,0 s | 4:3 dentro de un cuadro 16:9 | Con `crop_4_3` llena la pantalla |
| One Piece | `one-piece-es` | 5,5 s | 16:9 | |
| Pluto TV Anime | `pluto-tv-anime-es` | 8,9 s | 16:9 | |
| Lupin | `lupin-es` | 6,0 s | 4:3 con bandas laterales | `crop_4_3` |
| Érase una vez… | `erase-una-vez-es` | 4,4 s | Sin bandas laterales | |

En total hay 139 canales en España, entre ellos Dragon Ball Z, Detective Conan, Heidi, Los Pitufos y Pluto TV Kids.
La lista y los *slugs* salen de la guía (`/v2/guide/channels`).

### `crop_4_3`

Opción por canal (cualquier provider en directo): recorta el 4:3 central de un cuadro 16:9 antes de escalar. Sin
ella, una serie 4:3 emitida con bandas laterales queda pequeña, con negro por los cuatro lados.

## Pendiente (no implementado)

- **Tunarr (V0.2e).**
- **Varias teles:** cada sesión lanza su propio FFmpeg. Dos teles en el mismo canal son dos procesos. Si llega un
  cliente de más por accidente, funciona, con el límite de 8 sesiones.
