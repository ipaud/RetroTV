# Contribuir a RETROTV

Gracias por querer mejorar RETROTV. El proyecto junta el firmware del ESP32-S3, una app de standby, herramientas de
conversión y un servidor en Python. Estas reglas sirven para que todo siga siendo reproducible.

## Antes de abrir un pull request

Ejecuta lo que toque tu cambio. Son los mismos comandos que repite la integración continua en cada pull request
([qué comprueba cada job](docs/DEVELOPMENT.md#integración-continua)):

```sh
tools/run_host_tests.sh                      # tests de la lógica pura (en Linux: CXX=clang++ tools/run_host_tests.sh)
pio run -e pautv                             # y los demás entornos de platformio.ini que afecte tu cambio
pio run -d standby                           # si tocas standby/ (compila sin el modelo de «Hey Retro»)
cd server && python -m pip install -r requirements-dev.txt && python -m pytest   # si tocas server/, mejor en un entorno virtual
```

En Linux, `g++` 13 detiene los tests de host por dos avisos `format-truncation` de los propios tests; usa clang, como
la CI. Los entornos y cómo se graba cada uno: [desarrollo](docs/DEVELOPMENT.md).

## Lo que se prueba en la placa

- Di qué has comprobado físicamente y qué no. Nada se da por probado en la placa si solo se ha verificado en el
  ordenador: en el [plan de pruebas](docs/TEST_PLAN.md), lo pendiente se marca como REQUIRES HARDWARE TEST.
- Cuando una decisión dependa del hardware, apunta la placa y su variante, la configuración, cómo se midió, el
  resultado y la fecha.
- Las estimaciones (batería, consumo) se escriben como estimaciones, nunca como medidas.

## Lo que no se sube

- Series, películas, logos de canales ni capturas de contenido de terceros.
- Contraseñas o redes Wi-Fi: `include/secrets.h` y `wifi.json` no van al repositorio.
- `include/title_tags.h`, con los nombres propios de una colección.
- Modelos entrenados con datos que no permiten redistribuirlos, como `standby/models/heyretro.tflite`.
- Los archivos de la carcasa, que todavía no se publican.

## Estilo

- El código propio compila con `-Wall -Wextra`, y los tests de host además con `-Werror`.
- La lógica que se pueda probar en el ordenador no incluye `Arduino.h` y lleva sus tests en `test/`
  ([principios de la arquitectura](docs/ARCHITECTURE.md#principios)).
- La documentación va en castellano; el código, sus comentarios y los mensajes de commit, en inglés.

## Licencia

Lo que aportes se publica con la licencia MIT del proyecto ([LICENSE](LICENSE)), salvo en los archivos que ya llevan
otra, como `standby/src/MicroWakeWord.*` (GPL-3.0).
