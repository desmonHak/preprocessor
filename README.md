# vpp — Preprocesador Vesta

**vpp** (Vesta PreProcessor) es un preprocesador de texto independiente del
lenguaje, pensado para su uso con el lenguaje Vesta (.vel) pero utilizable
con cualquier lenguaje de texto plano. Tambien actua como herramienta de
construccion (builder), permitiendo ejecutar comandos del sistema y generar
codigo durante el preprocesado.

El marcador de directiva no esta cableado: `#` es el de C y el de por omision,
pero el fichero puede declarar el suyo -- ver
[Cualquier lenguaje](#cualquier-lenguaje-el-marcador-de-directiva) -- porque en
Python, shell, Ruby o Make el `#` es un comentario y no una directiva.

Implementa ademas el preprocesador que exige el estandar de C -- y con el, el de
C++ -- de modo que sirve para preprocesar codigo real: las cabeceras de la
biblioteca estandar de ambos lenguajes pasan por el, y la salida compila y se
ejecuta.  Eso se comprueba en cada build, no se afirma aqui (ver
[Conformidad con el preprocesador de C](#conformidad-con-el-preprocesador-de-c)).

---

## Caracteristicas

### Macros de plataforma (predefinidas automaticamente)

| Macro                | Se define cuando...                        |
|----------------------|--------------------------------------------|
| `__VPP_WINDOWS__`    | Compilado / ejecutado en Windows           |
| `__VPP_LINUX__`      | Compilado / ejecutado en Linux             |
| `__VPP_MACOS__`      | Compilado / ejecutado en macOS             |
| `__VPP_X86_16__`     | Arquitectura de 16 bits                    |
| `__VPP_X86_32__`     | Arquitectura de 32 bits (x86, ARM 32)      |
| `__VPP_X86_64__`     | Arquitectura de 64 bits (x86-64)           |
| `__VPP_AARCH64__`    | Arquitectura AArch64 (ARM de 64 bits)      |
| `__POINTER_WIDTH__`  | Ancho del puntero en bits (16 / 32 / 64)   |

### Macros dinamicas

| Macro                    | Valor                                       |
|--------------------------|---------------------------------------------|
| `__FILE__`               | Nombre del archivo que se esta procesando   |
| `__LINE__`               | Numero de linea actual                      |
| `__COUNTER__`            | Entero autoincremental (0, 1, 2, ...)       |
| `__DATE__`               | Fecha de compilacion: `"Mmm DD YYYY"`       |
| `__TIME__`               | Hora de compilacion: `"HH:MM:SS"`           |
| `__VPP_VERSION__`        | Version del preprocesador (e.g. `100`)      |
| `__VPP_VERSION_MAJOR__`  | Version mayor del preprocesador (e.g. `1`)  |
| `__VPP_VERSION_MINOR__`  | Version menor del preprocesador (e.g. `0`)  |

El valor de `__FILE__` y `__LINE__` se resuelve en el momento de EXPANDIR, no
al arrancar, de modo que dentro del cuerpo de una macro dan la posicion donde se
**invoca** y no donde se definio -- que es justo para lo que sirven:

```
#define AQUI __FILE__ ":" __LINE__
x = AQUI          // -> "prog.c" ":" 2
```

`__COUNTER__` se consume por expansion, asi que dos usos en la misma linea dan
valores distintos.

### Directivas soportadas

```
#define  NAME [value]                     macro objeto
#define  NAME(p1, p2, ...) body           macro funcion (con variadic)
#undef   NAME                             elimina macro
#include "archivo"                        incluir archivo (relativo)
#include <archivo>                        incluir archivo (ruta de sistema)
#include_next <archivo>                   reanuda la busqueda tras la ruta actual
#embed   "recurso"                        incrusta un binario como lista de bytes
#if      expr                             condicional por expresion
#ifdef   NAME                             condicional si esta definido
#ifndef  NAME                             condicional si NO esta definido
#elif    expr                             rama alternativa
#elifdef  NAME                            rama alternativa si NAME esta definida
#elifndef NAME                            rama alternativa si NO lo esta
#else                                     rama por defecto
#endif                                    cierre de bloque condicional
#error   mensaje                          emite error y detiene
#warning mensaje                          emite advertencia (continua)
#pragma  once                             inclusion unica del archivo
#line    N ["archivo"]                    redefine linea y archivo
#foreach VAR in (item1, item2, ...)       bucle sobre lista inline
#foreach VAR in NOMBRE_ARRAY              bucle sobre array nombrado
#foreach VAR, IDX in (item1, item2, ...)  bucle con indice numerico IDX (base 0)
#foreach VAR, IDX in NOMBRE_ARRAY         bucle con indice sobre array nombrado
#endforeach                               cierre de #foreach
#repeat  N                               repeticion N veces
#endrepeat                               cierre de #repeat
#array   NAME (item1, item2, ...)        define un array de cadenas
#exec    VARNAME comando args...         ejecuta un comando del sistema
#set     VAR = expr                      asigna una variable entera o de cadena
#set     VAR op= expr                    asignacion compuesta (+= -= *= /= %= &= |= ^= <<= >>=)
#set     VAR++                           incremento
#set     VAR--                           decremento
#assert  expr [mensaje]                  error fatal en preprocesado si expr == 0
#macro   NAME(p1, p2, ...) ... #endmacro macro multilinea (genera bloques de codigo)
```

Lo que empieza por el marcador y no es ninguna de estas es un **error**, no un
aviso: el marcador es inequivoco, asi que solo puede ser una equivocacion.  Si
en tu lenguaje ese caracter no marca directivas, declaralo (ver
[Cualquier lenguaje](#cualquier-lenguaje-el-marcador-de-directiva)) y esas
lineas pasaran a ser texto.

### Directivas y operadores de C

Ademas del dialecto propio, vpp implementa lo que exige el preprocesador de C.
Lo que sigue no es obvio y conviene tenerlo escrito:

**`#line N "fichero"`** cambia la posicion que reportan `__LINE__` y `__FILE__`
a partir de la linea siguiente.  Sirve para que el codigo generado apunte a su
fuente original en vez de al fichero intermedio.

```
#line 100 "original.vx"
aqui = __LINE__        // -> 100
de   = __FILE__        // -> "original.vx"
```

**`_Pragma("texto")`** equivale a escribir `#pragma texto`.  Existe porque una
macro no puede generar una directiva, y este operador es la via que da el
estandar para conseguirlo:

```
#define EMPAQUETAR(n) _Pragma(#n)
EMPAQUETAR(pack(1))    // -> #pragma pack(1)
```

**Reescaneo.**  El resultado de expandir una macro se relee junto con los
tokens que le SIGUEN, no solo por su cuenta.  Es lo que hace funcionar el
idioma con el que se elige macro segun el numero de argumentos:

```
#define ELIGE(_1,_2,_3,NOMBRE,...) NOMBRE
#define F(...) ELIGE(__VA_ARGS__, F3, F2, F1)(__VA_ARGS__)
```

`ELIGE(...)` produce el NOMBRE, y los parentesis que quedan a su lado son sus
argumentos.  La macro que se acaba de expandir queda oculta mientras se relee,
de modo que `#define R(x) R` con `R(1)(2)` da `R(2)` en vez de reentrar en R.

**Aritmetica de `#if`** con las conversiones de C: si cualquiera de los
operandos es sin signo, la operacion entera lo es.  Eso cambia resultados de
forma poco intuitiva y conviene no olvidarlo:

```
#if -1 > 0u            // VERDADERO: el -1 se convierte en un valor enorme
#if (-1 >> 1) == -1    // verdadero: con signo se replica el bit alto
#if (0xFFFFFFFFFFFFFFFFu >> 60) == 15   // sin signo entran ceros
```

Un literal es sin signo si lleva sufijo `u`/`U`, o si no cabe en un entero con
signo aunque no lo lleve.

**Cadenas crudas de C++** (`R"delim( ... )delim"`) se leen enteras: dentro no
hay escapes, ni comentarios, ni macros, ni directivas, y pueden ocupar varias
lineas.  El delimitador es lo que las hace utiles, porque deja escribir `)"` en
el contenido:

```
auto s = R"xy(esto tiene )" dentro y no cierra nada)xy";
```

Se puede apagar (`LexerOptions::raw_strings`) para un lenguaje en el que un
identificador acabado en `R` pueda ir pegado a una comilla y signifique otra
cosa.  Solo cuentan los cinco prefijos del estandar -- `R`, `LR`, `uR`, `UR`,
`u8R` -- de modo que un `FOOR"..."` sigue siendo un identificador y una cadena
normal.

**`#include_next <fichero>`** repite la busqueda del fichero, pero empezando en
el directorio SIGUIENTE a aquel donde aparecio el que la escribe.  Es como una
cabecera se superpone a otra del mismo nombre sin perderla: la propia se
encuentra primero, y desde dentro llega a la de debajo para completarla.  Toda
la biblioteca estandar de C++ se apoya en esto para envolver las cabeceras de C.

```
// /usr/include/c++/15/math.h
#include_next <math.h>     // la de C, no esta misma
```

Requiere saber DONDE aparecio cada fichero, no solo su nombre, asi que la
busqueda lleva ese dato consigo (`IncludeSearch`, en `pp_include.h`).  De
propina, un `#include "vecino.h"` escrito dentro de una cabecera hallada por una
ruta de busqueda se resuelve al lado de ESA cabecera, que es lo correcto.

**Operadores de prueba de caracteristicas.**  `__has_include`, `__has_builtin`,
`__has_attribute`, `__has_c_attribute`, `__has_cpp_attribute`, `__has_feature`,
`__has_extension` y `__has_declspec_attribute` se pueden usar en `#if`:

```
#if __has_include(<optional>)
#  include <optional>
#endif

#if __has_builtin(__builtin_expect)
#  define PROBABLE(x) __builtin_expect(!!(x), 1)
#else
#  define PROBABLE(x) (x)
#endif
```

`__has_include` lo resuelve vpp por su cuenta, porque es una pregunta sobre el
sistema de ficheros y ahi no hace falta nadie mas.  Los demas preguntan por
capacidades DEL COMPILADOR, y vpp no es un compilador: no puede inventarse la
respuesta sin mentir.  Asi que se la pregunta al compilador objetivo, el que se
le indique con `--capabilities-from`:

```bash
vpp --capabilities-from "gcc -E -P -x c" fuente.c
```

Se le pasa una vez por operador y argumento distintos, y el resultado queda
cacheado en memoria durante la ejecucion.  Sin `--capabilities-from` no hay a
quien preguntar: `__has_include` sigue funcionando, y el resto **devuelve 0**,
que es la respuesta honesta -- "no consta que exista" -- y la que hace que el
codigo bien escrito tome su rama de reserva en vez de romperse.  Si se sabe la
respuesta, se puede fijar a mano con `-D__has_builtin(x)=1` o similar.

Estos operadores ademas **constan como definidos**, que es una pregunta
distinta de cuanto valen y hay que contestarla igual.  Las bibliotecas estandar
se protegen asi:

```
#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif
```

Un preprocesador que no se declare capaz se come esa macro, y a partir de ahi
toda consulta responde 0 aunque hubiera compilador al que preguntar.  Con
`--capabilities-from` vpp se declara capaz y el shim no se instala; sin el, no
se declara, el shim entra y el codigo toma su rama de reserva -- que es
exactamente para lo que el shim existe.  `__has_include` consta como definido
siempre, porque no necesita compilador.

Que un operador este definido tampoco se supone: se pregunta.  El juego depende
del MODO, no solo del compilador -- clang tiene `__has_cpp_attribute`
compilando C++ y no compilando C -- y darlo por bueno lleva a consultar algo
que ese compilador rechaza, y con ello a ramas que el nunca habria tomado.

Por el mismo motivo se delegan tambien los **predicados propios** de cada
compilador, los que no empiezan por `__has_`: `__is_target_arch`,
`__is_target_vendor`, `__is_identifier` y demas.  Las cabeceras de macOS los
usan de verdad.  Solo se delega un nombre que empiece por `__`, vaya seguido de
un parentesis y que el compilador de destino tenga; sin esas tres condiciones,
cualquier identificador seguido de un parentesis se tomaria por uno de ellos y
una expresion mal escrita se quedaria sin diagnostico.

---

### Operadores en cuerpo de macros

| Operador | Descripcion                                          | Ejemplo                           |
|----------|------------------------------------------------------|-----------------------------------|
| `#param` | Stringify: convierte el argumento a cadena literal   | `#define S(x) #x` → `S(hi)="hi"` |
| `a##b`   | Token paste: fusiona dos tokens en uno               | `#define P(a,b) a##b` → `P(x,y)=xy` |
| `...`    | Parametro variadic en macro funcion                  | `#define F(...) __VA_ARGS__`      |

### Expresiones en directivas condicionales

Las expresiones de `#if` y `#elif` soportan:

- Literales: decimales, hexadecimales (`0xFF`), octales (`077`), binarios (`0b101`)
- Aritmetica: `+ - * / % ~ ^ & | << >>`
- Logica: `&& || !`
- Comparacion: `== != < > <= >=`
- Ternario: `cond ? a : b`
- Operador `defined(NAME)` y `defined NAME`



---

## Incrustar un recurso: #embed

Mete un binario -- un icono, una tabla, una clave -- dentro del fuente, sin
generar el fichero intermedio con un script aparte.  Es de C23 y el recurso se
busca con las MISMAS rutas que un `#include`:

```c
const unsigned char logo[] = {
#embed "logo.png"
};
```

| Parametro | Que hace |
| :-------- | :------- |
| `limit(N)` | Lee N bytes como mucho.  Acepta una macro |
| `prefix(...)` | Tokens antes de los datos |
| `suffix(...)` | Tokens despues |
| `if_empty(...)` | Tokens en lugar de todo, si el recurso esta vacio |

Un recurso vacio -- o un `limit(0)` -- **no emite ni prefijo ni sufijo**, solo lo
que diga `if_empty`.  Tiene sentido: el prefijo suele ser el separador que une
los datos con lo que tienen al lado, y sin datos sobra.  Esta medido contra gcc.

`__has_embed` dice si el recurso se puede incrustar, y devuelve **tres** valores
y no dos:

```c
#if __has_embed("logo.png")            // 1 esta y tiene datos
const unsigned char logo[] = {
#embed "logo.png"
};
#else                                  // 0 no esta, 2 esta pero vacio
const unsigned char logo[] = {0};
#endif
```

La diferencia importa: un recurso vacio no se puede incrustar igual que uno con
datos.

Los bytes salen sin signo -- 255, no -1 -- y un cero no corta la lectura: lo que
se lee es un binario, no una cadena.
---

## Cualquier lenguaje: el marcador de directiva

`#` es el marcador de C, y por eso es el de por omision.  Pero vpp no es un
preprocesador de C, y en Python, shell, Ruby, Make, YAML o TOML el `#` es un
**comentario**: tomarlo por directiva se come lineas que no son suyas.

El fichero puede decir cual es el suyo, en sus primeras lineas:

```python
#!/usr/bin/env python3
# vpp:marker=%
# este comentario es de Python y sale intacto
%define DOBLE 2
def f(x): return x * DOBLE
```

Va en el fichero y no en la linea de ordenes a proposito: asi el dialecto viaja
con el fuente en lugar de con la invocacion.  Una bandera hay que acordarse de
pasarla, se pierde al mover el fichero a otro build, y no le dice nada a quien
lo abre.  Es el mismo motivo por el que Python declara su codificacion en las
dos primeras lineas en vez de esperarla del entorno.

Se busca el texto `vpp:` sin exigir ninguna sintaxis alrededor, asi que la
declaracion se escribe dentro del comentario del propio lenguaje -- y con eso se
rompe el circulo vicioso de tener que conocer el lenguaje para leer la linea que
dice cual es el lenguaje:

```
# vpp:marker=%           Python, shell, Ruby, Make, YAML
-- vpp:marker=%          Lua, SQL, Haskell
// vpp:marker=%          C, C++, Rust, Java
<!-- vpp:marker=% -->    HTML, XML
;; vpp:marker=%          Lisp, ensamblador
```

Se mira solo al principio (tres lineas, para dejar sitio a un shebang); mas
abajo no cuenta, y si aparece alli se avisa en vez de no hacer nada en silencio.
Para un fichero que no se pueda tocar -- generado, o de terceros -- esta
`--marker`, y la declaracion del fichero le gana por ser mas concreta.

### Lo que NO se pierde por el camino

Una directiva mal escrita **sigue siendo un error**:

```
$ vpp typo.py
typo.py:2:1: error: directiva de preprocesador desconocida: %defien
```

La alternativa facil habria sido dejar pasar lo desconocido, y entonces
`%defien FOO 1` se colaria como texto sin que nadie se enterase.  Con el
marcador declarado no hay que elegir: lo que empieza por el es una directiva y
un nombre desconocido ahi es una equivocacion; lo que no, es texto y sale
intacto.  Antes esto era un aviso Y ademas se tragaba la linea, que es lo peor
de las dos opciones.


### Comentarios y comillas

Lo mismo vale para el resto de lo que el lexer da por sentado de C.  Los
ajustes que admite la declaracion:

| Ajuste | Que hace | Para que |
| :----- | :------- | :------- |
| `marker=X` | Que empieza una directiva | `#` es comentario en Python, shell, Ruby, Make |
| `line-comments=0` | `//` deja de comentar | En Lua es division entera: `7 // 2` perdia el `// 2` **sin avisar** |
| `block-comments=0` | `/* */` deja de comentar | Lenguajes que no los tienen |
| `line-comment=SEQ` | Declara el comentario de linea del lenguaje | `--` en Lua y SQL, `;` en Lisp |
| `block-comment-open=SEQ` | Y el de bloque | `--[[` en Lua |
| `block-comment-close=SEQ` | Su cierre | `]]` en Lua |
| `strings=0` | `"` deja de abrir cadena | Texto llano, o donde la comilla no delimite |
| `char-literals=0` | `'` deja de abrir caracter | Un apostrofo: `It's a test` fallaba con "literal sin cerrar" |
| `raw-strings=0` | `R"(...)"` deja de ser cruda | Donde un nombre pueda ir pegado a una comilla |

```
-- vpp:marker=% line-comments=0
local n = 7 // 2            -- la division sobrevive
```

Apagar un comentario y **declarar** el propio no es lo mismo.  Apagado, el
comentario del lenguaje sale como texto corriente, asi que una macro mencionada
dentro SI se expande.  Declarado, vpp lo descarta como lo que es:

```lua
-- vpp:marker=% line-comment=-- block-comment-open=--[[ block-comment-close=]]
%define N 9
-- este comentario menciona N y no se toca
--[[ el bloque
tampoco ]]
local x = N          -- aqui si: queda 9
local y = 7 // 2     -- y // vuelve a ser division entera
```

El de bloque se comprueba antes que el de linea, que es lo que hace falta cuando
uno es prefijo del otro -- en Lua `--[[` abre bloque y `--` abre linea.

Estos ajustes hablan del **texto del lenguaje de destino**.  Dentro de una
directiva rige siempre la sintaxis de vpp, asi que `#include "fichero.h"` sigue
leyendo su cadena aunque `strings` este apagado -- que es justo el lenguaje en
el que hara falta.
### Alcance

El dialecto es **de cada fichero** y no se hereda al incluir.  Un fuente en
Python con marcador `%` puede hacer `%include "cabecera.h"` y esa cabecera se
lee con el suyo.

---

## Metaprogramacion

### #foreach con lista inline

Itera sobre una lista de valores, definiendo una variable de iteracion:

```
#foreach TYPE in (int, float, double)
register_TYPE:
    ; instrucciones para TYPE
#endforeach
```

Produce:
```
register_int:
    ; instrucciones para int
register_float:
    ; instrucciones para float
register_double:
    ; instrucciones para double
```

### #foreach con indice (VAR, IDX)

La forma extendida permite obtener el indice numerico de cada elemento (base 0):

```
#array REGS (r0, r1, r2, r3)

#foreach REG, IDX in REGS
    mov r0, IDX                    ; 0, 1, 2, 3
    mov r1, __MUL__(IDX, 8)        ; 0, 8, 16, 24
#endforeach
```

`IDX` es un token `NUMBER` y se sustituye en cualquier posicion donde aparezca
como token independiente. **Limitacion:** `IDX` no se sustituye cuando forma parte
de un identificador compuesto (`entry_IDX` permanece literal; usar `##` requeriria
soporte futuro de token-paste en expansiones).

### #array y #foreach con array nombrado

Define un array con nombre y reusalo en multiples lugares:

```
#array PLATFORMS (linux, windows, macos)
#array FLAGS     (-O2, -Wall, -Wextra)

#foreach PLAT in PLATFORMS
build_PLAT:
#endforeach

#foreach FLAG in FLAGS
CFLAGS += FLAG
#endforeach
```

Los arrays tambien se consultan con las macros funcion integradas:
```
__ARRAY_SIZE__(PLATFORMS)     ; -> 3
__ARRAY_GET__(PLATFORMS, 1)   ; -> windows
```

### #repeat

Repite un bloque N veces. Dentro del cuerpo, `__REPEAT_INDEX__` contiene
el indice de la iteracion actual (base 0):

```
#repeat 4
slot___REPEAT_INDEX__ dw 0
#endrepeat
```

Produce:
```
slot_0 dw 0
slot_1 dw 0
slot_2 dw 0
slot_3 dw 0
```

`#repeat` acepta expresiones y macros funcion como contador:

```
#array REGS (r0, r1, r2, r3, r4, r5, r6, r7)
#repeat __ARRAY_SIZE__(REGS)
    push __ARRAY_GET__(REGS, __REPEAT_INDEX__)
#endrepeat
```

### #assert

Evalua una expresion en tiempo de preprocesado. Si el resultado es 0 (falso),
emite un error fatal con el mensaje opcional y detiene el procesado:

```
#define MAX_REGS  16
#define PAGE_SIZE 4096

; verificar invariantes de compilacion
#assert __ISODD__(MAX_REGS) == 0         "MAX_REGS debe ser par"
#assert MAX_REGS <= 32                   "MAX_REGS no puede superar 32"
#assert __LOG2__(PAGE_SIZE) == 12        "PAGE_SIZE debe ser 2^12"
#assert __MOD__(PAGE_SIZE, MAX_REGS) == 0
```

La expresion acepta todo lo que admite `#if`: aritmetica, bitwise, comparaciones,
`defined(NAME)`, macros funcion integradas y macros objeto definidas.

### #macro ... #endmacro

Define una macro multilinea que genera bloques de instrucciones al expandirse.
Es equivalente a `#define` pero el cuerpo puede ocupar multiples lineas:

```
; wrapper para la convencion de llamada de vio_print_int
#macro PRINT_INT(val)
    mov r1, val
    mov r15, 1
    calln @Method("stdlib/native/io/vesta_io:vio_print_int")
#endmacro

; uso: cada invocacion expande a las 3 instrucciones anteriores
    PRINT_INT(42)
    PRINT_INT(__MUL__(6, 7))   ; argumento evaluado en preprocesado -> 42
```

Otra macro con varios parametros:

```
#macro PRINTLN(proc, addr, len)
    mov r1, proc
    mov r2, addr
    mov r3, len
    mov r15, 3
    calln @Method("stdlib/native/io/vesta_io:vio_println")
#endmacro

    PRINTLN(r14, @Absolute("all.msg"), 5)
```

Las macros multilinea admiten `...` como ultimo parametro (variadic) igual que
las macros funcion de `#define`. Los parametros se sustituyen en el cuerpo por
coincidencia de token.

---

## Variables (#set)

`#set` define o modifica variables del preprocesador. A diferencia de `#define`,
las variables creadas con `#set` no emiten advertencias al ser reasignadas.

### Asignacion simple

```
#set CONTADOR = 0
#set NOMBRE   = "hello"
#set VALOR    = 3 + 4 * 2     ; evalua la expresion -> 11
```

### Operadores compuestos

| Operador | Descripcion            | Ejemplo                  |
|----------|------------------------|--------------------------|
| `=`      | Asignacion             | `#set N = 42`            |
| `+=`     | Suma y asigna          | `#set N += 5`            |
| `-=`     | Resta y asigna         | `#set N -= 3`            |
| `*=`     | Multiplica y asigna    | `#set N *= 2`            |
| `/=`     | Divide y asigna        | `#set N /= 4`            |
| `%=`     | Modulo y asigna        | `#set N %= 7`            |
| `&=`     | AND de bits y asigna   | `#set N &= 0x0F`         |
| `\|=`    | OR de bits y asigna    | `#set N \|= 0x80`        |
| `^=`     | XOR de bits y asigna   | `#set N ^= 0xFF`         |
| `<<=`    | Desplaz. izq. y asigna | `#set N <<= 2`           |
| `>>=`    | Desplaz. der. y asigna | `#set N >>= 1`           |
| `++`     | Incremento             | `#set N++`               |
| `--`     | Decremento             | `#set N--`               |

### Uso en condicionales y bucles

```
; contador con #repeat
#set IDX = 0
#repeat 8
    slot_IDX: dw 0
    #set IDX++
#endrepeat

; usar variable en #if
#set LEVEL = 3
#if LEVEL >= 2
    #define VERBOSE
#endif
```

---

## Ejecucion de comandos (#exec)

`#exec` ejecuta un comando del sistema y guarda la salida estandar (stdout)
en una macro. El comando se expande con macros antes de ejecutarse.

```
#exec GIT_HASH   git rev-parse --short HEAD
#exec BUILD_DATE date +%Y-%m-%d

; usar las macros definidas por exec
version: db "BUILD_DATE-GIT_HASH", 0
```

Tambien existe la forma de macro funcion integrada `__EXEC__`:

```
version: db __EXEC__("git describe --tags"), 0
```

---

## Macros funcion integradas

Todas las macros funcion integradas aceptan argumentos que pueden ser otras
macros o incluso otras macros funcion integradas (composicion completa).

### Operaciones sobre cadenas

| Macro                        | Resultado                                     |
|------------------------------|-----------------------------------------------|
| `__STRLEN__(s)`              | Longitud de `s` (numero)                      |
| `__TOUPPER__(s)`             | `s` convertida a mayusculas (cadena)          |
| `__TOLOWER__(s)`             | `s` convertida a minusculas (cadena)          |
| `__SUBSTR__(s, inicio, len)` | Subcadena de `s` desde `inicio` con `len` caracteres |
| `__STRCAT__(a, b)`           | Concatenacion de `a` y `b` (cadena)           |
| `__STREQ__(a, b)`            | `1` si `a == b`, `0` en caso contrario        |
| `__CONTAINS__(s, sub)`       | `1` si `s` contiene `sub`, `0` en caso contrario |
| `__STRREPLACE__(s, de, a)`   | Reemplaza todas las ocurrencias de `de` por `a` en `s` |
| `__TRIM__(s)`                | Elimina espacios al inicio y al final de `s`  |

Ejemplos:
```
__STRLEN__(hello)                     ; -> 5
__TOUPPER__(hello)                    ; -> "HELLO"
__SUBSTR__(hello world, 6, 5)         ; -> "world"
__STRCAT__(foo, bar)                  ; -> "foobar"
__STREQ__(abc, abc)                   ; -> 1
__CONTAINS__(hello world, world)      ; -> 1
__STRREPLACE__(a.b.c, ., /)           ; -> "a/b/c"
__TOUPPER__(__STRCAT__(foo, bar))     ; -> "FOOBAR"
```

### Aritmetica entera

| Macro              | Resultado                                        |
|--------------------|--------------------------------------------------|
| `__ADD__(a, b)`    | `a + b`                                          |
| `__SUB__(a, b)`    | `a - b`                                          |
| `__MUL__(a, b)`    | `a * b`                                          |
| `__DIV__(a, b)`    | `a / b` (entero; devuelve `0` si `b == 0`)       |
| `__MOD__(a, b)`    | `a % b`                                          |
| `__NEG__(a)`       | `-a`                                             |

Ejemplos:
```
__ADD__(8, 3)           ; -> 11
__SUB__(8, 3)           ; -> 5
__MUL__(4, 5)           ; -> 20
__DIV__(10, 3)          ; -> 3
__MOD__(10, 3)          ; -> 1
__NEG__(5)              ; -> -5
```

### Operaciones de bit (enteras)

| Macro                  | Resultado                                          |
|------------------------|----------------------------------------------------|
| `__AND__(a, b)`        | `a & b`                                            |
| `__OR__(a, b)`         | `a \| b`                                           |
| `__XOR__(a, b)`        | `a ^ b`                                            |
| `__NOT__(a)`           | `~a` (complemento a uno de 64 bits)                |
| `__SHL__(a, n)`        | `a << n`                                           |
| `__SHR__(a, n)`        | `(uint64)a >> n` (desplazamiento logico)           |
| `__SAR__(a, n)`        | `(int64)a >> n`  (desplazamiento aritmetico)       |

Ejemplos:
```
__AND__(0xFF, 0x0F)      ; -> 15
__OR__(0x10, 0x01)       ; -> 17
__XOR__(0xFF, 0x0F)      ; -> 240
__NOT__(0)               ; -> 18446744073709551615
__SHL__(1, 4)            ; -> 16
__SHR__(256, 4)          ; -> 16
__SAR__(-16, 2)          ; -> -4
```

### Comparaciones enteras

| Macro              | Resultado                              |
|--------------------|----------------------------------------|
| `__EQ__(a, b)`     | `1` si `a == b`, `0` en caso contrario |
| `__NEQ__(a, b)`    | `1` si `a != b`, `0` en caso contrario |
| `__LT__(a, b)`     | `1` si `a < b`,  `0` en caso contrario |
| `__GT__(a, b)`     | `1` si `a > b`,  `0` en caso contrario |
| `__LE__(a, b)`     | `1` si `a <= b`, `0` en caso contrario |
| `__GE__(a, b)`     | `1` si `a >= b`, `0` en caso contrario |

Ejemplos:
```
__EQ__(3, 3)            ; -> 1
__NEQ__(3, 4)           ; -> 1
__LT__(2, 5)            ; -> 1
__GE__(5, 5)            ; -> 1
```

### Operaciones numericas

| Macro                        | Resultado                                         |
|------------------------------|---------------------------------------------------|
| `__MIN__(a, b)`              | Minimo de `a` y `b`                               |
| `__MAX__(a, b)`              | Maximo de `a` y `b`                               |
| `__ABS__(n)`                 | Valor absoluto de `n`                             |
| `__POW__(base, exp)`         | `base` elevado a `exp` (enteros, exp >= 0)        |
| `__CLAMP__(val, min, max)`   | `val` limitado al rango `[min, max]`              |
| `__LOG2__(n)`                | Parte entera de log2(`n`); `0` para `n <= 0`      |
| `__ISODD__(n)`               | `1` si `n` es impar, `0` si es par                |
| `__ISEVEN__(n)`              | `1` si `n` es par, `0` si es impar                |
| `__NUMFMT__(n, base)`        | Representacion de `n` en base 2, 8, 10 o 16      |
| `__ALIGN__(n, align)`        | Redondea `n` hacia arriba al multiplo de `align`  |

Ejemplos:
```
__MIN__(3, 7)           ; -> 3
__MAX__(3, 7)           ; -> 7
__ABS__(8)              ; -> 8
__POW__(2, 8)           ; -> 256
__CLAMP__(15, 0, 10)    ; -> 10
__LOG2__(8)             ; -> 3
__ISODD__(7)            ; -> 1
__ISEVEN__(6)           ; -> 1
__NUMFMT__(255, 16)     ; -> "ff"
__NUMFMT__(5, 2)        ; -> "101"
__ALIGN__(13, 8)        ; -> 16
```

### Operaciones sobre punto flotante (IEEE 754)

Los argumentos y resultados son los **bits de 64 bits** del double IEEE 754.
Usar `__F2BITS__(literal)` para convertir un literal decimal a bits, e `__ITOF__(n)` para convertir un entero.

#### Conversion

| Macro              | Resultado                                              |
|--------------------|--------------------------------------------------------|
| `__F2BITS__(x)`    | Bits IEEE 754 del literal decimal `x`                  |
| `__BITS2F__(bits)` | Cadena con el valor decimal del double `bits`          |
| `__FTOI__(bits)`   | Parte entera truncada del double `bits` (entero)       |
| `__ITOF__(n)`      | Bits IEEE 754 del double equivalente al entero `n`     |
| `__FFORMAT__(bits)`| Cadena con formato `%.17g` del double `bits`           |
| `__FPI__()`        | Bits IEEE 754 de pi = 4614256656552045848              |

#### Aritmetica

| Macro                   | Resultado                    |
|-------------------------|------------------------------|
| `__FADD__(a, b)`        | Bits de `a + b`              |
| `__FSUB__(a, b)`        | Bits de `a - b`              |
| `__FMUL__(a, b)`        | Bits de `a * b`              |
| `__FDIV__(a, b)`        | Bits de `a / b`              |
| `__FMOD__(a, b)`        | Bits de `fmod(a, b)`         |
| `__FABS__(a)`           | Bits de `|a|`                |
| `__FNEG__(a)`           | Bits de `-a`                 |
| `__FSQRT__(a)`          | Bits de `sqrt(a)`            |
| `__FPOW__(a, b)`        | Bits de `pow(a, b)`          |
| `__FFLOOR__(a)`         | Bits de `floor(a)`           |
| `__FCEIL__(a)`          | Bits de `ceil(a)`            |
| `__FROUND__(a)`         | Bits de `round(a)`           |
| `__FTRUNC__(a)`         | Bits de `trunc(a)`           |
| `__FMIN__(a, b)`        | Bits de `min(a, b)`          |
| `__FMAX__(a, b)`        | Bits de `max(a, b)`          |
| `__FCLAMP__(v, lo, hi)` | Bits de `clamp(v, lo, hi)`   |

#### Trigonometria (angulos en radianes como bits IEEE 754)

| Macro                | Resultado                         |
|----------------------|-----------------------------------|
| `__FSIN__(a)`        | Bits de `sin(a)`                  |
| `__FCOS__(a)`        | Bits de `cos(a)`                  |
| `__FTAN__(a)`        | Bits de `tan(a)`                  |
| `__FASIN__(a)`       | Bits de `asin(a)`                 |
| `__FACOS__(a)`       | Bits de `acos(a)`                 |
| `__FATAN__(a)`       | Bits de `atan(a)`                 |
| `__FATAN2__(y, x)`   | Bits de `atan2(y, x)`             |
| `__FLOG__(a)`        | Bits de `log(a)` (natural)        |
| `__FLOG2__(a)`       | Bits de `log2(a)`                 |
| `__FLOG10__(a)`      | Bits de `log10(a)`                |
| `__FEXP__(a)`        | Bits de `exp(a)`                  |
| `__FDEG2RAD__(a)`    | Bits de `a * pi / 180`            |
| `__FRAD2DEG__(a)`    | Bits de `a * 180 / pi`            |

#### Comparaciones y clasificacion (devuelven 0 o 1)

| Macro                | Resultado                              |
|----------------------|----------------------------------------|
| `__FEQ__(a, b)`      | `1` si `a == b`                        |
| `__FLT__(a, b)`      | `1` si `a < b`                         |
| `__FGT__(a, b)`      | `1` si `a > b`                         |
| `__FISNAN__(a)`      | `1` si `a` es NaN                      |
| `__FISINF__(a)`      | `1` si `a` es infinito                 |
| `__FISZERO__(a)`     | `1` si `a` es cero                     |

### Conversion numerica y manipulacion de bits

#### Conversion de bases

| Macro                      | Resultado                                    |
|----------------------------|----------------------------------------------|
| `__DEC2HEX__(n)`           | `n` en base 16 (cadena sin prefijo `0x`)     |
| `__DEC2BIN__(n)`           | `n` en base 2  (cadena sin prefijo `0b`)     |
| `__DEC2OCT__(n)`           | `n` en base 8  (cadena sin prefijo `0`)      |
| `__HEX2DEC__(s)`           | Valor decimal del hexadecimal `s`            |
| `__BIN2DEC__(s)`           | Valor decimal del binario `s`                |
| `__OCT2DEC__(s)`           | Valor decimal del octal `s`                  |
| `__PARSE_INT__(s, base)`   | Valor decimal de `s` en la base indicada     |

#### Extraccion de bytes, words y dwords

| Macro              | Resultado                                    |
|--------------------|----------------------------------------------|
| `__LOBYTE__(n)`    | Byte bajo (bits 0..7)                        |
| `__HIBYTE__(n)`    | Byte alto del word bajo (bits 8..15)         |
| `__LOWORD__(n)`    | Word bajo (bits 0..15)                       |
| `__HIWORD__(n)`    | Word alto del dword bajo (bits 16..31)       |
| `__LODWORD__(n)`   | Dword bajo (bits 0..31)                      |
| `__HIDWORD__(n)`   | Dword alto (bits 32..63)                     |

#### Inversión de bytes (endianness)

| Macro           | Resultado                          |
|-----------------|------------------------------------|
| `__SWAP16__(n)` | Intercambia los dos bytes de `n`   |
| `__SWAP32__(n)` | Invierte los 4 bytes de `n`        |
| `__SWAP64__(n)` | Invierte los 8 bytes de `n`        |

#### Operaciones bit a bit

| Macro                   | Resultado                                      |
|-------------------------|------------------------------------------------|
| `__BITSET__(n, bit)`    | `n` con el bit `bit` puesto a 1                |
| `__BITCLEAR__(n, bit)`  | `n` con el bit `bit` puesto a 0                |
| `__BITTEST__(n, bit)`   | `1` si el bit `bit` de `n` esta activo         |
| `__BITCOUNT__(n)`       | Numero de bits a 1 en `n` (popcount)           |
| `__SIGNEXT__(n, width)` | Extension de signo de `n` con `width` bits     |
| `__ZEROEXT__(n, width)` | Extension de cero de `n` enmascarando a `width` bits |

### Operaciones sobre arrays

| Macro                               | Resultado                                              |
|-------------------------------------|--------------------------------------------------------|
| `__ARRAY_SIZE__(NOMBRE)`            | Numero de elementos del array `NOMBRE`                 |
| `__ARRAY_GET__(NOMBRE, idx)`        | Elemento en la posicion `idx` del array (token IDENT)  |
| `__ARRAY_FIND__(NOMBRE, valor)`     | Indice de `valor` en el array; `-1` si no existe       |
| `__ARRAY_JOIN__(NOMBRE, sep)`       | Todos los elementos unidos por `sep` (cadena)          |

Ejemplo:
```
#array VM_OPCODES (NOP, PUSH, POP, MOV, ADD, HLT)

__ARRAY_SIZE__(VM_OPCODES)            ; -> 6
__ARRAY_GET__(VM_OPCODES, 3)          ; -> MOV  (token IDENT, sin comillas)
__ARRAY_FIND__(VM_OPCODES, ADD)       ; -> 4
__ARRAY_FIND__(VM_OPCODES, SUB)       ; -> -1
__ARRAY_JOIN__(VM_OPCODES, ", ")      ; -> "NOP, PUSH, POP, MOV, ADD, HLT"
__ARRAY_JOIN__(VM_OPCODES, "|")       ; -> "NOP|PUSH|POP|MOV|ADD|HLT"

; uso tipico: asignar IDs de opcode sin token-pasting
#define OP_NOP  __ARRAY_FIND__(VM_OPCODES, NOP)    ; -> 0
#define OP_HLT  __ARRAY_FIND__(VM_OPCODES, HLT)    ; -> 5
```

**Nota sobre `__ARRAY_GET__` y db:** `__ARRAY_GET__` devuelve un token IDENT sin comillas.
Usarlo directamente en `db` produce un error del parser (`db MOV` en vez de `db "MOV"`).
Soluciones: `__TOUPPER__` (cita y convierte a mayuscula) o `__QUOTE__` (cita sin cambiar caja).

### Manipulacion de tokens

| Macro              | Resultado                                                       |
|--------------------|-----------------------------------------------------------------|
| `__QUOTE__(x)`     | Devuelve `x` como cadena entre comillas; no modifica la caja    |
| `__UNQUOTE__(s)`   | Extrae el contenido de la cadena `s` como token IDENT           |

```
#array OPS (add, sub, mul)

; __ARRAY_GET__ devuelve IDENT -> "db add" = crash del parser
; __QUOTE__ cita sin cambiar caja:
entry db __QUOTE__(__ARRAY_GET__(OPS, 0)), 0x00   ; -> db "add", 0x00

; __TOUPPER__ cita y convierte a mayuscula:
ENTRY db __TOUPPER__(__ARRAY_GET__(OPS, 0)), 0x00 ; -> db "ADD", 0x00

; __UNQUOTE__ para usar un string como nombre de etiqueta o macro:
#define MY_LABEL __UNQUOTE__("my_section")
MY_LABEL dw 0   ; -> my_section dw 0
```

### Ejecucion de comandos

| Macro              | Resultado                                            |
|--------------------|------------------------------------------------------|
| `__EXEC__("cmd")`  | Salida estandar del comando `cmd` (cadena literal)   |

---

## Ejemplos de uso avanzado

### Generacion de codigo con arrays

```
#array OPCODES (ADD, SUB, MUL, DIV)

#foreach OP in OPCODES
exec_OP:
    call handler___TOLOWER__(OP)
    ret
#endforeach
```

### Combinar exec con condicionales

```
#exec OS_NAME uname -s

#ifdef __LINUX__
    #define LIB_EXT so
#else
    #define LIB_EXT dll
#endif

lib_name: db "mylib.LIB_EXT", 0
```

### Construir tablas de datos en tiempo de preprocesado

```
#array SIZES (1, 2, 4, 8, 16, 32, 64, 128)

; tabla de potencias de 2
powers:
#foreach SZ in SIZES
    dq __POW__(2, __LOG2__(SZ) + 1)
#endforeach
```

### Usar exec como sistema de build

```
; obtener informacion del repositorio en tiempo de build
#exec VER    git describe --tags --abbrev=0
#exec COMMIT git rev-parse --short HEAD
#exec BRANCH git rev-parse --abbrev-ref HEAD

build_info:
    db "Version:  VER",    0
    db "Commit:   COMMIT", 0
    db "Branch:   BRANCH", 0
```

### Variables con #set como contadores

```
; generar N entradas con ID autoincremental
#set ID = 0
#array HANDLERS (read, write, seek, flush, close)
#foreach H in HANDLERS
    handler_H:
        mov r0, ID
        call dispatch
        ret
    #set ID++
#endforeach
```

### Asignar IDs de opcode con __ARRAY_FIND__

El token-pasting (`name##VAR`) no sustituye la variable de iteracion dentro
de un identificador compuesto. Usa `__ARRAY_FIND__` para asignar un ID
numerico a cada opcode sin depender de token-pasting:

```
#array OPCODES (NOP, PUSH, POP, MOV, ADD, SUB, HLT)

#define OP_NOP  __ARRAY_FIND__(OPCODES, NOP)    ; -> 0
#define OP_PUSH __ARRAY_FIND__(OPCODES, PUSH)   ; -> 1
#define OP_POP  __ARRAY_FIND__(OPCODES, POP)    ; -> 2
#define OP_MOV  __ARRAY_FIND__(OPCODES, MOV)    ; -> 3
#define OP_ADD  __ARRAY_FIND__(OPCODES, ADD)    ; -> 4
#define OP_SUB  __ARRAY_FIND__(OPCODES, SUB)    ; -> 5
#define OP_HLT  __ARRAY_FIND__(OPCODES, HLT)    ; -> 6

    mov r10, OP_NOP     ; r10 = 0
```

### Verificar invariantes con #assert

```
#define ENTRY_SIZE  8
#define MAX_ENTRIES 256

; la tabla completa debe caber en un segmento de 4 KB
#assert __MUL__(ENTRY_SIZE, MAX_ENTRIES) <= 4096  "tabla demasiado grande"

; ENTRY_SIZE debe ser potencia de 2
#assert __BITTEST__(ENTRY_SIZE, 0) == 0           "ENTRY_SIZE debe ser par"
#assert __LOG2__(ENTRY_SIZE) >= 2                 "ENTRY_SIZE minimo 4 bytes"
```

### Reducir boilerplate con #macro

```
; sin #macro: cada llamada a vio_print_int requiere 3 lineas
    mov r1, 42
    mov r15, 1
    calln @Method("stdlib/native/io/vesta_io:vio_print_int")

; con #macro: una sola linea, el preprocesador expande a las 3 instrucciones
#macro PRINT_INT(val)
    mov r1, val
    mov r15, 1
    calln @Method("stdlib/native/io/vesta_io:vio_print_int")
#endmacro

    PRINT_INT(42)
    PRINT_INT(__MUL__(6, 7))    ; argumento evaluado en preprocesado -> 42
    PRINT_INT(__ADD__(1000, 337))
```

### Construir tablas de offsets con #foreach VAR, IDX

```
#define ENTRY_SIZE 8
#array FIELDS (id, name, type, value)

; tabla de offsets: cada campo a IDX * ENTRY_SIZE bytes del inicio
offsets:
#foreach FIELD, IDX in FIELDS
    dw __MUL__(IDX, ENTRY_SIZE)   ; 0, 8, 16, 24
#endforeach

; codigo: cargar offset del campo N en r1
#foreach FIELD, IDX in FIELDS
    mov r1, __MUL__(IDX, ENTRY_SIZE)
#endforeach
```

### Punto flotante en tiempo de preprocesado

```
; precomputar sqrt(2) en bits IEEE 754
#define F_TWO   __ITOF__(2)
#define F_SQRT2 __FSQRT__(F_TWO)    ; = 6369051672525773*2^-52 aprox

.data
    sqrt2: dq F_SQRT2

; verificar que sqrt(2)^2 = 2 (o muy cercano)
#define F_SQRT2_SQ __FMUL__(F_SQRT2, F_SQRT2)
#if __FLT__(F_SQRT2_SQ, __ITOF__(3)) && __FGT__(F_SQRT2_SQ, __ITOF__(1))
    sqrt2_valid: db "sqrt(2) en rango correcto", 0
#endif
```

---

## Libreria estandar (#import)

La carpeta `include_lib/` contiene los modulos de la stdlib vpp. Se instala
automaticamente junto al ejecutable — no se necesita configuracion manual.

### Modulos disponibles

| Directiva                    | Contenido                                                   |
|------------------------------|-------------------------------------------------------------|
| `#import <vesta>`            | Importa todos los modulos de un tiron                       |
| `#import <vesta/registers>`  | Aliases de registros: `REG_RET`, `REG_PROC`, `REG_ARGC`...  |
| `#import <vesta/io>`         | Constantes E/S, `METHOD_PRINTLN`, `LIB_IO`, `CHAR_NUL`...  |
| `#import <vesta/types>`      | Tamanios (`SIZEOF_QWORD`), rangos (`UINT32_MAX`)...         |
| `#import <vesta/math>`       | Constantes IEEE 754: `F_PI`, `F_E`, `F_SQRT2`, `F_NAN`...  |
| `#import <vesta/platform>`   | `OS_NAME`, `ARCH_NAME`, `PATH_SEP`, `DLL_EXT`...            |

### Ejemplo de uso

```vel
#import <vesta/registers>   // REG_ARG1, REG_PROC, REG_ARGC...
#import <vesta/io>          // METHOD_PRINTLN, CHAR_NUL, LIB_IO...
#import <vesta/platform>    // OS_NAME, ARCH_NAME, ARCH_BITS...

@Import {
    @Method { @Lib(LIB_IO) @Name(FN_PRINTLN) }   // LIB_IO = "stdlib/native/io/vesta_io"
}

code:
    @InitPc(code)
    getproc REG_PROC                    // en lugar de: getproc r14

    mov REG_ARG1, REG_PROC              // r1 = proc
    mov REG_ARG2, @Absolute("all.msg")  // r2 = addr
    mov REG_ARG3, 5                     // r3 = len
    mov REG_ARGC, 3                     // r15 = 3
    calln @Method(METHOD_PRINTLN)       // en lugar del string literal completo
    hlt

data:
    msg db "hello", CHAR_NUL            // CHAR_NUL en lugar de 0x00
    os  db OS_NAME, CHAR_NUL            // "Windows", "Linux" o "macOS"
```

### Macros de METHOD_* (vesta/io)

Los strings de metodo completos para `calln @Method(...)` se construyen
con `__STRCAT__` sobre `LIB_IO` y evitan repetir la ruta de la libreria:

| Macro                  | Expande a                                            |
|------------------------|------------------------------------------------------|
| `METHOD_PRINTLN`       | `"stdlib/native/io/vesta_io:vio_println"`            |
| `METHOD_PRINT_INT`     | `"stdlib/native/io/vesta_io:vio_print_int"`          |
| `METHOD_PRINT_UINT`    | `"stdlib/native/io/vesta_io:vio_print_uint"`         |
| `METHOD_PRINT_HEX`     | `"stdlib/native/io/vesta_io:vio_print_hex"`          |
| `METHOD_PRINT_FLOAT`   | `"stdlib/native/io/vesta_io:vio_print_float"`        |
| `METHOD_FOPEN`         | `"stdlib/native/io/vesta_io:vio_fopen"`              |
| `METHOD_FCLOSE`        | `"stdlib/native/io/vesta_io:vio_fclose"`             |
| `METHOD_FREAD`         | `"stdlib/native/io/vesta_io:vio_fread"`              |
| `METHOD_FWRITE`        | `"stdlib/native/io/vesta_io:vio_fwrite"`             |
| `METHOD_FFLUSH`        | `"stdlib/native/io/vesta_io:vio_fflush"`             |

---

## Uso desde la linea de comandos

```bash
# Preprocesar un archivo y escribir en stdout
vpp archivo.vel

# Preprocesar y guardar en un archivo de salida
vpp archivo.vel -o archivo_pp.vel

# Precargar las macros que predefine un compilador concreto
vpp --predef-from "gcc -dM -E -" archivo.c

# Precargar directivas desde un fichero (sirve para cualquier lenguaje)
vpp --predef plataforma.def archivo.txt

# Resolver __has_builtin y companeros preguntandole al compilador objetivo
vpp --capabilities-from "gcc -E -P -x c" archivo.c

# Recordar entre ejecuciones en otro sitio, o no recordar nada
vpp --cache-dir build/.vpp-cache archivo.c
vpp --no-cache archivo.c

# Definir macros desde la linea de comandos
vpp -DDEBUG -DVERSION=2 archivo.vel

# Quitar una macro que traiga el volcado del compilador
vpp --predef-from "gcc -dM -E -" -U__GNUC__ archivo.c

# Anadir rutas de busqueda para #include
vpp -I ./include -I /usr/local/vesta/include archivo.vel

# Marcador de directiva para un fichero que no se puede tocar
vpp --marker % generado.py

# Lista de dependencias para make o ninja
vpp --deps -I include main.c
vpp -MD -MF build/main.d -I include main.c -o build/main.i

# Leer desde stdin
echo "#define X 1\nX" | vpp --stdin

# Emitir marcadores #line (util para depuracion)
vpp --line-markers archivo.vel -o out.vel

# Dejar el texto sin expandir (solo se atienden las directivas)
vpp --no-expand archivo.txt

# Ver version
vpp --version

# Ayuda
vpp --help
```

---

## Precargar macros de otro compilador

Un preprocesador que quiera tragarse codigo real necesita las macros que el
compilador de destino da por hechas.  La diferencia es enorme: vpp predefine 3
macros y `gcc` predefine 412.  Sin ellas, las cabeceras de la biblioteca
estandar toman ramas equivocadas -- creen que no hay GCC, por ejemplo -- y la
salida no compila aunque el preprocesado no haya dado un solo error.

Se resuelve con dos opciones **genericas**: no hay ningun `--emulate=gcc` que
hornee dentro de vpp el conocimiento de un compilador o de un lenguaje.

```bash
# desde la salida de un comando
vpp --predef-from "gcc -dM -E -" -I/usr/include programa.c

# desde un fichero de directivas escrito a mano
vpp --predef mi_plataforma.def programa.lo-que-sea
```

Que la variante de comando apunte al **binario exacto** no es un detalle: es lo
que permite que convivan varios compiladores y varias versiones en la misma
maquina sin que vpp sepa nada de ninguno.

```bash
vpp --predef-from "gcc-12 -dM -E -"            ...
vpp --predef-from "clang-15 -dM -E -x c++ -"   ...
vpp --predef-from "arm-none-eabi-gcc -dM -E -" ...
```

Y como lo que se carga es simplemente **texto con directivas**, el mecanismo no
esta atado a C.  Para un lenguaje propio basta con un fichero:

```
#define OBJETIVO_ES_64  1
#define NOMBRE_ABI      "sysv"
#define ALINEAR(x)      (((x) + 7) & ~7)
```

**Por que se procesa como fuente y no como una lista `nombre=valor`.**  Un
volcado real trae macros funcion (`#define __glibcxx_assert(cond)`) y valores de
varios tokens (`#define __SIZE_TYPE__ long unsigned int`).  Ninguna de las dos
cosas cabe en un `nombre=valor`, asi que el bloque pasa por el pipeline completo
y solo se conservan las macros; la salida se descarta.

Un `-D` posterior gana al bloque precargado, por ser mas especifico.


---

## Memoria entre ejecuciones

Preguntarle algo a un compilador cuesta lanzar un proceso.  El volcado de
`--predef-from` se pide en CADA invocacion de vpp, asi que compilar N ficheros
son N procesos para obtener exactamente lo mismo; y una unidad de C++ moderna
consulta capacidades decenas de veces.  vpp recuerda ambas cosas entre
ejecuciones:

```bash
# Por omision, en la cache del usuario.  No hay que hacer nada.
vpp --predef-from "gcc -dM -E -" --capabilities-from "gcc -E -P -x c" a.c

# En otro sitio, para una compilacion aislada o reproducible.
vpp --cache-dir build/.vpp-cache ... a.c

# Sin recordar nada, para medir el coste real o descartar la memoria.
vpp --no-cache ... a.c
```

Medido en Windows sobre un fuente con tres consultas: **363 ms en frio, 24 ms en
caliente, ~200 ms sin memoria**.

### Por que la clave no es la orden

Lo obvio seria guardar las respuestas bajo `"gcc -E -P -x c"`.  Eso miente en
dos direcciones: `gcc` y `/usr/bin/gcc` son el mismo compilador con dos cadenas
distintas, y -- lo que de verdad hace dano -- **la misma cadena pasa a
significar otra cosa en cuanto se actualiza el compilador**.  Una memoria con esa
clave devolveria respuestas viejas para siempre, sin dar la cara por ningun
sitio.

La clave es la identidad del BINARIO -- su ruta real, su tamano y su fecha, que
cambian los tres al actualizarlo -- mas la orden entera, porque los flags
cambian la respuesta tanto como el binario: `-x c++` habilita operadores que en
C no existen, y `--target=` cambia lo que contestan los predicados de objetivo.
Si el ejecutable no se puede localizar, **no se recuerda nada**: sin saber a
quien se pregunta no hay forma de notar que ha cambiado.

### Que NO se guarda

- **`__has_include`**, porque pregunta por las rutas de busqueda de quien
  compila, no por el compilador.  Es un hecho del proyecto, no suyo, y
  recordarlo daria respuestas de otro proyecto.
- **Las consultas que fallaron.**  Un fallo pasajero -- el compilador ocupado,
  un PATH a medio poner -- no es una respuesta, y guardarlo lo dejaria escrito
  como una propiedad del compilador: el error sobreviviria a su causa.

### Compilaciones en paralelo

Una compilacion con `-j` lanza muchos vpp a la vez sobre la misma memoria.  No
hay ningun cerrojo y no hace falta: hay un fichero por compilador y se escribe
entero en un temporal que despues se renombra encima, que es atomico en los dos
sistemas.  Nadie lee jamas un fichero a medias.  Si dos procesos se pisan, lo
peor que pasa es que una respuesta se vuelva a preguntar -- cada entrada es una
funcion pura de (compilador, pregunta), asi que dos escritores no pueden
discrepar.

Los ficheros son texto y se pueden mirar a ojo:

```
$ cat ~/.cache/vpp/v1/496235fd9618b9f5.facts
# vpp facts v1
__has_builtin __builtin_expect	1
#defined __has_cpp_attribute	0
```

Para limpiarla, se borra el directorio.  La version del formato va en el nombre
(`v1/`), asi que un cambio de formato deja lo viejo donde esta en lugar de
leerlo mal.


---

## Diagnosticos

Un mensaje cita la linea que lo provoco y senala la columna:

```
t.c:2:1: error: directiva de preprocesador desconocida: #defien
   2 | #defien FOO 1
     | ^
```

Es la diferencia entre un diagnostico que se arregla y uno que se investiga.  Se
resalta con color solo cuando la salida de error va a una terminal -- redirigida
a un fichero, las secuencias ANSI son basura para quien despues lo lea o lo
parsee -- y se respeta `NO_COLOR`.

Si el fuente no esta disponible se da el mensaje de siempre sin adornos.  Pasa
con lo que no viene del disco: la API, la entrada estandar, un `#exec`.

Los tabuladores del original se reproducen en la linea del cursor en lugar de
contarlos como un caracter; con espacios, el cursor acabaria senalando a otro
sitio, que es peor que no ponerlo.
---

## Dependencias para el build

Un build incremental necesita saber que rehacer cuando cambia una cabecera.  vpp
emite la lista en el formato de `cc`, asi que sirve tal cual a make y a ninja:

```bash
# Solo la regla, sin preprocesar a la salida
vpp --deps -I include main.c
main.o: main.c include/a.h include/b.h

# La salida normal Y las dependencias en un fichero
vpp -MD -MF build/main.d -I include main.c -o build/main.i

# Objetivo propio y objetivos ficticios
vpp --deps -MT build/main.o -MP -I include main.c
```

| Opcion | Que hace |
| :----- | :------- |
| `--deps` | Emite solo la regla, sin la salida preprocesada |
| `-MD` | Emite la regla ADEMAS de la salida |
| `-MF <f>` | Escribe la regla en ese fichero en vez de en la salida |
| `-MT <t>` | Nombre del objetivo (por omision, el fichero de salida) |
| `-MP` | Anade una regla vacia por cada dependencia |
| `-MM` | Como `--deps`, dejando fuera las cabeceras de sistema |
| `-MMD` | Como `-MD`, dejando fuera las cabeceras de sistema |

`-MP` esta para que borrar una cabecera no rompa el build: sin esas reglas, make
lee la lista vieja, ve un fichero que ya no esta y se para -- cuando en realidad
ya no hace falta, porque el fuente dejo de incluirlo.  El fuente principal se
queda fuera a proposito: si ESE falta, pararse es lo correcto.

Las rutas son las RESUELTAS, que son las unicas que le sirven a make, y salen
con barras normales aunque sean de Windows, porque en un Makefile la barra
invertida es un escape.  El espacio y el dolar van escapados por lo mismo.

> **Por que `--deps` y no `-M`.**  En cc, `-M` es esto.  En vpp `-M` ya era la
> ruta de busqueda de `#import`, y romperlo habria dejado de funcionar en
> silencio a quien lo usara.  Los demas nombres si son los de cc, de modo que un
> build system los emite tal cual; solo hay que traducir ese.

Comprobado contra `gcc -M`: la salida es identica byte a byte, con y sin `-MP`.

### Cabeceras de sistema

`-isystem <ruta>` declara un directorio como ajeno: se busca DESPUES de las
rutas `-I`, que es el orden de cc, y lo que aparezca en el queda fuera de la
lista que emiten `-MM` y `-MMD`.  Rehacer el build porque cambio una cabecera
del sistema no tiene sentido: si eso pasa, se recompila entero de todos modos.

La regla es **transitiva**: lo que una cabecera de sistema incluya sigue siendo
ajeno aunque lo encuentre por una ruta propia.  Comprobado contra `gcc -MM`,
que da lo mismo.
---

## Uso como biblioteca (C++ API)

```cpp
#include "preprocessor/preprocessor.h"

// uso basico
vpp::Preprocessor pp;
pp.options().predefines.push_back("PLATFORM=vesta");
pp.options().include_paths.push_back("./include");

std::string result = pp.process(source_text, "mi_archivo.vel");

if (pp.diagnostics().has_errors()) {
    pp.diagnostics().print_all(std::cerr);
    return 1;
}

// con resolvedor de includes personalizado (e.g. base de datos virtual)
pp.set_include_resolver([](const std::string& from,
                            const std::string& path,
                            bool is_system) -> std::string {
    return load_from_vfs(path);
});

// procesar un archivo directamente
std::string out = pp.process_file("programa.vel");

// acceder a la tabla de macros y arrays tras el procesado
const vpp::MacroTable& mt = pp.macro_table();
if (mt.has_array("OPCODES")) {
    const auto* items = mt.get_array("OPCODES");
    // items es un vector<string> con los elementos del array
}
```

---

## Uso como biblioteca (ABI en C)

La API C++ de arriba solo sirve para enlazar la biblioteca ESTATICA con el mismo
compilador: `std::string` y `std::function` en la firma no cruzan la frontera de
una DLL/.so entre MSVC, MinGW o versiones distintas de libstdc++.

Para eso esta `preprocessor/vpp_c.h`: handles opacos, `const char*`, codigos de
error y cero excepciones.  Es lo UNICO que exporta la biblioteca compartida, y
por tanto es consumible desde C, Python, Rust, C#, Go, Zig o cualquier lenguaje
con FFI a C.

```c
#include <preprocessor/vpp_c.h>
#include <stdio.h>

int main(void) {
    vpp_preprocessor* pp = vpp_create();
    char* out = NULL;

    vpp_add_define(pp, "N=7");
    vpp_add_include_path(pp, "./include");

    vpp_status st = vpp_process(pp,
        "#define DOBLE(x) ((x)*2)
r = DOBLE(N)
", "u.c", &out);

    if (st == VPP_ERR_DIAGNOSTIC) {
        for (size_t i = 0; i < vpp_diagnostic_count(pp); ++i) {
            vpp_diagnostic d;
            vpp_diagnostic_at(pp, i, &d);
            fprintf(stderr, "%s:%u:%u: %s
", d.file, d.line, d.col, d.message);
        }
    } else {
        printf("%s", out);          /* r = ((7)*2) */
    }

    vpp_string_free(out);           /* SIEMPRE con vpp_string_free, no free */
    vpp_destroy(pp);
    return 0;
}
```

Desde Python, sin compilar nada:

```python
import ctypes
lib = ctypes.CDLL("vpp.dll")            # libvpp.so en Linux
lib.vpp_create.restype = ctypes.c_void_p
lib.vpp_process.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                            ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
pp, out = lib.vpp_create(), ctypes.c_char_p()
lib.vpp_process(pp, b"#define A 1
x = A
", b"m.c", ctypes.byref(out))
print(out.value.decode())
```

### Precargar macros desde el ABI en C

Las mismas tres vias que ofrece la linea de ordenes, para quien embebe la
biblioteca:

```c
/* el texto lo consigue el programa anfitrion como quiera */
vpp_add_predef_text(pp, "#define OBJETIVO 64
#define DOBLE(x) ((x)*2)
");

/* desde un fichero */
vpp_add_predef_file(pp, "plataforma.def");

/* desde la salida de un comando -- OJO: esto LANZA UN PROCESO */
vpp_add_predef_command(pp, "gcc-12 -dM -E -");
```

`vpp_add_predef_text` es la primitiva recomendada al embeber: no toca el sistema
de ficheros ni lanza procesos, asi que el anfitrion mantiene el control de como
obtiene esas macros.  Si esa restriccion no aplica, la variante de comando
ahorra el trabajo intermedio.

---


### Lo que se configura desde el ABI

Todo lo que hace la linea de ordenes:

| Funcion | Equivale a |
| :------ | :--------- |
| `vpp_add_define` / `vpp_add_undefine` | `-D` / `-U` |
| `vpp_add_include_path` / `vpp_add_system_include_path` | `-I` / `-isystem` |
| `vpp_add_import_path` | `-M` |
| `vpp_add_predef_text` / `_file` / `_command` | `--predef` / `--predef-from` |
| `vpp_set_capabilities_command` | `--capabilities-from` |
| `vpp_set_cache_dir` / `vpp_set_use_cache` | `--cache-dir` / `--no-cache` |
| `vpp_set_directive_marker` | `--marker` |
| `vpp_set_comment_syntax` / `vpp_set_quote_syntax` | los ajustes de dialecto |
| `vpp_format_deps` | `--deps` / `-MM` / `-MT` / `-MP` |
| `vpp_user_included_file_count` / `_at` | las dependencias propias |

La declaracion que traiga el fichero (`vpp:marker=...`) GANA sobre lo que se
fije aqui, por ser mas concreta -- igual que gana sobre `--marker`.
### Reglas de propiedad de memoria

- Lo que devuelve `vpp_process` / `vpp_process_file` es **del llamante**: se
  libera con `vpp_string_free` (nunca con `free`, porque la biblioteca puede
  tener otro heap).
- Todo `const char*` de los demas getters es **prestado**: deja de ser valido
  tras el siguiente `vpp_process*` o tras `vpp_destroy`.
- Un handle NO es thread-safe: usa uno por hilo.
- Un handle acumula macros y diagnosticos entre llamadas; para unidades de
  traduccion independientes, usa un handle por unidad.

---

## Construccion

### Requisitos

- CMake 3.16+
- Compilador C++17: GCC 8+, Clang 7+, MSVC 2019+

### Artefactos que se generan

| Artefacto | Target CMake | Para que sirve |
| :-------- | :----------- | :------------- |
| `libvpp.a` (MinGW) / `vpp_static.lib` (MSVC) | `vpp_lib` | Enlace estatico.  Expone la API C++ **y** el ABI en C.  Compilado con `-fPIC`, asi que puede embeberse dentro de un `.so`. |
| `vpp.dll` / `libvpp.so` | `vpp_shared` | Biblioteca compartida.  Exporta **solo** el ABI en C (los simbolos C++ quedan ocultos), lo que la hace consumible desde cualquier compilador y lenguaje. |
| `libvpp.dll.a` (MinGW) / `vpp.lib` (MSVC) | `vpp_shared` | Import library de la DLL. |
| `vpp.def` (solo MinGW) | `vpp_shared` | Lista de exports de la DLL, para fabricar una import library de MSVC (ver abajo). |
| `vpp.exe` / `vpp` | `vpp` | Herramienta de linea de comandos. |
| `vpp.pc` | -- | pkg-config, para consumidores que no usan CMake. |
| `vppConfig.cmake` | -- | Paquete CMake, para `find_package(vpp)`. |

### ABIs de Windows

Una biblioteca estatica **no** es intercambiable entre MSVC y MinGW: distinto
mangling de C++, distinto modelo de excepciones y distinta STL.  Tampoco entre
32 y 64 bits.  No hay conversion posible; la unica salida es compilar una vez
por ABI.  Por eso en Windows las bibliotecas se instalan separadas:

```
include/preprocessor/*.h        <- comunes a todas las ABI
lib/cmake/vpp/vppConfig.cmake   <- comun: elige la ABI segun tu compilador
lib/x64-mingw/  libvpp.a  libvpp.dll.a  vpp.dll  vpp.def  pkgconfig/  cmake/
lib/x86-mingw/  libvpp.a  libvpp.dll.a  vpp.dll  vpp.def  pkgconfig/  cmake/
lib/x64-msvc/   vpp_static.lib  vpp.lib  vpp.dll  pkgconfig/  cmake/
lib/x86-msvc/   vpp_static.lib  vpp.lib  vpp.dll  pkgconfig/  cmake/
```

`find_package(vpp)` deduce con que esta compilando el consumidor y carga la ABI
correcta sin que haya que decirle nada; si esa ABI no esta instalada, falla en
la configuracion con la lista de las que si estan, en lugar de dejar que el
error salga luego como un simbolo no resuelto.  La ABI elegida queda en
`vpp_ABI`.

En Unix no aplica: gcc y clang comparten ABI, y se usa el `lib/` plano de
siempre.

**Cual usar: la dinamica o la estatica.**  No son equivalentes en lo que
exponen.

La biblioteca **dinamica** exporta unicamente el ABI en C (verificado: 23
simbolos, cero simbolos C++, tanto en la DLL de Windows como en la `.so` de
Linux).  Por eso la consume cualquier compilador y cualquier lenguaje, y es la
opcion por defecto para terceros.

La biblioteca **estatica** tiene dos requisitos que la dinamica no.

Primero, aunque la uses solo a traves de `vpp_c.h`, por dentro es C++, asi que
tu proyecto tiene que habilitar ese lenguaje aunque no escribas ni una linea de
C++:

```cmake
project(mi_app LANGUAGES C CXX)      # el CXX es imprescindible con vpp::vpp_static
find_package(vpp 1.0 REQUIRED)
target_link_libraries(mi_app PRIVATE vpp::vpp_static)
```

Sin el, CMake enlaza con el driver de C y el enlace muere con `undefined
reference to operator new`.  Con la dinamica no pasa: lleva la runtime dentro.

Segundo, expone ademas la API C++, y eso ata al consumidor:
`std::string` y `std::function` en la firma obligan a compilar con un toolchain
compatible con el que se construyo -- misma familia de compilador y una version
de la biblioteca estandar compatible.  Enlazar `vpp_static.lib` de MSVC 14.4x
desde otro toolset, o `libvpp.a` de GCC desde clang con otra libstdc++, puede
fallar en el enlace o, peor, compilar y romperse en ejecucion.  Si usas la
estatica desde otro lenguaje o no controlas el compilador del consumidor, usa
solo las funciones de `vpp_c.h`, que no tienen ese problema.

**Consumir la DLL de MinGW desde MSVC.**  Se puede, porque los 23 exports estan
sin decorar y la DLL lleva la runtime de C++ enlazada estaticamente.  O bien con
`LoadLibrary` + `GetProcAddress` directamente, o fabricando la import library:

```bat
lib /def:libd-mingwpp.def /machine:x64 /out:vpp.lib
```

Aun asi, lo natural es instalar el componente `x64-msvc` y dejar que
`find_package` haga su trabajo.

### Opciones

| Opcion | Por defecto | Efecto |
| :----- | :---------- | :----- |
| `VPP_BUILD_STATIC` | `ON` | Construye la biblioteca estatica. |
| `VPP_BUILD_SHARED` | `ON` si es el proyecto raiz | Construye la DLL/.so. |
| `VPP_BUILD_EXE` | `ON` | Construye el ejecutable `vpp`. |
| `VPP_BUILD_TESTS` | `ON` | Construye los tests. |
| `VPP_INSTALL` | `ON` si es el proyecto raiz | Genera las reglas de instalacion y el paquete. |

Como submodulo de otro proyecto, los cuatro valores que dependen de "proyecto
raiz" pasan a `OFF` solos: el padre solo obtiene el `.a` y no paga el coste de
construir ni empaquetar lo demas.


### Construir el ejecutable y los tests

```bash
mkdir -p preprocessor/build
cd preprocessor/build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Solo los tests (sin el ejecutable vpp)

```bash
cmake -DVPP_BUILD_EXE=OFF -DVPP_BUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

### En Windows con MinGW

```bash
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
mingw32-make -j4
```

---

## Instalar y consumir desde otro proyecto

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /donde/quieras
```

Desde CMake:

```cmake
find_package(vpp 1.0 REQUIRED)
target_link_libraries(mi_app PRIVATE vpp::vpp)          # la compartida si existe
# target_link_libraries(mi_app PRIVATE vpp::vpp_static) # forzar la estatica
```

Desde pkg-config:

```bash
gcc mi_app.c $(pkg-config --cflags --libs vpp) -o mi_app
```

La stdlib de macros (`#import <vesta/...>`) se instala junto a la biblioteca; su
ruta se expone como `vpp_INCLUDE_LIB_DIR` en CMake y como `vpp_include_lib` en
el `.pc`, para pasarla como `import_path` cuando embebes el preprocesador.

---

## Generar un instalador

Para una sola ABI (la del compilador con el que configuraste):

```bash
cmake --build build --target installer       # Windows: vpp-<ver>-win64.exe (NSIS)
cmake --build build --target installer-zip   # portable: .zip (Windows) / .tar.gz
```

Para un instalador que traiga **todas las ABI de Windows** a la vez:

```bat
cmakeuild_all_abis.bat --vcvarsall "C:\...\VC\Auxiliary\Buildcvarsall.bat"
```

Compila MinGW x64/x86 y MSVC x64/x86, preinstala cada una en su staging y
genera un unico `.exe` con las cuatro.  Sin `--vcvarsall` se omiten las ABI de
MSVC (avisa, no es un error); `--no-mingw` / `--no-msvc` / `--no-installer`
acotan lo que se hace.

El target `installer` **descarga NSIS solo** (version portable, ~2.3 MB, con
hash fijado) si `makensis` no esta en el sistema: no hay que instalar nada a
mano.  El instalador ofrece estos componentes marcables:

| Componente | Contenido |
| :--------- | :-------- |
| `runtime` (obligatorio) | `vpp.exe` y la stdlib de macros |
| `headers` | cabeceras y el `vppConfig.cmake` que elige la ABI |
| `libs_x64_mingw`, `libs_x86_mingw`, `libs_x64_msvc`, `libs_x86_msvc` | las bibliotecas de cada ABI; marca la de tu compilador |
| `docs` | README y licencia |

Ademas anade `<prefix>in` al PATH (con pagina de eleccion), de modo que `vpp`
queda disponible desde cualquier shell.

### Linux: paquetes nativos

```bash
cmake --build build --target installer-deb   # -> vpp_<ver>_amd64.deb + vpp-dev_<ver>_amd64.deb
cmake --build build --target installer-rpm   # -> vpp-<ver>-1.x86_64.rpm + vpp-devel-...
cmake --build build --target installer-zip   # -> vpp-<ver>-Linux-x86_64.tar.gz
```

El reparto sigue la convencion de las distribuciones: el paquete base lleva lo
que hace falta para EJECUTAR, y el `-dev` lo que hace falta para COMPILAR
contra la biblioteca.

| Paquete | Contenido |
| :------ | :-------- |
| `vpp` / `vpp` (rpm) | `/usr/bin/vpp`, `libvpp.so.1` -> `libvpp.so.1.0.0`, stdlib de macros, docs |
| `vpp-dev` / `vpp-devel` (rpm) | cabeceras, `libvpp.a`, el enlace `libvpp.so`, `vpp.pc` y el paquete CMake.  Depende del paquete base con la misma version |

El generador de RPM necesita `rpmbuild` instalado; el de DEB no necesita nada
fuera de lo que ya trae una Debian.

Las dependencias (`libc6`, `libstdc++6`, `libgcc-s1`) las deduce
`dpkg-shlibdeps` leyendo los binarios, no una lista escrita a mano.

**Que el `.deb` instale en otras distribuciones.**  Un paquete hereda como
dependencia la glibc de la maquina donde se compilo.  Construido en una
distribucion reciente declara algo como `libc6 (>= 2.38)` y `dpkg` lo RECHAZA
en Debian 12 o Ubuntu 22.04, aunque el codigo funcionase ahi perfectamente.  Y
no hay flag que lo evite: glibc 2.38+ redirige `strtoll`/`strtoul`/`strtoull` a
variantes `__isoc23_*` de forma incondicional.

La solucion es compilar contra cabeceras antiguas, que es lo que automatiza:

```bash
sudo cmake/build_linux_compat.sh          # monta un chroot Debian 11 y compila ahi
```

Deja los paquetes en `dist-linux/`.  Medido, el resultado pasa de

```
libc6 (>= 2.38), libgcc-s1 (>= 3.0), libstdc++6 (>= 13.1)
```

a una sola dependencia:

```
libc6 (>= 2.29)
```

La de `libstdc++6` desaparece porque el runtime de C++ se enlaza DENTRO de la
biblioteca, algo que aqui es seguro precisamente porque el version script
garantiza que no sale ningun simbolo C++ de ella.  Con ese suelo, el `.deb`
instala en Debian 11+, Ubuntu 20.04+, RHEL 9+ y cualquier distribucion mas
reciente.

---

## Ejecucion de tests

```bash
cd preprocessor/build
ctest --output-on-failure -V

# o directamente
./vpp_test_lexer
./vpp_test_macros
./vpp_test_conditionals
./vpp_test_includes
./vpp_test_metaprog
./vpp_test_new_features    ; arrays, exec, macros funcion integradas
./vpp_test_variables       ; directiva #set y todos los operadores de asignacion
./vpp_test_float_conv      ; macros de flotantes y conversion numerica
./vpp_test_import          ; #import y rutas de modulos
./vpp_test_c_api           ; el ABI en C
./vpp_test_include_search  ; busqueda de inclusiones e #include_next
./vpp_test_compiler_id     ; identidad del compilador
./vpp_test_facts_cache     ; memoria entre ejecuciones
./vpp_test_dialect         ; marcador de directiva por fichero
./vpp_test_deps            ; lista de dependencias para el build
./vpp_test_diag_render     ; diagnosticos con contexto
./vpp_test_embed           ; #embed y __has_embed
```

### Integracion continua

Cada push a `master` o `develop` compila y pasa la suite completa.  La
matriz no es simetrica por capricho: cada entrada cubre algo que las demas no.

| Trabajo | Que cubre |
| :------ | :-------- |
| `linux-gcc` | ELF, glibc y el version script que recorta los simbolos |
| `macos-clang` | Mach-O y su lista de simbolos exportados |
| `windows-msvc` | La rama MSVC del CMake y su forma de nombrar las bibliotecas |
| `windows-mingw` | El toolchain con el que se construye de verdad en Windows |
| `consumidor-externo` | Enlaza desde un proyecto en C PURO, por `find_package` y por `pkg-config` |

El ultimo merece explicacion: compilar el propio vpp no demuestra que el ABI en
C sirva.  Ese trabajo monta un proyecto que no tiene NADA de C++ -- ni siquiera
el lenguaje habilitado en su `project()` -- y comprueba que enlaza y ejecuta.

Hay ademas un test, `vpp_test_exports`, que verifica que la biblioteca compartida
exporta UNICAMENTE el ABI en C.  Es la invariante que sostiene el diseno, y no
es una precaucion teorica: en ELF ya se colaron nueve plantillas de libstdc++
pese a tener la visibilidad oculta, y por eso hizo falta el version script.

La matriz no es decorativa: los tres huecos de conformidad que quedaban -- el
shim de `__has_builtin`, los operadores que solo existen en un modo y el
reescaneo -- los encontro `macos-clang`, y ninguno se veia en Linux ni en
Windows.  Las cabeceras de cada sistema aprietan por sitios distintos.

---

### Conformidad con el preprocesador de C

Ademas de los tests unitarios hay tres suites cuyo oraculo es un compilador de
verdad.  Existen porque los unitarios comprueban lo que vpp **construyo**, no lo
que el estandar **exige**, y por eso podian estar en verde mientras
`#if defined(X)` estaba roto.

| Suite | Que mide |
| :---- | :------- |
| `vpp_test_conformance` | 46 casos preprocesados con vpp y con `gcc`/`clang`, comparando las salidas |
| `vpp_test_system_headers` | Preprocesa un fuente con cabeceras de C del sistema, **compila** la salida y **ejecuta** el binario |
| `vpp_test_system_headers_cxx` | Lo mismo con `<string>`, `<vector>`, `<algorithm>` e `<iostream>`: es el unico sitio donde `#include_next` y los operadores de prueba de caracteristicas se ejercitan de verdad |

La comparacion es a nivel de **token**, no de linea: dos salidas con los mismos
tokens compilan igual, y vpp diverge de gcc en el reparto por lineas a
proposito, conservando los saltos de los comentarios de bloque para no
descuadrar la numeracion de las etapas siguientes.

Los casos que se sepa que fallan van en `tests/conformance/xfail.txt` con la
explicacion de que falla.  Uno listado que falla no rompe la suite; uno que
**pasa** se reporta como XPASS y si la rompe, para obligar a sacarlo de la lista
al arreglar el bug.  Asi el fichero no puede quedarse mintiendo sobre el estado
real.  Ahora mismo la lista esta vacia: 46/46.

Las tres se registran solo si hay un compilador de referencia; sin el se
omiten en vez de dar un rojo que no dice nada del codigo.

---

### Cobertura de tests

| Suite                  | Que verifica                                                  |
|------------------------|---------------------------------------------------------------|
| `vpp_test_lexer`       | Tokenizacion: directivas, texto, comentarios                  |
| `vpp_test_macros`      | Expansion de macros objeto y funcion, #, ##, variadic         |
| `vpp_test_conditionals`| #if/#ifdef/#ifndef/#elif/#else/#endif, expresiones            |
| `vpp_test_includes`    | #include, #pragma once, includes anidados                     |
| `vpp_test_metaprog`    | #foreach inline, #repeat, __REPEAT_INDEX__, #foreach VAR IDX, #assert, #macro |
| `vpp_test_new_features`| #array, #exec, macros cadenas (__STRLEN__ .. __TRIM__), macros numericas (__MIN__ .. __ALIGN__), aritmetica entera (__ADD__ .. __NEG__), bitwise (__AND__ .. __SAR__), comparaciones (__EQ__ .. __GE__), __ARRAY_FIND__, __ARRAY_JOIN__, __QUOTE__, __UNQUOTE__ |
| `vpp_test_variables`   | #set con =, +=, -=, *=, /=, %=, &=, \|=, ^=, <<=, >>=, ++, -- |
| `vpp_test_float_conv`  | Macros IEEE 754 (__FADD__ .. __FPI__), conversion (__DEC2HEX__ .. __ZEROEXT__) |
| `vpp_test_import`      | #import, rutas de modulos y extensiones del dialecto           |
| `vpp_test_c_api`       | El ABI en C completo: handles, diagnosticos, propiedad de la memoria |
| `vpp_test_include_search` | Busqueda de inclusiones: precedencia de rutas, vecino relativo, `#include_next` |
| `vpp_test_dialect`     | Marcador de directiva declarado en el fichero, y que una errata siga siendo error |
| `vpp_test_deps`        | Lista de dependencias: objetivo, escapado, partido de lineas y objetivos ficticios |
| `vpp_test_diag_render` | Diagnosticos con contexto: cita de la linea, posicion del cursor y tabuladores |
| `vpp_test_embed`       | #embed: parametros, datos binarios y los tres valores de __has_embed |
| `vpp_test_compiler_id` | Identificacion del compilador: extraer el ejecutable, buscarlo por el PATH, y que la huella cambie al cambiar el binario o los flags |
| `vpp_test_facts_cache` | Memoria entre ejecuciones: persistencia, separacion por compilador, fusion de escrituras y lo que se niega a guardar |


---


### Rendimiento

Dos cosas dominaban el coste, y las dos se midieron con VTune antes y despues
(carga: una unidad de C++ con 14 cabeceras estandar, 110782 lineas de salida):

| | reloj | reservas |
| :-- | --: | --: |
| de partida | 1283 ms | 15 773 478 |
| nombres de fichero compartidos | 693 ms | 2 361 047 |
| + inclusion multiple | 310 ms | |
| + menos reservas y menos disco | **246 ms** | 776 470 |

Como referencia, `gcc -E` hace lo mismo en 126 ms: se paso de 10x mas lento a
1,95x.

**Nombres compartidos.**  Cada token lleva su ubicacion, y la ubicacion guardaba
el nombre del fichero por valor.  Una ruta de cabecera pasa de los ochenta
caracteres, no cabe en el buffer pequeno de `std::string` y por tanto reserva
memoria SIEMPRE: al crear el token y otra vez en cada copia.  Ahora el nombre se
interna una vez por fichero y la ubicacion guarda un puntero.

**Inclusion multiple.**  Una cabecera protegida con `#ifndef` no puede producir
nada la segunda vez que se incluye.  Sin esto se abria, leia, tokenizaba y
parseaba entera para que la guarda la dejara inerte: 624 inclusiones para 220
ficheros distintos, con una cabecera leida 60 veces.  Ahora se reconoce la
guarda y se sale sin abrir el fichero, siempre que la macro SIGA definida -- un
`#undef` por medio vuelve a hacer significativo el contenido.



**Menos reservas y menos disco.**  Un identificador que no es macro -- la
inmensa mayoria -- se devolvia dentro de un vector de UN elemento, o sea una
reserva por identificador.  Y del lado del disco: al separar localizar de leer
se acabo buscando dos veces cada fichero que si se lee, la lista de rutas se
recorria consultando el sistema de ficheros en cada inclusion aunque la peticion
fuese identica, el buscador se reconstruia entero cada vez, y se construia un
preprocesador completo por inclusion que no se usaba para nada.
**Identidad de una inclusion.**  Lo que se recuerda de un fichero -- su guarda,
su `#pragma once` -- va indexado por la ruta RESUELTA, no por la escrita.  Un
`#include "vecino.h"` desde dos directorios distintos nombra dos ficheros
distintos, y con la ruta escrita lo recordado de uno se aplicaba al otro.  Por
eso la busqueda separa localizar de leer: hace falta saber CUAL es el fichero
antes de decidir que no hace falta abrirlo.
La salida no cambia, salvo que una cabecera ya incluida deja de aportar sus
lineas en blanco, igual que en gcc.
## Perfilar

Hay una configuracion `Profile`: **Release CON simbolos**, que es la unica que
mide el programa que de verdad se ejecuta.

```bash
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Profile -B cmake-build-profile
cmake --build cmake-build-profile -j8

vtune -collect hotspots -r vtune-r001 -- ./cmake-build-profile/vpp.exe <args>
vtune -report summary  -r vtune-r001
vtune -report hotspots -r vtune-r001 -group-by source-file
```

No vale perfilar sobre Debug ni sobre RelWithDebInfo.  Debug dice QUE codigo
existe, no cuanto pesa: sin meter en linea, cualquier funcion pequena llamada a
menudo copa la lista y se acaba optimizando lo que no toca.  RelWithDebInfo es
otro nivel de optimizacion, es decir otro programa, y por tanto otras
proporciones.  `Profile` parte de los flags de Release y solo anade `-g` y
`-fno-omit-frame-pointer`; el nivel de optimizacion no se toca, y tampoco se
estripa al enlazar, porque `--strip-all` borra justo lo que hace falta para
atribuir cada muestra a una funcion.

Conviene perfilar con la memoria caliente (ver arriba): en frio, lo que se mide
son los procesos del compilador, no el trabajo de vpp.
---

## Estructura del proyecto

```
preprocessor/
+-- CMakeLists.txt           construccion principal
+-- README.md                este archivo
+-- cmake/                   empaquetado, instalador y listas de simbolos exportados
+-- include/
|   +-- preprocessor/
|       +-- pp_token.h        tokens y SourceLocation
|       +-- pp_lexer.h        lexer
|       +-- pp_ast.h          nodos del AST
|       +-- pp_parser.h       parser
|       +-- pp_macro.h        tabla, arrays y motor de expansion de macros
|       +-- pp_evaluator.h    evaluador de expresiones #if
|       +-- pp_include.h      busqueda de #include, #include_next e #import
|       +-- pp_dialect.h       el marcador de directiva que declara el fichero
|       +-- pp_deps.h          lista de dependencias en formato de make
|       +-- pp_name_pool.h     nombres de fichero compartidos entre ubicaciones
|       +-- pp_capabilities.h consulta al compilador objetivo para __has_*
|       +-- pp_system.h        entorno y rutas de plataforma
|       +-- pp_atomic_write.h  escribir un fichero sin que se lea a medias
|       +-- pp_compiler_id.h   identidad del compilador (clave de la memoria)
|       +-- pp_facts_cache.h   memoria de respuestas cortas, por compilador
|       +-- pp_command_cache.h memoria de la salida de un comando
|       +-- pp_diag_render.h    diagnosticos con la linea y el cursor
|       +-- pp_source_map.h     texto de cada fuente, para citarlo
|       +-- pp_diagnostics.h  sistema de errores y advertencias
|       +-- preprocessor.h    clase principal (API en C++)
|       +-- vpp_c.h           ABI en C, estable entre compiladores
+-- src/
|   +-- pp_lexer.cpp
|   +-- pp_parser.cpp
|   +-- pp_macro.cpp          incluye registro de macros funcion integradas
|   +-- pp_evaluator.cpp
|   +-- pp_include.cpp
|   +-- pp_deps.cpp
|   +-- pp_dialect.cpp
|   +-- pp_name_pool.cpp
|   +-- pp_capabilities.cpp
|   +-- pp_system.cpp
|   +-- pp_atomic_write.cpp
|   +-- pp_compiler_id.cpp
|   +-- pp_facts_cache.cpp
|   +-- pp_command_cache.cpp
|   +-- pp_diag_render.cpp
|   +-- pp_source_map.cpp
|   +-- pp_diagnostics.cpp
|   +-- preprocessor.cpp      eval_array_def, eval_exec, eval_set
|   +-- vpp_c.cpp             implementacion del ABI en C
|   +-- main.cpp              punto de entrada del ejecutable vpp
+-- tests/
|   +-- CMakeLists.txt
|   +-- test_lexer.cpp
|   +-- test_macros.cpp
|   +-- test_conditionals.cpp
|   +-- test_includes.cpp
|   +-- test_metaprog.cpp
|   +-- test_new_features.cpp
|   +-- test_variables.cpp        tests de #set
|   +-- test_float_conv.cpp       tests de macros IEEE 754 y conversion
|   +-- test_embed.cpp             tests de #embed
|   +-- test_import.cpp           tests de #import
|   +-- test_c_api.cpp            tests del ABI en C
|   +-- test_include_search.cpp   tests de la busqueda de inclusiones
|   +-- test_deps.cpp             tests de la lista de dependencias
|   +-- test_diag_render.cpp      tests de los diagnosticos con contexto
|   +-- test_dialect.cpp          tests del marcador de directiva
|   +-- test_compiler_id.cpp      tests de la identidad del compilador
|   +-- test_facts_cache.cpp      tests de la memoria entre ejecuciones
|   +-- check_exports.cmake       la biblioteca compartida solo exporta vpp_*
|   +-- conformance/              suites contra un compilador de verdad
|       +-- cases/                corpus de casos del estandar de C
|       +-- xfail.txt             fallos conocidos (vacio)
|       +-- run_conformance.cmake
|       +-- run_system_headers.cmake
+-- examples/
    +-- 01_hello_macros.vel       macros basicas y expresiones
    +-- 02_variables.vel          directiva #set con operadores compuestos
    +-- 03_arrays_foreach.vel     arrays y bucles #foreach
    +-- 04_string_macros.vel      macros de cadenas __STRLEN__ .. __TRIM__
    +-- 05_numeric_macros.vel     macros numericas, bits y conversion de bases
    +-- 06_float_macros.vel       macros IEEE 754 de punto flotante
    +-- 07_build_system.vel       vpp como sistema de build con #exec
    +-- 08_metaprogramming.vel       generacion de tablas con #repeat y #foreach
    +-- 09_conditional_platform.vel  configuracion portable por plataforma
    +-- 10_full_program.vel          programa completo: interprete de VM compacto
    +-- 11_quote_unquote.vel         __QUOTE__ y __UNQUOTE__: tokens <-> cadenas
    +-- 12_array_join.vel            __ARRAY_JOIN__: listas de texto precalculadas
    +-- 13_assert.vel                #assert: invariantes de compilacion
    +-- 14_foreach_index.vel         #foreach VAR, IDX: bucle con indice numerico
    +-- 15_macro_codegen.vel         #macro...#endmacro: macros multilinea
    +-- 16_vesta_stdlib.vel          uso completo de #import <vesta> y sus modulos
```

---

## Licencia

**VMProject — Licencia de Uso**
Copyright (c) 2026 David Lopez T. (Castilla y Leon, Espana)
Todos los derechos reservados para uso comercial.

Uso no comercial libre. Consultar `../LICENSE.md` para los terminos completos.
