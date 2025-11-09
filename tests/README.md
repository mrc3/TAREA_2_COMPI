Tests para el parser

Este directorio `tests/` contiene dos entradas y un script para ejecutar
el parser y guardar los resultados.

Archivos:
- `test_ok.json` : JSON válido (se espera `OK: fuente sintacticamente correcto.`)
- `test_error.json` : JSON con errores sintácticos para comprobar los mensajes de error.
- `run_tests.sh` : script que ejecuta el parser contra ambos tests y escribe `results.txt`.

Cómo usar:
1. Desde `TAREA_2` compila el parser si aún no está compilado:

```bash
gcc -std=c11 -O2 -Wall src/parser.c -o parser
```

2. Ejecuta el script de tests:

```bash
./tests/run_tests.sh
```

3. Consulta `tests/results.txt` para ver la salida completa.

Si deseas desactivar la traza de tokens abre `src/parser.c` y pon `debug_tokens = 0`.
