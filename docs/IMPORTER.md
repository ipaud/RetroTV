# RetroTV Importar: vídeos a la tele sin terminal

Una app de Mac que va dentro de la propia microSD. Se mete la tarjeta en el Mac, se abre la app, se suelta una carpeta
de capítulos y sale un canal nuevo, convertido al formato de la tele. Sin instalar nada, sin terminal y sin editar
`channels.json`. Volver al [README](../README.md).

Por dentro hace lo mismo que [`tools/convert_video.sh`](../tools/convert_video.sh), con los mismos ajustes
([convertir capítulos](CHANNELS.md#convertir-capítulos)), y lleva su propio ffmpeg.

## Para quien usa la tele

**Si la tarjeta aún no tiene la app:** descárgala de las
[releases de RetroTV](https://github.com/ipaud/RetroTV/releases/latest) (`RetroTV-Importar-<versión>-mac.zip`),
doble clic en el zip y abre la app desde Descargas. La primera vez el Mac dice que no puede comprobar quién la ha
hecho (no está firmada por Apple): ve a **Ajustes del Sistema > Privacidad y seguridad**, baja hasta el aviso de
RetroTV Importar y pulsa **Abrir igualmente**. Solo hace falta una vez.

- Tarjeta nueva de 64 GB o más (exFAT): la app la encuentra sola y ofrece **Preparar la tarjeta**, que además la
  copia dentro.
- Tarjeta de 32 GB o menos (FAT32) que nunca ha estado en la tele: pulsa **La tarjeta ya está dentro…** y elígela.

Con la app ya en la tarjeta:

1. **Mete la tarjeta en el Mac** y ábrela en el Finder. Doble clic en **RetroTV Importar**.
2. Una tarjeta preparada por la app o por `importer/install.sh` la abre sin preguntar nada.
3. La ventana enseña los canales de la tarjeta como una página de teletexto, con los capítulos de cada uno.
4. **Para un canal nuevo:** suelta una carpeta de capítulos en el recuadro (o sobre el icono de la app, o con
   «Elegir carpeta…»). Escribe el nombre del canal; debajo se ve cómo lo escribirá la tele. Si los vídeos tienen
   varios idiomas, elige el audio. Pulsa **Convertir**.
5. **Para añadir capítulos a un canal que ya existe:** suéltalos encima de su fila en la lista.
6. Al terminar, **Expulsar la tarjeta y salir**. Mete la tarjeta en la tele: los canales nuevos salen al encenderla.

| | |
|---|---|
| Vídeos que acepta | mp4, mkv, avi, mov, m4v y webm: H.264, HEVC, MPEG-4/DivX, VP8, VP9, ProRes... Los AV1 se saltan (la app lo dice) |
| Tiempo | ~30 s por capítulo de 23 min en un Mac con Apple Silicon; más en uno con Intel |
| Espacio | ~370 MB por capítulo de 23 min (la app avisa si no cabe) |
| Números | Cada canal nuevo va detrás de la última serie: 1, 2, 3... en una tarjeta nueva |
| Si se para | **Detener**, cerrar la app o sacar la tarjeta no deja archivos a medias. Lo convertido se queda; si se vuelve a soltar la misma carpeta con el mismo nombre, sigue donde iba |

**Tarjeta nueva de más de 32 GB:** vienen en exFAT y la tele solo lee FAT32. La app lo detecta y ofrece **Preparar la
tarjeta**: la borra entera (pregunta antes, con su nombre y tamaño), la deja en FAT32 con el nombre `RETROTV`, crea
`/retrotv` con el teletexto (8) y la carta de ajuste (9), y vuelve a copiarse dentro. Si la app se abrió desde esa
misma tarjeta, antes se copia a una carpeta temporal y sigue desde allí. Solo borra discos extraíbles, de una partición
y que no estén protegidos contra escritura; nunca el del Mac.

## Para quien prepara la tele

Hace falta un Mac con Xcode o las Command Line Tools, y `nasm` para el ffmpeg de los Mac con Intel
(`brew install nasm`).

```sh
importer/install.sh /Volumes/RETROTV   # compila la app (y su ffmpeg la primera vez) y la copia en la tarjeta
importer/build.sh app                  # solo compila: importer/build/RetroTV Importar.app
```

**Publicar una versión:** sube `CFBundleShortVersionString` en `build.sh`, compila desde el commit publicado y crea
una release en RetroTV con el zip de la app (`ditto -c -k --sequesterRsrc --keepParent "importer/build/RetroTV Importar.app"
RetroTV-Importar-<versión>-mac.zip`) y `importer/build/ffmpeg-9.0.2.tar.xz`, el código de ffmpeg que pide su licencia
LGPL. Quien descargue el zip tendrá que permitir la app una vez. Copiada por `install.sh` o por la propia app, no lleva
la marca de descarga y se abre sin avisos.

**Qué lleva dentro** (39 MB, para Apple Silicon e Intel, macOS 12 o posterior):

- `Contents/MacOS/RetroTVImporter`: la app, en SwiftUI ([importer/Sources](../importer/Sources)).
- `Contents/MacOS/ffmpeg` y `ffprobe` 9.0.2, compilados por [build_ffmpeg.sh](../importer/build_ffmpeg.sh) solo con lo
  que hace falta, estáticos y en LGPL. En `Contents/Resources/ffmpeg/` van su licencia y `SOURCE.txt`, con el código
  fuente usado (verificado con la firma de FFmpeg) y las opciones de compilación.
- `Contents/Resources/convert_video.sh`: la misma herramienta del repo. La app la llama con `INDEXER` (sus índices
  `.idx`, en Swift, porque un Mac sin herramientas de desarrollo no tiene python3) y `PROGRESO=1` (la barra de
  progreso), y lee sus líneas `convert`, `done`, `skip` y `FAILED`.
- Firma ad hoc, no de Apple: de ahí el aviso del paso 2.

**Cómo escribe en la tarjeta:** añade el canal al final de `channels.json` sin tocar el resto del archivo, y solo
cuando ya hay un capítulo convertido. Si `channels.json` existe pero no se puede leer, no importa nada (nunca lo
sustituye). Las reglas de id y número están en
[ChannelsFile.swift](../importer/Sources/ChannelsFile.swift).

**Probarla sin ventana:** el binario de dentro de la app tiene un modo de línea de órdenes que hace lo mismo que la
ventana. Las pruebas de la lógica pura van en `tools/run_host_tests.sh`.

```sh
app="importer/build/RetroTV Importar.app/Contents/MacOS/RetroTVImporter"
"$app" --import /Volumes/RETROTV --new "Mi serie" ~/Videos/MiSerie        # canal nuevo
"$app" --import /Volumes/RETROTV --into /retrotv/media/mi_serie --track 1 ~/Videos/MiSerie   # añadir, 2.ª pista
"$app" --self-test
```

Una tarjeta de pruebas, sin tocar la de verdad:
`hdiutil create -size 600m -fs "MS-DOS FAT32" -volname RTVTEST -layout MBRSPUD card.dmg && hdiutil attach card.dmg`
(`-fs ExFAT` para probar «Preparar la tarjeta»).

**Límites:** solo Mac; sin AV1 (haría falta compilar dav1d); no firmada ni notarizada; los canales se quitan o se
reordenan a mano en `channels.json`.
