# Controles, encendido y batería

Teclas, BOOT, táctil, apagado y encendido, batería, LED, menú de ajustes y palmadas. Volver al
[README](../README.md).

## Teclas, BOOT y táctil

Los eventos son los mismos venga de donde venga la orden. El táctil solo se activa si al arrancar responde en 0x38
(FNK0104B).

Las cuatro teclas de la carcasa, de izquierda a derecha (actúan al soltar, sin esperas). **Estado:** su lógica está
probada en el ordenador (T19.1), pero **todavía no con los pulsadores montados en la placa** (T19.2, T22.3). En la
placa se han probado BOOT, el mando web y el puerto serie, que dan las mismas órdenes.

| Tecla | Pulsar | Mantener 1 s |
|---|---|---|
| CH− (GPIO2) | Canal anterior | **Apagar** (mantener 2 s) |
| CH+ (GPIO3) | Canal siguiente | Ajustes |
| VOL− (GPIO14) | Volumen − | Silencio |
| VOL+ (GPIO21) | Volumen + | Volumen + |

Con la placa suelta, BOOT: clic = canal siguiente, doble clic = anterior, mantener = ajustes (probado en la placa).

| Táctil | Toque | ← | → | ↑ | ↓ | Mantener 0.8 s |
|---|---|---|---|---|---|---|
| Evento | OSD fijo sí/no | Canal anterior | Canal siguiente | Volumen + | Volumen − | Ajustes |

- **Encender y apagar** (con batería): mantener **CH− 2 s** (o ⏻ en el mando web) apaga la tele: la imagen se recoge
  en la línea del CRT, se apagan pantalla, sonido, Wi-Fi y LED, y el chip se duerme. **Cualquier tecla** (o BOOT) la
  vuelve a encender: arranque normal, con la intro y el último canal. Dormida no tiene Wi-Fi: el mando web no la
  enciende. La tecla con la que se enciende no cuenta como orden. Probado en la placa con el mando web, el puerto
  serie y BOOT; con las teclas montadas, pendiente.
- **Otros apagados:** con el firmware `voice`, AJUSTES → VOZ → APAGADO elige entre este reposo (AHORRO MAX) y
  STANDBY VOZ, que escucha palmadas. La carcasa sin botones (`voice_nokeys`) al apagarla nunca entra en el reposo
  profundo: apaga a STANDBY VOZ o, si no puede escuchar, a STANDBY WI-FI, de donde la saca el mando web
  ([HARDWARE.md](HARDWARE.md#integración-física-en-la-carcasa)).
- **Batería:** la barra del canal enseña una pila con el porcentaje junto a la Wi-Fi, y el mando web `BAT 78%`. Al
  bajar del 15 % sale "BATERIA / QUEDA POCA CARGA" y al 5 % "CONECTA EL USB-C" (una vez por nivel). Se carga por el
  USB-C de la placa (TP4054, ~300 mA nominales: unas 10–11 h estimadas de vacía a llena); enchufada, la tele funciona
  del cable y la batería solo carga. Sin batería conectada la lectura es la salida del cargador y enseña ~90 %. El
  porcentaje sale de una curva genérica de LiPo, no de una descarga medida de esta celda.
- **Cargando:** al enchufar el USB-C sale "CARGANDO / BATERIA 78%" y, mientras carga, la pila de la barra se ve verde
  con un rayo amarillo; el mando web pone `CARGANDO 78% ⚡`. La placa no tiene ningún pin que diga si hay cable: la tele
  lo deduce del salto de ~100 mV que da la lectura al enchufar o desenchufar y, si se encendió ya enchufada, de que la
  tensión suba durante unos minutos. Mientras carga, el porcentaje marca algo de más (es la tensión de carga).
- **Medido** (2026-10-02, por el USB del Mac y con la tele encendida): del 81 % al 99 % en unas 2 h 45 min. La última
  hora la tensión apenas sube (el cargador mantiene la tensión y baja la corriente) y se queda en ~4,19 V leídos: ahí ya
  está llena. Si la tele se reinicia enchufada con la batería casi llena, puede no marcar CARGANDO, porque la tensión ya
  no sube.
- **Autonomía (estimada, sin medidor):** viendo la tele, unas 8–10 h suponiendo ~0,3 A, sin medir todavía
  (`tools/battery_log.py` registra una descarga completa). En STANDBY VOZ, unos 40 mA (30–50), deducidos de una noche
  en standby: unos 3 días desde llena.
- **Batería agotada:** si se queda en el 2 % (~3,42 V) durante unos 30 s, sale "BATERIA / AGOTADA: SE APAGA" y la
  tele pasa a reposo, para no vaciar la celda. Al encenderla con la batería aún agotada se vuelve a apagar; con el
  USB-C enchufado, no.
- **Conector:** JST 1.25 de 2 pines, pin 1 = BAT+ y pin 2 = GND. Comprueba la polaridad de la batería antes de
  enchufarla: en las baratas el rojo y el negro a veces vienen al revés.
- **LED del frontal** (GPIO43, cableado en [HARDWARE.md](HARDWARE.md#integración-física-en-la-carcasa)): encendido
  mientras la tele funciona y apagado un instante con cada orden, como el piloto de una tele de los 90. Destella con
  cada palmada en STANDBY VOZ, se enciende al instante al despertar, parpadea a 2 Hz mientras graba, da un latido cada
  4 s en STANDBY WI-FI y un doble destello cada 10 s con la batería baja en standby. Probado en la placa (T19.3,
  T19.7), salvo el aviso de batería baja, que necesita una descarga real.
- **Teletexto:** en ese canal no hay sonido, así que VOLUMEN + / − (o un toque) pasa de página.
- **Ajustes:** CH−/CH+ mueven la selección, VOL+ elige o sube, VOL− baja, y mantener CH+ sale. Las opciones son WI-FI
  (estado y reintentar), BRILLO (10–100 %), VOLUMEN, DIAGNOSTICO y REINICIAR; con el firmware `voice`, también VOZ
  (micrófono, palmadas, sensibilidad, APAGADO, LED ESCUCHA y GRABAR MENSAJE; con `voice_ww`, también HOLA ESP y HEY
  RETRO).

## Palmadas y mensajes

Con el firmware `voice` (`pio run -e voice -t upload`; en la carcasa sin botones, `pio run -e voice_nokeys -t upload`)
la tele usa su micrófono. Todo pasa dentro de la tele: sin Internet y sin servicios de reconocimiento.

<img src="img/ajustes-voz.jpg" width="250" align="right" alt="Apartado VOZ de los ajustes del mando web: micrófono, palmadas, escuchar palmadas al apagar, piloto al oír una palmada y sensibilidad 80">

- **Apagar y encender con tres palmadas.** Con un canal puesto, tres palmadas seguidas (plas-plas-plas, a menos de
  0,7 s entre ellas) la apagan con el efecto del tubo y la dejan en STANDBY VOZ, escuchando. Tras un momento de
  silencio, otras tres la encienden. El piloto destella con cada palmada que oye. Dos palmadas no hacen nada: es lo
  que más se cuela.
- **No es infalible.** Funciona con palmadas firmes a 0,5–1 m. Algún ruido de casa puede colarse, y una palmada floja
  que coincide con un golpe del programa no cuenta. La sensibilidad (80 por defecto) se cambia en AJUSTES → VOZ, en la
  tele o en el mando.
- **Consumo.** STANDBY VOZ gasta unos 40 mA (estimado de una noche, sin medidor): unos 3 días desde llena. Con la
  batería agotada, duerme del todo.
- **Mensajes.** AJUSTES → VOZ → GRABAR MENSAJE: cuenta atrás, ● REC hasta 15 s, y se guarda como WAV en la microSD,
  subido al nivel de la voz para que se oiga bien. El canal **MENSAJES** se añade solo a la lista y los pone en bucle.
- **Privacidad.** Las palmadas no graban nada: solo miden niveles. La grabadora solo graba cuando la pones en marcha
  desde la tele, con ● REC en pantalla; el mando web no puede grabar.
- **Palabras de activación (experimental):** «Hola ESP» y «Hey Retro» encienden desde STANDBY VOZ con los firmwares
  `voice_ww` y `voice_nokeys_ww` más una app de standby aparte. Se activan y desactivan en AJUSTES → VOZ (HOLA ESP,
  HEY RETRO). El modelo de «Hey Retro» se entrena aparte y no viene en el repositorio: [guía del wake word](WAKEWORD.md).
- **Lo que no hay:** comandos de voz.

Detalle, medidas y pruebas: [RETROTV Voice](VOICE.md).
<br clear="right">
