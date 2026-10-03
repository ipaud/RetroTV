# Mando web

El mando del móvil que sirve la propia tele: diseños, guía, logos, ajustes, canal del mando y API HTTP.
Volver al [README](../README.md).

## Qué hace y cómo se abre

<p align="center">
  <img src="img/mandos.jpg" width="100%" alt="Los cuatro diseños del mando web con una parrilla de ejemplo: CLÁSICO, oscuro con pantalla verde; NEGRO, con teclas beige, pantalla naranja y un panel con volumen, INFO redondo y canal en naranja; PLATA, plateado con teclas de colores y un aro de cruceta; GRIS, con cabeza redonda que lleva volumen, encendido rosa y canal">
  <br><sub>De izquierda a derecha: CLÁSICO, NEGRO, PLATA y GRIS, con canales y logos inventados. Los tres primeros enseñan
  los logos en color; el GRIS, en una sola tinta.</sub>
</p>

<img src="img/sticker.png" width="300" align="right" alt="Pegatina trasera de RETROTV con el QR del mando y las instrucciones de encendido">

Con la tele en la Wi-Fi, abre **http://retrotv.local** en el móvil, conectado a la misma red. Si el móvil no resuelve
`.local`, usa la IP que sale en el log (`[WEB] remote at …`).

- **Qué hace:** CANAL ▲▼, VOLUMEN + −, SILENCIO, INFO (el OSD), ⏻ (apagar) y la lista de canales para saltar directamente a
  uno. La pantalla de arriba muestra el canal y el volumen actuales. Los canales remotos llevan un punto rojo.
- **Cómo funciona:** las órdenes entran por el mismo camino que los mandos físicos. Un salto directo pasa por la
  estática y el destello, como un zapeo.
- **Dónde vive:** la página la sirve la propia tele, sin nube y sin el Mac. Pesa unos 42 KB y funciona sin Internet.
- **Cuatro mandos (botón MANDO, o deslizando el dedo a un lado):** cada uno con la forma, los colores y la colocación
  de un mando de verdad:
  - CLÁSICO, el de siempre;
  - NEGRO, de televisor de los 90: teclas beige, canal en naranja, pantalla fluorescente y un panel hundido con
    volumen, un INFO redondo como un joystick y canal, encima de los canales; logos en color;
  - PLATA, de los 2000: plateado cepillado, teclas de colores, un gran aro que hace de cruceta (CH+ arriba, CH−
    abajo, VOL− y VOL+ a los lados, INFO en medio) y GUÍA en la píldora azul del MENU; logos en color;
  - GRIS, europeo de cabeza redonda: la cabeza, más ancha que el cuerpo, lleva volumen, canal, encendido y silencio;
    logos en blanco, de una sola tinta.
  Cada móvil recuerda el suyo, y `http://retrotv.local/?skin=negro` (o `plata`, `gris`) abre uno directamente.
<img src="img/guia.jpg" width="260" align="right" alt="La guía del mando web: cada canal con su logo, el capítulo de ahora con una barra de progreso y los minutos que quedan, y los siguientes con su hora">

- **Guía (botón GUÍA, o `http://retrotv.local/#guia`):** cada canal con su logo, lo que echa ahora con su barra y los
  minutos que quedan, y los tres siguientes con su hora; tocar uno lo sintoniza. Es la misma programación que el
  teletexto. La tele solo la prepara mientras alguien la mira: lee la de cada canal en segundo plano (unos 25 s la
  primera vez) sin frenar el vídeo ni el mando.
- **Seguridad:** no tiene PIN; cualquiera en la misma Wi-Fi puede cambiar de canal. Las órdenes exigen la cabecera
  `X-RETROTV`, así que otra web abierta en el móvil no puede mandarlas.

**Logos de los canales:** `/retrotv/logos/<id del canal>.png` en la SD (el `id` de `channels.json`).
- **Formato:** PNG de 360×160 px con fondo transparente, hasta 32 KB cada uno. Se ven a una cuarta parte, nítidos en
  pantallas retina. `tools/make_logo.py <archivo|url> <id> <carpeta>` los prepara: recorta, iguala el peso visual
  (una palabra fina ocupa más que un bloque macizo) y aclara los logos negros, que no se verían sobre las teclas oscuras.
  Para un logo blanco impreso sobre una caja negra, el modo `mono` deja solo las letras.
- **Tres versiones:** `<id>.png` en color (CLÁSICO, NEGRO y PLATA), `<id>.black.png` en negro (sin uso por ahora) y `<id>.white.png` en
  blanco. Un logo plano sale en silueta; uno con letras perfiladas, en tonos de una sola tinta (`--black` y `--white`
  eligen `solid` o `tonal`). Si falta una versión, la tele sirve la de color.
- **Carga:** la tele los lee a PSRAM al arrancar, así que nunca compiten con el vídeo por la SD. El móvil los guarda
  un día. Sin logo, el canal enseña su nombre.
- **Derechos:** los logos son marcas de sus dueños. Van solo en tu SD, **nunca en el repo**, igual que los capítulos.

**Ajustes** (botón AJUSTES al final del mando, o directamente `http://retrotv.local/#ajustes`):
- **Emparejar:** pulsa MOSTRAR CÓDIGO; la tele enseña un código de 4 cifras durante 60 s. Escríbelo en el móvil, que
  queda autorizado mientras lo uses (caduca a los 30 min sin usarlo). Con 5 códigos mal hay que pedir otro. Solo quien
  ve la tele puede tocar los ajustes. Hay un solo móvil emparejado a la vez: emparejar otro desconecta el anterior.
- **Wi-Fi:** las redes guardadas (sin contraseña), BUSCAR REDES para las de 2,4 GHz al alcance y AÑADIR RED, que la
  guarda en `wifi.json` de la SD. Las redes de `secrets.h` van en el firmware y no se pueden borrar desde aquí.
- **Pantalla y sonido:** brillo y volumen, guardados como con los mandos.
- **Voz** (solo con el firmware `voice`): micrófono, palmadas, sensibilidad, escuchar palmadas al apagar (STANDBY VOZ)
  y el destello del piloto al oír una palmada; los mismos ajustes que AJUSTES → VOZ en la tele. La grabadora no está
  aquí a propósito: el mando no tiene PIN y cualquiera en la Wi-Fi podría grabar la sala
  ([palmadas y mensajes](CONTROLS.md#palmadas-y-mensajes)). Con `voice_ww` y `voice_nokeys_ww`, también «Hola ESP» y
  «Hey Retro» (experimental).
- **Canales:** activa o desactiva cada canal. Se guarda en `channels.json` de la SD; el zapeo y el mando se saltan los
  desactivados.
- **Información:** versión, Wi-Fi, IP, SD, memoria y REINICIAR LA TELE.
- **Cómo se guarda:** la tele escribe `wifi.json` y `channels.json` en una copia `.tmp` y la pone en su sitio al
  acabar. Un corte de luz nunca deja un archivo a medias.

**Canal del mando:** un canal `internal` con `"source": "mando"` enseña un QR grande con la dirección de la tele
(`http://<ip>/`) y la dirección escrita debajo. Se escanea con la cámara del móvil y abre el mando.
- **Por qué la IP y no `retrotv.local`:** algunos Android no resuelven nombres `.local`. En iPhone, Safari sí; Chrome
  solo con el permiso de red local (Ajustes → Privacidad y seguridad → Red local).
- **Lista de canales:** con la página abierta se actualiza sola cuando cambia (tele reiniciada con otra
  `channels.json`, un canal activado o desactivado): `/api/state` lleva `list`, que cambia con ella.
- **Cambios de red:** si la IP cambia, el QR se redibuja solo. Sin Wi-Fi enseña `SIN WI-FI`.
- **Pegatina:** `tools/make_remote_sticker.py <carpeta>` hace una pegatina de 36 × 46 mm con el QR de
  `http://retrotv.local` (PDF suelto y hoja A4 de 20). Imprime al 100 %.

<details>
<summary><b>API HTTP de la tele</b></summary>

- `GET /api/state`: canal, nombre, volumen, silencio, pantalla, batería (`battery` en %, `battery_mv`, `charging`) y `list`;
- `GET /api/channels`: la lista de canales activos (`logo: true` si tiene logo);
- `GET /api/logo?n=<número>[&c=black|white]`: el PNG del logo, en color o en una tinta (si no hay esa versión, en color);
- `GET /api/guide`: la guía (`{"pending":true}` mientras se prepara; horas en segundos del reloj de la tele);
- `POST /api/key?k=next|prev|volup|voldown|mute|info|power`;
- `POST /api/channel?n=<número>`.

Los `POST` necesitan la cabecera `X-RETROTV: 1`. Los ajustes van en `/api/config/…`:
- se empareja con `POST /api/config/pair/start` y `POST /api/config/pair {"code"}`, que devuelve un `token`;
- el resto necesita la cabecera `X-RETROTV-Token`: `info`, `wifi`, `display`, `channels`, `voice` (`GET`, y `POST`
  con `{"mic", "claps", "sensitivity", "standby": "voice"|"deep", "led"}`, más `"hola_esp"` y `"hey_retro"` en
  `voice_ww` y `voice_nokeys_ww`; sin voz responde `{"available": false}`)
  y `reboot`.

</details>
