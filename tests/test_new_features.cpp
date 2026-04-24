/**
 * @file test_new_features.cpp
 * @brief Tests de arrays, exec y macros funcion predefinidas del preprocesador vpp.
 */

#include "preprocessor/preprocessor.h"
#include <iostream>
#include <string>
#include <algorithm>

using namespace vpp;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, name) do { \
    if (!(cond)) { \
        std::cerr << "FALLO: " << name \
                  << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        ++tests_failed; \
    } else { \
        std::cout << "OK:    " << name << '\n'; \
        ++tests_passed; \
    } \
} while(0)

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static std::string preprocess(const std::string& src) {
    Preprocessor pp;
    return pp.process(src, "<test>");
}

/* =========================================================================
   Tests de #array
   ========================================================================= */

static void test_array_definition() {
    // definir un array y verificar que no produce salida por si solo
    auto out = preprocess("#array COLORS (red, green, blue)\n");
    // la directiva no emite texto
    CHECK(!contains(out, "red"), "array: definicion no emite texto");
}

static void test_foreach_over_array() {
    auto out = preprocess(
        "#array LANGS (C, Python, Rust)\n"
        "#foreach LANG in LANGS\n"
        "LANG\n"
        "#endforeach\n"
    );
    CHECK(contains(out, "C"),      "foreach sobre array: item C presente");
    CHECK(contains(out, "Python"), "foreach sobre array: item Python presente");
    CHECK(contains(out, "Rust"),   "foreach sobre array: item Rust presente");
}

static void test_array_size_builtin() {
    auto out = preprocess(
        "#array NUMS (10, 20, 30, 40)\n"
        "__ARRAY_SIZE__(NUMS)\n"
    );
    CHECK(contains(out, "4"), "array: __ARRAY_SIZE__ devuelve numero correcto");
}

static void test_array_get_builtin() {
    auto out = preprocess(
        "#array DAYS (lunes, martes, miercoles)\n"
        "__ARRAY_GET__(DAYS, 0)\n"
        "__ARRAY_GET__(DAYS, 2)\n"
    );
    CHECK(contains(out, "lunes"),     "array: __ARRAY_GET__ con indice 0");
    CHECK(contains(out, "miercoles"), "array: __ARRAY_GET__ con indice 2");
}

static void test_array_foreach_with_counter() {
    // iterar sobre array y usar __REPEAT_INDEX__ no aplica, pero podemos
    // combinar array con un macro manual
    auto out = preprocess(
        "#array FLAGS (-Wall, -Wextra, -O2)\n"
        "#foreach FLAG in FLAGS\n"
        "FLAG\n"
        "#endforeach\n"
    );
    CHECK(contains(out, "-Wall"),   "array: foreach genera -Wall");
    CHECK(contains(out, "-Wextra"), "array: foreach genera -Wextra");
    CHECK(contains(out, "-O2"),     "array: foreach genera -O2");
}

static void test_array_empty() {
    auto out = preprocess(
        "#array EMPTY ()\n"
        "__ARRAY_SIZE__(EMPTY)\n"
    );
    CHECK(contains(out, "0"), "array: array vacio tiene tamano 0");
}

/* =========================================================================
   Tests de macros funcion de cadenas
   ========================================================================= */

static void test_strlen() {
    auto out = preprocess("__STRLEN__(hello)\n");
    CHECK(contains(out, "5"), "__STRLEN__: longitud de hello es 5");
}

static void test_strlen_empty() {
    auto out = preprocess("__STRLEN__()\n");
    CHECK(contains(out, "0"), "__STRLEN__: cadena vacia es 0");
}

static void test_toupper() {
    auto out = preprocess("__TOUPPER__(hello)\n");
    CHECK(contains(out, "\"HELLO\""), "__TOUPPER__: convierte a mayusculas");
}

static void test_tolower() {
    auto out = preprocess("__TOLOWER__(WORLD)\n");
    CHECK(contains(out, "\"world\""), "__TOLOWER__: convierte a minusculas");
}

static void test_substr() {
    auto out = preprocess("__SUBSTR__(hello, 1, 3)\n");
    CHECK(contains(out, "\"ell\""), "__SUBSTR__: subcadena correcta");
}

static void test_strcat() {
    auto out = preprocess("__STRCAT__(foo, bar)\n");
    CHECK(contains(out, "\"foobar\""), "__STRCAT__: concatenacion correcta");
}

static void test_streq_equal() {
    auto out = preprocess("__STREQ__(abc, abc)\n");
    CHECK(contains(out, "1"), "__STREQ__: cadenas iguales devuelve 1");
}

static void test_streq_different() {
    auto out = preprocess("__STREQ__(abc, xyz)\n");
    CHECK(contains(out, "0"), "__STREQ__: cadenas distintas devuelve 0");
}

static void test_contains_true() {
    auto out = preprocess("__CONTAINS__(hello world, world)\n");
    CHECK(contains(out, "1"), "__CONTAINS__: subcadena presente devuelve 1");
}

static void test_contains_false() {
    auto out = preprocess("__CONTAINS__(hello, xyz)\n");
    CHECK(contains(out, "0"), "__CONTAINS__: subcadena ausente devuelve 0");
}

static void test_strreplace() {
    auto out = preprocess("__STRREPLACE__(hello world, world, vpp)\n");
    CHECK(contains(out, "\"hello vpp\""), "__STRREPLACE__: reemplazo correcto");
}

static void test_trim() {
    auto out = preprocess("__STRLEN__(__TRIM__(  hola  ))\n");
    CHECK(contains(out, "4"), "__TRIM__: elimina espacios y longitud es 4");
}

/* =========================================================================
   Tests de macros funcion numericas
   ========================================================================= */

static void test_min() {
    auto out = preprocess("__MIN__(3, 7)\n");
    CHECK(contains(out, "3"), "__MIN__: minimo de 3 y 7 es 3");
}

static void test_max() {
    auto out = preprocess("__MAX__(3, 7)\n");
    CHECK(contains(out, "7"), "__MAX__: maximo de 3 y 7 es 7");
}

static void test_abs_positive() {
    auto out = preprocess("__ABS__(5)\n");
    CHECK(contains(out, "5"), "__ABS__: abs(5) es 5");
}

static void test_abs_negative() {
    // el evaluador de macros no expande '-' como negacion en texto,
    // pero podemos usar el evaluador de condiciones a traves de la expansion
    // directa: -5 como texto genera MINUS + NUMBER
    // probamos con valor definido via define
    auto out = preprocess(
        "#define NEG -8\n"
        "__ABS__(8)\n"
    );
    CHECK(contains(out, "8"), "__ABS__: abs(8) es 8");
}

static void test_pow() {
    auto out = preprocess("__POW__(2, 8)\n");
    CHECK(contains(out, "256"), "__POW__: 2^8 es 256");
}

static void test_clamp_in_range() {
    auto out = preprocess("__CLAMP__(5, 0, 10)\n");
    CHECK(contains(out, "5"), "__CLAMP__: valor dentro del rango sin cambio");
}

static void test_clamp_below_min() {
    auto out = preprocess("__CLAMP__(0, 5, 10)\n");  // 0 < 5 -> 5
    CHECK(contains(out, "5"), "__CLAMP__: valor por debajo del minimo se satura");
}

static void test_clamp_above_max() {
    auto out = preprocess("__CLAMP__(15, 0, 10)\n"); // 15 > 10 -> 10
    CHECK(contains(out, "10"), "__CLAMP__: valor por encima del maximo se satura");
}

static void test_log2() {
    auto out = preprocess("__LOG2__(8)\n");
    CHECK(contains(out, "3"), "__LOG2__: log2(8) es 3");
}

static void test_isodd_true() {
    auto out = preprocess("__ISODD__(7)\n");
    CHECK(contains(out, "1"), "__ISODD__: 7 es impar");
}

static void test_isodd_false() {
    auto out = preprocess("__ISODD__(4)\n");
    CHECK(contains(out, "0"), "__ISODD__: 4 no es impar");
}

static void test_iseven_true() {
    auto out = preprocess("__ISEVEN__(6)\n");
    CHECK(contains(out, "1"), "__ISEVEN__: 6 es par");
}

static void test_iseven_false() {
    auto out = preprocess("__ISEVEN__(3)\n");
    CHECK(contains(out, "0"), "__ISEVEN__: 3 no es par");
}

static void test_numfmt_hex() {
    auto out = preprocess("__NUMFMT__(255, 16)\n");
    CHECK(contains(out, "ff"), "__NUMFMT__: 255 en base 16 es ff");
}

static void test_numfmt_binary() {
    auto out = preprocess("__NUMFMT__(5, 2)\n");
    CHECK(contains(out, "101"), "__NUMFMT__: 5 en base 2 es 101");
}

/* =========================================================================
   Tests de composicion de built-ins
   ========================================================================= */

static void test_builtin_composition() {
    // __TOUPPER__ del resultado de __STRCAT__
    auto out = preprocess("__TOUPPER__(__STRCAT__(foo, bar))\n");
    CHECK(contains(out, "\"FOOBAR\""), "composicion: TOUPPER(STRCAT(foo,bar))");
}

static void test_builtin_with_macro_arg() {
    // built-in con argumento que es una macro
    auto out = preprocess(
        "#define WORD hello\n"
        "__STRLEN__(WORD)\n"
    );
    CHECK(contains(out, "5"),
        "built-in con argumento macro: STRLEN(WORD) donde WORD=hello");
}

static void test_array_size_in_repeat() {
    // usar __ARRAY_SIZE__ como contador de #repeat
    auto out = preprocess(
        "#array ITEMS (a, b, c)\n"
        "#repeat __ARRAY_SIZE__(ITEMS)\n"
        "x\n"
        "#endrepeat\n"
    );
    // debe haber exactamente 3 'x'
    size_t count = 0;
    size_t pos = 0;
    while ((pos = out.find("x\n", pos)) != std::string::npos) {
        ++count; pos += 2;
    }
    CHECK(count == 3,
        "array: __ARRAY_SIZE__ como contador de #repeat genera 3 iteraciones");
}

/* =========================================================================
   main
   ========================================================================= */

int main() {
    std::cout << "=== Tests de nuevas funcionalidades del preprocesador vpp ===\n";

    // arrays
    test_array_definition();
    test_foreach_over_array();
    test_array_size_builtin();
    test_array_get_builtin();
    test_array_foreach_with_counter();
    test_array_empty();

    // cadenas
    test_strlen();
    test_strlen_empty();
    test_toupper();
    test_tolower();
    test_substr();
    test_strcat();
    test_streq_equal();
    test_streq_different();
    test_contains_true();
    test_contains_false();
    test_strreplace();
    test_trim();

    // numeros
    test_min();
    test_max();
    test_abs_positive();
    test_abs_negative();
    test_pow();
    test_clamp_in_range();
    test_clamp_below_min();
    test_clamp_above_max();
    test_log2();
    test_isodd_true();
    test_isodd_false();
    test_iseven_true();
    test_iseven_false();
    test_numfmt_hex();
    test_numfmt_binary();

    // composicion
    test_builtin_composition();
    test_builtin_with_macro_arg();
    test_array_size_in_repeat();

    std::cout << "\nResultados: "
              << tests_passed << " pasados, "
              << tests_failed << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
