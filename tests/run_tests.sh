#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
PARSER_DIR="$(cd "$DIR/.." && pwd)"
PARSER="$PARSER_DIR/parser"

if [ ! -x "$PARSER" ]; then
  echo "Parser no encontrado o no ejecutable en: $PARSER"
  echo "Compila primero: gcc -std=c11 -O2 -Wall src/parser.c -o parser"
  exit 2
fi

RESULTS="$DIR/results.txt"
: > "$RESULTS"

for f in test_ok.json test_error.json; do
  echo "==== $f ====" | tee -a "$RESULTS"
  echo "Ejecutando: $PARSER $DIR/$f" | tee -a "$RESULTS"
  "$PARSER" "$DIR/$f" 2>&1 | tee -a "$RESULTS"
  echo "exit:$?" | tee -a "$RESULTS"
  echo "" | tee -a "$RESULTS"
done

echo "Resultados guardados en: $RESULTS"
