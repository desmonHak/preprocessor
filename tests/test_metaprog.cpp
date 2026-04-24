/**
 * @file test_metaprog.cpp
 * @brief Tests de directivas de metaprogramacion (#foreach, #repeat) del preprocesador vpp.
 */

#include "preprocessor/preprocessor.h"
#include <iostream>
#include <string>

using namespace vpp;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, name) do { \
    if (!(cond)) { \
        std::cerr << "FALLO: " << name << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        ++tests_failed; \
    } else { \
        std::cout << "OK:    " << name << '\n'; \
        ++tests_passed; \
    } \
} while(0)

static bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

static size_t count_occurrences(const std::string& h, const std::string& n) {
    size_t count = 0, pos = 0;
    while ((pos = h.find(n, pos)) != std::string::npos) { ++count; ++pos; }
    return count;
}

static std::string pp(const std::string& src) {
    Preprocessor pre([](const Diagnostic&){});
    return pre.process(src, "<test>");
}

/* --- tests de #foreach ---------------------------------------------------- */

static void test_foreach_basic() {
    auto out = pp(
        "#foreach ITEM in (a, b, c)\n"
        "item: ITEM\n"
        "#endforeach\n");
    CHECK(contains(out, "item: a"), "#foreach expande el primer elemento");
    CHECK(contains(out, "item: b"), "#foreach expande el segundo elemento");
    CHECK(contains(out, "item: c"), "#foreach expande el tercer elemento");
}

static void test_foreach_single_item() {
    auto out = pp(
        "#foreach X in (only)\n"
        "X\n"
        "#endforeach\n");
    CHECK(contains(out, "only"), "#foreach con un solo elemento");
}

static void test_foreach_empty_list() {
    auto out = pp(
        "#foreach X in ()\n"
        "never\n"
        "#endforeach\n"
        "after\n");
    CHECK(!contains(out, "never"), "#foreach con lista vacia no genera cuerpo");
    CHECK(contains(out, "after"), "#foreach con lista vacia no omite el texto posterior");
}

static void test_foreach_with_macro() {
    // la variable del foreach puede usarse como argumento de macro
    auto out = pp(
        "#define WRAP(x) [x]\n"
        "#foreach V in (one, two)\n"
        "WRAP(V)\n"
        "#endforeach\n");
    CHECK(contains(out, "[one]"), "#foreach variable se expande como argumento de macro");
    CHECK(contains(out, "[two]"), "#foreach segunda iteracion");
}

static void test_foreach_var_not_defined_after() {
    // la variable de iteracion no debe estar disponible fuera del #foreach
    auto out = pp(
        "#foreach T in (hello)\n"
        "inside: T\n"
        "#endforeach\n"
        "outside: T\n");
    CHECK(contains(out, "inside: hello"), "#foreach variable visible dentro");
    // T debe aparecer literalmente fuera (no expandido)
    CHECK(contains(out, "outside: T"), "#foreach variable no visible fuera");
}

static void test_foreach_numbers() {
    auto out = pp(
        "#foreach N in (1, 2, 3)\n"
        "N\n"
        "#endforeach\n");
    CHECK(contains(out, "1") && contains(out, "2") && contains(out, "3"),
          "#foreach con numeros");
}

/* --- tests de #repeat ----------------------------------------------------- */

static void test_repeat_basic() {
    auto out = pp(
        "#repeat 3\n"
        "line\n"
        "#endrepeat\n");
    size_t count = count_occurrences(out, "line");
    CHECK(count == 3, "#repeat 3 genera exactamente 3 repeticiones");
}

static void test_repeat_zero() {
    auto out = pp(
        "#repeat 0\n"
        "never\n"
        "#endrepeat\n"
        "after\n");
    CHECK(!contains(out, "never"), "#repeat 0 no genera ninguna repeticion");
    CHECK(contains(out, "after"), "#repeat 0 no omite el texto posterior");
}

static void test_repeat_one() {
    auto out = pp(
        "#repeat 1\n"
        "once\n"
        "#endrepeat\n");
    size_t count = count_occurrences(out, "once");
    CHECK(count == 1, "#repeat 1 genera exactamente una repeticion");
}

static void test_repeat_index_macro() {
    auto out = pp(
        "#repeat 3\n"
        "idx:__REPEAT_INDEX__\n"
        "#endrepeat\n");
    CHECK(contains(out, "idx:0"), "__REPEAT_INDEX__ vale 0 en la primera iteracion");
    CHECK(contains(out, "idx:1"), "__REPEAT_INDEX__ vale 1 en la segunda iteracion");
    CHECK(contains(out, "idx:2"), "__REPEAT_INDEX__ vale 2 en la tercera iteracion");
}

static void test_repeat_with_define_expression() {
    auto out = pp(
        "#define COUNT 4\n"
        "#repeat COUNT\n"
        "x\n"
        "#endrepeat\n");
    size_t count = count_occurrences(out, "x");
    CHECK(count == 4, "#repeat con expresion macro (COUNT=4)");
}

static void test_repeat_index_not_defined_after() {
    // __REPEAT_INDEX__ no debe estar definido fuera del #repeat
    auto out = pp(
        "#repeat 1\n"
        "inside\n"
        "#endrepeat\n"
        "outside:__REPEAT_INDEX__\n");
    // __REPEAT_INDEX__ debe aparecer literalmente fuera del bloque
    CHECK(contains(out, "outside:__REPEAT_INDEX__"),
          "__REPEAT_INDEX__ no visible fuera de #repeat");
}

/* --- tests combinados ----------------------------------------------------- */

static void test_foreach_inside_ifdef() {
    auto out = pp(
        "#define ENABLED\n"
        "#ifdef ENABLED\n"
        "#foreach X in (alpha, beta)\n"
        "X\n"
        "#endforeach\n"
        "#endif\n");
    CHECK(contains(out, "alpha") && contains(out, "beta"),
          "#foreach dentro de #ifdef funciona");
}

static void test_repeat_inside_ifdef() {
    auto out = pp(
        "#define DO_REPEAT\n"
        "#ifdef DO_REPEAT\n"
        "#repeat 2\n"
        "rep\n"
        "#endrepeat\n"
        "#endif\n");
    size_t count = count_occurrences(out, "rep");
    CHECK(count == 2, "#repeat dentro de #ifdef funciona");
}

static void test_counter_macro() {
    // __COUNTER__ debe incrementarse en cada expansion
    // nota: la macro se actualiza por el evaluador; verificamos que existe
    auto out = pp(
        "#ifdef __COUNTER__\ncounter_exists\n#endif\n");
    CHECK(contains(out, "counter_exists"), "__COUNTER__ esta predefinido");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de metaprogramacion del preprocesador vpp ===\n";

    test_foreach_basic();
    test_foreach_single_item();
    test_foreach_empty_list();
    test_foreach_with_macro();
    test_foreach_var_not_defined_after();
    test_foreach_numbers();
    test_repeat_basic();
    test_repeat_zero();
    test_repeat_one();
    test_repeat_index_macro();
    test_repeat_with_define_expression();
    test_repeat_index_not_defined_after();
    test_foreach_inside_ifdef();
    test_repeat_inside_ifdef();
    test_counter_macro();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
