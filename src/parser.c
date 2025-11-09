/*
    parser.c - Analizador sintáctico descendente LL(1) para un JSON simplificado

    Resumen
    -------
    Este archivo implementa un parser LL(1) recursivo con recuperación por
    "panic mode" para una versión simplificada de JSON. El programa realiza
    un escaneo sencillo (lexer embebido) y comprueba la estructura sintáctica
    del documento de entrada sin construir un árbol sintáctico ni validar
    semántica (por ejemplo, no se valida el formato exacto de números más allá
    de una lectura básica).

    Compilación y uso
    -----------------
    Compilar:  gcc -std=c11 -O2 -Wall src/parser.c -o parser
    Ejecutar:  ./parser archivo.json

    Salida / códigos de retorno
    ---------------------------
    - 0 : entrada sintácticamente correcta (imprime "OK: fuente sintacticamente correcto.")
    - 1 : se encontraron errores sintácticos (imprime número de errores)
    - 2 : error de uso o fallo al abrir el archivo

    Tokens reconocidos
    ------------------
    - T_LCOR  : '['
    - T_RCOR  : ']'
    - T_LLLA  : '{'
    - T_RLLA  : '}'
    - T_COMA  : ','
    - T_DOSP  : ':'
    - T_STRING: cadena entre comillas dobles, con escapes simples soportados por el scanner
    - T_NUMBER: secuencia que contiene dígitos, punto decimal y/o exponente (lectura liberal)
    - T_TRUE  : true (case-insensitive)
    - T_FALSE : false (case-insensitive)
    - T_NULL  : null (case-insensitive)
    - T_EOF   : fin de fichero
    - T_ERROR : token no reconocido o error de lectura

    Lexer (scanner) integrado
    -------------------------
    - Implementado de forma mínima y suficiente para la tarea.
    - Omite espacios en blanco y soporta comentarios de línea ("// ...").
    - Strings: reconoce escapes simples (si un '\' aparece copia el siguiente
        carácter sin interpretación adicional).
    - Números: lectura tolerante, acepta dígitos, '.', 'e', 'E', '+' y '-' en
        cualquier posición tras el inicio; no se valida estrictamente el formato.
    - Literales true/false/null acepan mayúsculas/minúsculas (comparación case-insensitive).
    - Se usan variables globales simples para el flujo del archivo y posición
        (fin, line, col, lastc, yytext).

    Estructura del parser
    ---------------------
    - Se implementan funciones recursivas para los no terminales:
        json, element, array, element_list, object, attributes_list,
        attribute, attribute_value.
    - match(Token t): consume el token esperado o reporta un error sintáctico.
    - advance(): pide el siguiente token al lexer.
    - report_err(): imprime un mensaje con la línea aproximada y cuenta errores.

    Gramática (conceptual, simplificada)
    -----------------------------------
    json            -> element EOF
    element         -> object | array
    array           -> '[' ( element_list )? ']'
    element_list    -> element ( ',' element )*
    object          -> '{' ( attributes_list )? '}'
    attributes_list -> attribute ( ',' attribute )*
    attribute       -> STRING ':' attribute_value
    attribute_value -> object | array | STRING | NUMBER | TRUE | FALSE | NULL

    Recuperación de errores (Panic Mode)
    -----------------------------------
    - Para permitir continuar el análisis tras errores sintácticos se usa
        sincronización basada en conjuntos FIRST y FOLLOW aproximados. Estos
        conjuntos se representan con bitmasks (TokSet) para eficiencia.
    - sync_to(first, follow) descarta tokens hasta encontrar uno que pertenezca
        a FIRST∪FOLLOW o EOF.
    - Se reportan mensajes específicos cuando faltan símbolos esperados (p.ej.
        '{', '}', '[', ']', ':') y se intenta seguir analizando el resto del archivo.
    - Los conjuntos FOLLOW definidos son aproximados y suficientes para la
        sincronización en la mayoría de errores típicos; pueden ajustarse para
        obtener un comportamiento de recuperación más fino.

    Limitaciones y notas importantes
    --------------------------------
    - El lexer es intencionalmente simple: la validación de números y
        secuencias de escape en strings no es exhaustiva.
    - No se genera ni devuelve un AST; la herramienta sólo valida sintaxis.
    - La localización de errores (line/col) es aproximada debido al manejo
        simple del último carácter y el reintroducido (ungetc1).
    - T_ERROR se usa para indicar errores de tokenización; el parser los
        reporta como errores sintácticos.
    - Si se desea usar un lexer distinto (por ejemplo, el de otra tarea),
        basta con reemplazar yylex()/yytext/line/col por la implementación propia
        y asegurarse de que devuelva los mismos tokens enumerados.

    Extensiones posibles
    --------------------
    - Mejorar el lexer para validar formalmente números (regex JSON-num)
        y escapes en strings.
    - Construir un AST durante el parsing para procesar/transformar el JSON
        en lugar de limitarse a la validación sintáctica.
    - Añadir mensajes de diagnóstico más precisos (columna exacta, contexto).
    - Soportar comentarios multi-línea o JSONC si se desea mantener compatibilidad
        con variantes no estándar.

    Mensajes de error en tiempo de ejecución
    ---------------------------------------
    - Los errores sintácticos se imprimen con una etiqueta "[Linea N]" y una
        descripción (en español) junto al token actual esperado/encotrado.
    - Al finalizar, si hubo errores, se imprime el total de errores sintácticos.

    Marcelo Caceres / Historial
    -----------------
    - Implementación didáctica orientada a la práctica de compiladores y
        recuperación de errores. Ideal como base para actividades de curso.
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Tokens -------------------- */
typedef enum {
  T_LCOR = 256,   // [
  T_RCOR,         // ]
  T_LLLA,         // {
  T_RLLA,         // }
  T_COMA,         // ,
  T_DOSP,         // :
  T_STRING,       // "..."
  T_NUMBER,       // 123 | 12.3 | 1e+9 ...
  T_TRUE,         // true/TRUE
  T_FALSE,        // false/FALSE
  T_NULL,         // null/NULL
  T_EOF,
  T_ERROR
} Token;

static const char* tok_name(int t) {
  switch (t) {
    case T_LCOR: return "'['";
    case T_RCOR: return "']'";
    case T_LLLA: return "'{'";
    case T_RLLA: return "'}'";
    case T_COMA: return "','";
    case T_DOSP: return "':'";
    case T_STRING: return "string";
    case T_NUMBER: return "number";
    case T_TRUE: return "true";
    case T_FALSE: return "false";
    case T_NULL: return "null";
    case T_EOF: return "eof";
    default: return "??";
  }
}

/* -------------------- Lexer mínimo -------------------- */
/* Este scanner es simple (suficiente para la tarea). Si querés usar tu
   lexer de la Tarea 1, reemplazá yylex()/yytext/line/col por los tuyos. */

static FILE* fin;
static int line = 1, col = 0, lastc = EOF;
static char yytext[4096];

static int nextc(void){
  int c = (lastc != EOF) ? (lastc) : fgetc(fin);
  if (c == '\n') { line++; col = 0; }
  else col++;
  lastc = EOF;
  return c;
}
/*
  nextc(): lee el siguiente carácter desde el FILE* `fin`.
  - Usa `lastc` como buffer de un carácter cuando se hace un "ungetc1".
  - Actualiza `line` y `col` para proporcionar una localización aproximada
    para los mensajes de error.
  - Devuelve EOF si no hay más caracteres.
  Uso típico: funciones del lexer llaman a nextc() repetidamente para
  consumir la entrada.
*/
static void ungetc1(int c){ lastc = c; if (c == '\n') { line--; /*aprox*/ } }
/*
  ungetc1(): "deslee" un carácter dentro de `lastc` para que el siguiente
  nextc() lo devuelva. También intenta ajustar `line` cuando se deshace
  de un salto de línea (esta corrección es aproximada).
  Se utiliza por ejemplo cuando el lexer lee un carácter que no pertenece
  al token actual y necesita devolverlo a la entrada.
*/

static void skip_ws(void){
  int c;
  while ((c = nextc()) != EOF) {
    if (c=='/' && (c = nextc())=='/') { // comentarios // ... (opcional)
      while ((c = nextc()) != EOF && c!='\n');
    } else if (isspace(c)) {
      continue;
    } else { ungetc1(c); break; }
  }
}
/*
  skip_ws(): omite espacios en blanco y comentarios de línea '//' en la
  entrada. Al finalizar deja `lastc` con el primer carácter útil
  no consumido (vía ungetc1), para que el siguiente tokenizador lo trate.
*/

static int read_string(void){
  int i=0; yytext[i++]='"';
  int c;
  while ((c = nextc()) != EOF){
    yytext[i++] = (char)c;
    if (c == '\\'){ // escapar siguiente
      int d = nextc(); if (d==EOF) break;
      yytext[i++] = (char)d;
      continue;
    }
    if (c == '"'){ yytext[i]=0; return 1; }
    if (i >= (int)sizeof(yytext)-2) break;
  }
  yytext[i]=0;
  return 0;
}
/*
  read_string(): lee un literal string que comienza en '"'.
  - Copia la cadena completa en `yytext` incluyendo el par de comillas.
  - Soporta escapes simples: si encuentra '\\' copia el siguiente
    carácter sin interpretarlo (no valida secuencias como \uXXXX).
  - Devuelve 1 si cerró correctamente la comilla, 0 si hubo EOF o corte.
  - `yytext` resultante puede usarse por el parser para mensajes o
    para construir un AST si se quisiera.
*/

static int is_num_start(int c){ return isdigit(c) || c=='-'; }

static int read_number(int first){
  int i=0; yytext[i++]=(char)first;
  int c;
  while ((c = nextc()) != EOF){
    if (isdigit(c) || c=='.' || c=='e' || c=='E' || c=='+' || c=='-'){
      yytext[i++]=(char)c;
      if (i >= (int)sizeof(yytext)-1) break;
    } else { ungetc1(c); break; }
  }
  yytext[i]=0;
  return 1;
}
/*
  read_number(): consume caracteres formando un token NUMBER.
  - Recibe el primer carácter ya leído (`first`) y continúa tomando
    caracteres que pueden formar parte de un número (dígitos, '.', 'e', etc.).
  - No valida estrictamente el formato JSON de un número; es una lectura
    tolerante suficiente para este parser didáctico.
  - Devuelve 1 siempre (indicando token formado), o podría extenderse
    para detectar errores léxicos más finos.
*/

static int strieq(const char* a, const char* b){
  for(; *a && *b; a++,b++) if (tolower(*a)!=tolower(*b)) return 0;
  return *a==0 && *b==0;
}
/*
  strieq(): compara dos strings ignorando mayúsculas/minúsculas.
  - Utilizado para reconocer literales como true/false/null sin importar
    la capitalización.
*/

static Token yylex(void){
  skip_ws();
  yytext[0]=0; /* limpiar texto previo */
  int c = nextc();
  if (c == EOF) return T_EOF;
  switch (c){
    case '[': return T_LCOR;
    case ']': return T_RCOR;
    case '{': return T_LLLA;
    case '}': return T_RLLA;
    case ',': return T_COMA;
    case ':': return T_DOSP;
    case '"': {
      /* Hemos consumido la comilla inicial en `c`, así que construimos
         el string empezando por ese carácter. Evitamos usar ungetc1
         aquí por posibles efectos colaterales con nextc/ungetc1. */
      int i = 0;
      yytext[i++] = (char)c; /* '"' */
      int d;
      while ((d = nextc()) != EOF){
        yytext[i++] = (char)d;
        if (d == '\\'){
          int e = nextc(); if (e==EOF) break;
          yytext[i++] = (char)e;
          continue;
        }
        if (d == '"') { yytext[i]=0; return T_STRING; }
        if (i >= (int)sizeof(yytext)-2) break;
      }
      yytext[i]=0; return T_ERROR;
    }
    default:
      if (is_num_start(c)){
        read_number(c); return T_NUMBER;
      }
      { // true/false/null (case-insensitive)
        char buf[16]={0}; buf[0]=(char)c;
        int i=1;
        for (; i<15; i++){
          int d = nextc();
          if (d==EOF || !isalpha(d)) { ungetc1(d); break; }
          buf[i]=(char)d;
        }
        /* copiar a yytext para permitir debug del token */
        strncpy(yytext, buf, sizeof(yytext)-1);
        yytext[sizeof(yytext)-1]=0;
        if (strieq(buf,"true"))  return T_TRUE;
        if (strieq(buf,"false")) return T_FALSE;
        if (strieq(buf,"null"))  return T_NULL;
      }
      return T_ERROR;
  }
}
/*
  yylex(): lexer mínimo que devuelve el siguiente Token.
  Flujo general:
  1) Llama a skip_ws() para ignorar espacio/comentarios.
  2) Toma un carácter con nextc(). Si es un símbolo simple devuelve
     el token correspondiente inmediatamente.
  3) Si es '"' delega a read_string() para capturar la cadena completa.
  4) Si comienza como número delega a read_number().
  5) Si es letra construye un buffer y compara contra true/false/null
     (case-insensitive).
  6) Si nada de lo anterior, retorna T_ERROR.

  Nota: El lexer no diferencia entre tokens y su valor semántico (p.ej.,
  no devuelve el número convertido); deja esa responsabilidad a quien
  use `yytext` si se necesitara.
*/

/* -------------------- Parser con Panic Mode -------------------- */

static Token lookahead;
static int errors = 0;
/* activar/desactivar depuracion de tokens */
static int debug_tokens = 1;

static void advance(){
  lookahead = yylex();
  if (debug_tokens){
    if (yytext[0])
      fprintf(stderr, "[TOK] Linea %d: %s  -> %s\n", line, tok_name(lookahead), yytext);
    else
      fprintf(stderr, "[TOK] Linea %d: %s\n", line, tok_name(lookahead));
  }
}
/*
  advance(): obtiene el siguiente token y, si debug_tokens está activo,
  imprime por stderr el token y su texto (si existe) para ayudar a depurar.
*/
static void debug_advance(void){
  lookahead = yylex();
  if (debug_tokens){
    if (yytext[0])
      fprintf(stderr, "[TOK] Linea %d: %s  -> %s\n", line, tok_name(lookahead), yytext);
    else
      fprintf(stderr, "[TOK] Linea %d: %s\n", line, tok_name(lookahead));
  }
}
/*
  advance(): obtiene el siguiente token del lexer y lo almacena en
  la variable global `lookahead`. El parser usa `lookahead` para
  decidir qué regla aplicar (estilo LL(1)).
*/

static void report_err(const char* msg){
  fprintf(stderr,"[Linea %d] Error sintactico: %s. Token actual: %s\n",
          line, msg, tok_name(lookahead));
  errors++;
}
/*
  report_err(): registra un error sintáctico e imprime un mensaje con la
  línea aproximada y el token actual. Incrementa el contador global
  `errors` para que al final el programa sepa si hubo fallos.
*/

// helpers de set (bitmask) para sincronización FIRST/FOLLOW
typedef unsigned int TokSet;
#define TS_EMPTY 0u
/* TS(t): macro que convierte un Token en un bit dentro de TokSet.
  Usamos una macro para que las expresiones con TS(...) puedan ser
  usadas en inicializadores estáticos constantes. */
#define TS(t) (1u << ((int)(t) - 256))
static int in_set(TokSet S, Token t){ return (S & TS(t)) != 0; }

/*
  TokSet: representación por bitmask de conjuntos de tokens (FIRST/FOLLOW).
  - TS(t) mapea un Token (empezando en 256) a un bit dentro de un unsigned int.
  - in_set(S,t) comprueba si el token t pertenece al conjunto S.
  Estas estructuras se usan para implementar la sincronización (panic mode)
  del parser: descartar tokens hasta alcanzar uno que pertenezca a
  FIRST∪FOLLOW y así reanudar el análisis.
*/

// Conjuntos FIRST de los no terminales (según la BNF)
static const TokSet F_json   = 0
  | TS(T_LLLA) | TS(T_LCOR);
static const TokSet F_element= F_json;
static const TokSet F_array  = TS(T_LCOR);
static const TokSet F_object = TS(T_LLLA);
static const TokSet F_attrv  = 0
  | TS(T_LLLA) | TS(T_LCOR) | TS(T_STRING) | TS(T_NUMBER)
  | TS(T_TRUE) | TS(T_FALSE) | TS(T_NULL);

// FOLLOW aproximados (suficientes para sincronizar)
static const TokSet Follow_element = 0 | TS(T_COMA) | TS(T_RCOR) | TS(T_EOF);
static const TokSet Follow_attr    = 0 | TS(T_COMA) | TS(T_RLLA);

// sincroniza descartando tokens hasta FIRST ∪ FOLLOW
static void sync_to(TokSet first, TokSet follow){
  while (lookahead != T_EOF && !in_set(first, lookahead) && !in_set(follow, lookahead)) {
    advance();
  }
}
/*
  sync_to(first, follow): modo "panic" de recuperación de errores.
  - Mientras el token actual no pertenezca a FIRST ni a FOLLOW y no sea EOF,
    consume tokens (advance) descartándolos.
  - Cuando termina, el parser puede intentar aplicar la regla correspondiente
    (si el token está en FIRST) o finalizar la construcción actual (si está en FOLLOW).
  - Esta técnica evita que un único error imprevisto detenga todo el análisis.
*/

/* Prototipos de no terminales */
static void json(void);
static void element(void);
static void array(void);
static void element_list(void);
static void object(void);
static void attributes_list(void);
static void attribute(void);
static void attribute_value(void);

/* match */
static void match(Token t){
  if (lookahead == t) { advance(); }
  else {
    char msg[128];
    snprintf(msg,sizeof(msg),"se esperaba %s", tok_name(t));
    report_err(msg);
  }
}
/*
  match(t): consume el token esperado `t` si coincide con `lookahead`.
  - Si coincide: avanza al siguiente token.
  - Si no coincide: reporta un error pero no consume (no hace advance
    adicional), de modo que el flujo del parser pueda intentar recuperarse
    usando sync_to desde la regla que llamó a match.
*/

/* No terminales (LL(1) con recuperación simple) */

static void json(void){
  if (!in_set(F_json, lookahead)) {
    report_err("json: token inicial invalido");
    sync_to(F_json, TS(T_EOF));
  }
  element();
  if (lookahead != T_EOF) {
    report_err("sobran tokens luego del elemento raiz");
  }
}
/*
  json(): regla inicial del parser.
  - Verifica que el token de arranque pertenezca a FIRST(json) (objeto o array).
  - Si no, reporta error y sincroniza hasta encontrar un posible comienzo.
  - Llama a element() para analizar el elemento raíz y luego exige EOF.
  - Si hay tokens extra después del elemento raíz, los reporta como error.
*/

static void element(void){
  if (lookahead == T_LLLA) object();
  else if (lookahead == T_LCOR) array();
  else {
    report_err("element: se esperaba objeto '{' o arreglo '['");
    sync_to(F_element, Follow_element);
  }
}
/*
  element(): detecta si el elemento es un objeto o un array y delega.
  - Si ninguno de los tokens esperados aparece, reporta error y
    sincroniza hasta FIRST(element) o FOLLOW(element) para continuar.
*/

static void array(void){
  if (lookahead != T_LCOR){ report_err("array: falta '['"); sync_to(F_array, Follow_element); }
  match(T_LCOR);
  if (lookahead == T_RCOR) { match(T_RCOR); return; }
  element_list();
  if (lookahead != T_RCOR) report_err("array: falta ']'");
  match(T_RCOR);
}
/*
  array(): analiza una lista de elementos encerrada en '[' ']'.
  Flujo:
  - Comprueba y consume '[' (reporta si falta).
  - Si el siguiente token es ']' el array está vacío y retorna.
  - Si no, llama a element_list() para procesar un elemento y sus comas.
  - Finalmente exige ']' (reporta si falta) y lo consume.
  - Usa sync_to previamente si encuentra errores en el inicio para
    reubicarse en un posible comienzo de array.
*/

static void element_list(void){
  element();
  while (lookahead == T_COMA){
    match(T_COMA);
    element();
  }
}
/*
  element_list(): analiza uno o más elementos separados por comas.
  - Llama a element() para el primer elemento y luego consume repeticiones
    de ',' element.
  - No hace checks adicionales; los errores en elementos se manejan dentro
    de element() y sus subrutinas.
*/

static void object(void){
  if (lookahead != T_LLLA){ report_err("object: falta '{'"); sync_to(F_object, Follow_element); }
  match(T_LLLA);
  if (lookahead == T_RLLA) { match(T_RLLA); return; }
  attributes_list();
  if (lookahead != T_RLLA) report_err("object: falta '}'");
  match(T_RLLA);
}
/*
  object(): analiza un objeto JSON '{' attributes_list '}'.
  - Similar a array(): verifica '{', si el siguiente es '}' es vacío,
    si no delega a attributes_list().
  - Reporta la ausencia de '{' o '}' y usa sync_to para recuperación.
*/

static void attributes_list(void){
  attribute();
  while (lookahead == T_COMA){
    match(T_COMA);
    attribute();
  }
}
/*
  attributes_list(): procesa una o más parejas clave:valor separadas por comas.
  - Llama a attribute() para cada pareja.
*/

static void attribute(void){
  if (lookahead != T_STRING){
    report_err("attribute: nombre debe ser string");
    sync_to(TS(T_STRING) | TS(T_RLLA), Follow_attr);
    if (lookahead == T_RLLA || lookahead == T_COMA) return; // eps por recuperacion
  }
  match(T_STRING);
  if (lookahead != T_DOSP) report_err("attribute: falta ':'");
  match(T_DOSP);
  attribute_value();
}
/*
  attribute(): regla para una pareja STRING ':' attribute_value.
  - Si el nombre (clave) no es STRING reporta y sincroniza hasta el token
    STRING, '}' o ',' (podría tratarse de fin de lista) y, si se encuentra '}'
    o ',' por recuperación, retorna (simula epsilon para continuar).
  - Luego exige ':' y llama attribute_value() para analizar el valor.
*/

static void attribute_value(void){
  if (in_set(F_attrv, lookahead)){
    switch (lookahead){
      case T_LLLA: object(); break;
      case T_LCOR: array();  break;
      case T_STRING: match(T_STRING); break;
      case T_NUMBER: match(T_NUMBER); break;
      case T_TRUE: match(T_TRUE); break;
      case T_FALSE: match(T_FALSE); break;
      case T_NULL: match(T_NULL); break;
      default: break;
    }
  } else {
    report_err("attribute_value: token invalido");
    sync_to(F_attrv, Follow_attr | Follow_element);
  }
}
/*
  attribute_value(): acepta cualquiera de los tipos válidos para un valor
  de atributo en JSON (objeto, array, string, number, true, false, null).
  - Si el token está en FIRST(attrv) llama a la producción adecuada.
  - Si no, reporta error y sincroniza usando FIRST(attrv) y conjuntos FOLLOW
    apropiados para intentar recuperar el análisis del objeto actual.
*/

int main(int argc, char** argv){
  if (argc < 2){
    fprintf(stderr, "Uso: %s archivo.json\n", argv[0]);
    return 2;
  }
  fin = fopen(argv[1], "rb");
  if (!fin){ perror("fopen"); return 2; }
  /* prime */
  advance();
  json();

  fclose(fin);
  if (errors == 0){
    printf("OK: fuente sintacticamente correcto.\n");
    return 0;
  } else {
    printf("FINALIZO con %d error(es) sintactico(s).\n", errors);
    return 1;
  }
}
