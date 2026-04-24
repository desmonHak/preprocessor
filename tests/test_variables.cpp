/**
 * @file test_variables.cpp
 * @brief Tests de la directiva #set (variables) del preprocesador vpp.
 */

#include "preprocessor/preprocessor.h"
#include <iostream>
#include <string>

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
   Asignacion simple
   ========================================================================= */

static void test_set_integer() {
    auto out = preprocess(
        "#set X = 42\n"
        "X\n"
    );
    CHECK(contains(out, "42"), "#set: asignacion de entero");
}

static void test_set_expression() {
    auto out = preprocess(
        "#set A = 3 + 4 * 2\n"
        "A\n"
    );
    CHECK(contains(out, "11"), "#set: asignacion de expresion (3+4*2=11)");
}

static void test_set_string() {
    auto out = preprocess(
        "#set MSG = \"hola\"\n"
        "MSG\n"
    );
    CHECK(contains(out, "\"hola\""), "#set: asignacion de cadena");
}

static void test_set_from_macro() {
    auto out = preprocess(
        "#define BASE 10\n"
        "#set X = BASE * 3\n"
        "X\n"
    );
    CHECK(contains(out, "30"), "#set: asignacion desde macro (BASE*3=30)");
}

/* =========================================================================
   Operadores compuestos aritmeticos
   ========================================================================= */

static void test_set_add_assign() {
    auto out = preprocess(
        "#set N = 10\n"
        "#set N += 5\n"
        "N\n"
    );
    CHECK(contains(out, "15"), "#set +=: 10+5=15");
}

static void test_set_sub_assign() {
    auto out = preprocess(
        "#set N = 20\n"
        "#set N -= 7\n"
        "N\n"
    );
    CHECK(contains(out, "13"), "#set -=: 20-7=13");
}

static void test_set_mul_assign() {
    auto out = preprocess(
        "#set N = 6\n"
        "#set N *= 7\n"
        "N\n"
    );
    CHECK(contains(out, "42"), "#set *=: 6*7=42");
}

static void test_set_div_assign() {
    auto out = preprocess(
        "#set N = 100\n"
        "#set N /= 4\n"
        "N\n"
    );
    CHECK(contains(out, "25"), "#set /=: 100/4=25");
}

static void test_set_mod_assign() {
    auto out = preprocess(
        "#set N = 17\n"
        "#set N %= 5\n"
        "N\n"
    );
    CHECK(contains(out, "2"), "#set %=: 17%5=2");
}

/* =========================================================================
   Operadores compuestos a nivel de bits
   ========================================================================= */

static void test_set_and_assign() {
    auto out = preprocess(
        "#set N = 0xFF\n"
        "#set N &= 0x0F\n"
        "N\n"
    );
    CHECK(contains(out, "15"), "#set &=: 0xFF & 0x0F = 15");
}

static void test_set_or_assign() {
    auto out = preprocess(
        "#set N = 0x01\n"
        "#set N |= 0x10\n"
        "N\n"
    );
    CHECK(contains(out, "17"), "#set |=: 0x01 | 0x10 = 17");
}

static void test_set_xor_assign() {
    auto out = preprocess(
        "#set N = 0xFF\n"
        "#set N ^= 0x0F\n"
        "N\n"
    );
    CHECK(contains(out, "240"), "#set ^=: 0xFF ^ 0x0F = 240");
}

static void test_set_shl_assign() {
    auto out = preprocess(
        "#set N = 1\n"
        "#set N <<= 4\n"
        "N\n"
    );
    CHECK(contains(out, "16"), "#set <<=: 1 << 4 = 16");
}

static void test_set_shr_assign() {
    auto out = preprocess(
        "#set N = 256\n"
        "#set N >>= 3\n"
        "N\n"
    );
    CHECK(contains(out, "32"), "#set >>=: 256 >> 3 = 32");
}

/* =========================================================================
   Incremento / decremento
   ========================================================================= */

static void test_set_increment() {
    auto out = preprocess(
        "#set I = 0\n"
        "#set I++\n"
        "#set I++\n"
        "#set I++\n"
        "I\n"
    );
    CHECK(contains(out, "3"), "#set ++: tres incrementos desde 0 dan 3");
}

static void test_set_decrement() {
    auto out = preprocess(
        "#set I = 10\n"
        "#set I--\n"
        "#set I--\n"
        "I\n"
    );
    CHECK(contains(out, "8"), "#set --: dos decrementos desde 10 dan 8");
}

/* =========================================================================
   Uso en condicionales
   ========================================================================= */

static void test_set_in_if() {
    auto out = preprocess(
        "#set FLAG = 1\n"
        "#if FLAG\n"
        "activado\n"
        "#endif\n"
    );
    CHECK(contains(out, "activado"), "#set en #if: FLAG=1 activa el bloque");
}

static void test_set_loop_counter() {
    // simular un contador con #repeat usando #set
    auto out = preprocess(
        "#set CNT = 0\n"
        "#repeat 5\n"
        "#set CNT++\n"
        "#endrepeat\n"
        "CNT\n"
    );
    CHECK(contains(out, "5"), "#set contador con #repeat: 5 iteraciones -> 5");
}

static void test_set_no_redefinition_warning() {
    // las variables #set no deben generar advertencia al redefinirse
    Preprocessor pp;
    std::string out = pp.process(
        "#set X = 1\n"
        "#set X = 2\n"
        "#set X = 3\n"
        "X\n", "<test>");
    CHECK(!pp.diagnostics().has_errors(), "#set: sin errores al redefinir variable");
    CHECK(contains(out, "3"), "#set: ultimo valor tras varias asignaciones es 3");
}

/* =========================================================================
   main
   ========================================================================= */

int main() {
    std::cout << "=== Tests de variables (#set) del preprocesador vpp ===\n";

    test_set_integer();
    test_set_expression();
    test_set_string();
    test_set_from_macro();

    test_set_add_assign();
    test_set_sub_assign();
    test_set_mul_assign();
    test_set_div_assign();
    test_set_mod_assign();

    test_set_and_assign();
    test_set_or_assign();
    test_set_xor_assign();
    test_set_shl_assign();
    test_set_shr_assign();

    test_set_increment();
    test_set_decrement();

    test_set_in_if();
    test_set_loop_counter();
    test_set_no_redefinition_warning();

    std::cout << "\nResultados: "
              << tests_passed << " pasados, "
              << tests_failed << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
