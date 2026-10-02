# Contribuir a RETROTV

Gracias por querer mejorar RETROTV.

El proyecto mezcla firmware ESP32-S3, herramientas de conversión y un servidor Python. Para mantenerlo reproducible, una contribución debería intentar conservar estas reglas:

## Antes de abrir un PR

```sh
platformio run
tools/run_host_tests.sh
cd server
python -m pip install -r requirements-dev.txt
pytest
```

Los cambios dependientes de la placa deben indicar claramente qué se ha comprobado físicamente. No marques una prueba como válida si solo se ha verificado en host.

## Alcance

Son especialmente útiles:

- correcciones reproducibles;
- mejoras de estabilidad o rendimiento medidas;
- tests;
- compatibilidad con hardware documentado;
- documentación de montaje;
- mejoras del servidor doméstico;
- herramientas de conversión.

No incluyas en el repositorio series, películas, logos, claves, contraseñas o contenido protegido que no tengas derecho a redistribuir.

## Estilo

El código propio de C++ se mantiene con `-Wall -Wextra` y los tests host con `-Werror`.

Cuando una decisión dependa de hardware, documenta:

1. placa/variante;
2. configuración;
3. cómo se midió;
4. resultado;
5. fecha si es relevante.

Para detalles de arquitectura, hardware y pruebas consulta `docs/ARCHITECTURE.md`, `docs/HARDWARE.md` y `docs/TEST_PLAN.md`.
