# Construir RETROTV

Esta guía resume la parte física del proyecto. Para pinout, medidas eléctricas y decisiones verificadas, la fuente de verdad sigue siendo [HARDWARE.md](HARDWARE.md).

> [!IMPORTANT]
> RETROTV sigue en **alpha**. Revisa también [TEST_PLAN.md](TEST_PLAN.md): allí se distingue explícitamente lo probado en hardware de lo que todavía necesita validación física.

## Lista de materiales

| Pieza | Especificación usada en el proyecto | Cantidad | Notas |
|---|---|---:|---|
| Placa | Freenove ESP32-S3 Display 2.8" FNK0104A o FNK0104B | 1 | ESP32-S3R8, pantalla ILI9341 320×240, ES8311, microSD y cargador LiPo |
| microSD | FAT32, esquema MBR | 1 | El firmware no lee exFAT con el core actual |
| Altavoz | 40×28 mm | 1 | El módulo admite hasta 1.5 W a 8 Ω o 2 W a 4 Ω |
| LiPo | 103665, 3.7 V, 3000 mAh, JST 1.25 | 1 | Probada en la unidad del proyecto |
| Pulsador | táctil 6×6 mm | 4 | CH−, CH+, VOL−, VOL+ |
| LED | rojo, 3 mm | 1 | Ánodo a GPIO43 mediante ~1 kΩ; cátodo a GND |
| Resistencia | ~1 kΩ | 1 | Serie con el LED frontal |
| Cableado | fino, flexible | — | Los pulsadores van de GPIO a GND |
| Carcasa | tele90 v9 impresa en PLA o PETG | 1 set | Diseño propio; los archivos 3D todavía no forman parte de este repositorio |
| Patas | TPU | 4 | Parte de la carcasa tele90 |

### GPIO de los cuatro mandos

De izquierda a derecha:

| Control | GPIO |
|---|---:|
| CH− | 2 |
| CH+ | 3 |
| VOL− | 14 |
| VOL+ | 21 |

Cada pulsador conecta su GPIO a **GND** y usa el pull-up interno.

El conector de expansión de 1.25 mm de la placa expone esos cuatro GPIO, pero **no lleva GND ni 3V3**. Toma GND del conector I2C o UART, como se documenta en [HARDWARE.md](HARDWARE.md).

## Carcasa tele90 v9

La unidad documentada usa la carcasa **tele90 v9**:

- 92,5 × 85 × 59 mm;
- frontal, cuerpo y tapa trasera;
- cierre mediante 12 imanes, sin tornillos;
- placa sobre pivotes;
- cuatro teclas frontales;
- LED rojo frontal;
- USB-C accesible desde el lateral;
- altavoz bajo la rejilla superior;
- LiPo detrás de la placa;
- patas de TPU.

Impresión aproximada indicada en el proyecto: unas **7 h y ~100 g**. Solo el cuerpo necesita soportes, por dentro.

Los archivos 3D no se publican todavía en este repositorio. Cuando estén disponibles, esta página debe ser el punto de entrada para STL/3MF/STEP y parámetros de impresión.

## Cableado mínimo

```text
CH−  GPIO2  ── pulsador ── GND
CH+  GPIO3  ── pulsador ── GND
VOL− GPIO14 ── pulsador ── GND
VOL+ GPIO21 ── pulsador ── GND

GPIO43 ── ~1 kΩ ── ánodo LED rojo
GND    ──────────── cátodo LED rojo
```

No mantengas **BOOT** pulsado al conectar alimentación: la placa puede entrar en modo descarga.

## Montaje recomendado

1. **Prueba la placa fuera de la carcasa.** Flashea el firmware y confirma LCD, audio, PSRAM, SD y Wi-Fi.
2. **Prepara la microSD.** FAT32/MBR y arranca una vez para crear la estructura `/retrotv`.
3. **Prueba un vídeo local.** Convierte un clip con `tools/convert_video.sh` y comprueba reproducción y audio.
4. **Cablea las cuatro teclas.** Verifica cada evento antes de montar el frontal.
5. **Cablea el LED frontal.** GPIO43 con resistencia serie de ~1 kΩ.
6. **Conecta altavoz y LiPo.** Comprueba polaridad antes de cerrar.
7. **Monta la placa y el altavoz.**
8. **Comprueba USB-C y microSD con la carcasa cerrada.**
9. **Repite las pruebas de SD/Wi-Fi dentro de la carcasa.** Los alargadores y el montaje físico pueden cambiar el margen eléctrico.
10. **Haz una prueba larga.** Usa el soak test descrito en [TEST_PLAN.md](TEST_PLAN.md) antes de considerar la unidad terminada.

## Antes de cerrar la carcasa

- [ ] Arranque sin reinicios
- [ ] LCD horizontal y colores correctos
- [ ] Altavoz limpio
- [ ] microSD montada de forma estable
- [ ] CH− / CH+ / VOL− / VOL+ responden
- [ ] LED frontal responde
- [ ] Wi-Fi conecta con la carcasa montada
- [ ] Vídeo local estable a 20 fps
- [ ] Carga de batería correcta
- [ ] USB-C sigue accesible
- [ ] Ningún cable queda pinzado

## Seguridad y límites

- La LiPo debe estar protegida y no debe quedar comprimida ni perforada.
- No cortocircuites los pines de la batería.
- El servidor de red está pensado para la LAN doméstica; no expongas su puerto directamente a Internet.
- Las contraseñas Wi-Fi de `wifi.json` quedan en texto plano en la tarjeta.
