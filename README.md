# Tarea 2 – Analizador Sintáctico Descendente (LL(1)) – JSON simplificado

Implementación en C de un parser LL(1) con recuperación de errores (Panic Mode)
para el lenguaje JSON simplificado definido en la consigna.

## Cómo compilar y ejecutar
gcc -std=c11 -O2 -Wall src/parser.c -o parser
./parser tests/fuente.json

## Qué valida
- `json -> element EOF`
- `element -> object | array`
- `array -> '[' (element (',' element)*)? ']'`
- `object -> '{' (attribute (',' attribute)*)? '}'`
- `attribute -> STRING ':' attribute_value`
- `attribute_value -> element | STRING | NUMBER | TRUE | FALSE | NULL`

## Errores y recuperación
- Mensajes con línea estimada.
- Panic Mode: sincroniza con FIRST/FOLLOW para continuar el análisis.

## Integrantes
- Marcelo Caceres, [CI/5165343]  
- [Roberto Arce], [CI/legajo]
