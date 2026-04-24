/**
 * @file test_conditionals.cpp
 * @brief Tests de directivas condicionales del preprocesador vpp.
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

static std::string pp(const std::string& src) {
    Preprocessor pre([](const Diagnostic&){});
    return pre.process(src, "<test>");
}

/* --- tests ---------------------------------------------------------------- */

static void test_if_true() {
    auto out = pp("#if 1\ntrue_branch\n#endif\n");
    CHECK(contains(out, "true_branch"), "#if 1 incluye la rama");
}

static void test_if_false() {
    auto out = pp("#if 0\nfalse_branch\n#endif\n");
    CHECK(!contains(out, "false_branch"), "#if 0 excluye la rama");
}

static void test_if_else_true() {
    auto out = pp("#if 1\nyes\n#else\nno\n#endif\n");
    CHECK(contains(out, "yes") && !contains(out, "no"),
          "#if 1 toma la rama then, no la else");
}

static void test_if_else_false() {
    auto out = pp("#if 0\nyes\n#else\nno\n#endif\n");
    CHECK(!contains(out, "yes") && contains(out, "no"),
          "#if 0 toma la rama else");
}

static void test_ifdef_defined() {
    auto out = pp("#define MY_FLAG\n#ifdef MY_FLAG\ndefined\n#endif\n");
    CHECK(contains(out, "defined"), "#ifdef con macro definida incluye la rama");
}

static void test_ifdef_undefined() {
    auto out = pp("#ifdef UNDEFINED_MACRO\ndefined\n#endif\n");
    CHECK(!contains(out, "defined"), "#ifdef con macro no definida excluye la rama");
}

static void test_ifndef_defined() {
    auto out = pp("#define PRESENT\n#ifndef PRESENT\nno\n#endif\n");
    CHECK(!contains(out, "no"), "#ifndef con macro definida excluye la rama");
}

static void test_ifndef_undefined() {
    auto out = pp("#ifndef ABSENT\nyes\n#endif\n");
    CHECK(contains(out, "yes"), "#ifndef con macro no definida incluye la rama");
}

static void test_elif_chain() {
    auto out = pp(
        "#define LEVEL 2\n"
        "#if LEVEL == 1\nlevel1\n"
        "#elif LEVEL == 2\nlevel2\n"
        "#elif LEVEL == 3\nlevel3\n"
        "#else\nlevelX\n"
        "#endif\n");
    CHECK(contains(out, "level2"), "#elif selecciona la rama correcta");
    CHECK(!contains(out, "level1") && !contains(out, "level3"),
          "#elif no incluye ramas incorrectas");
}

static void test_nested_if() {
    auto out = pp(
        "#define A\n"
        "#define B\n"
        "#ifdef A\n"
        "  #ifdef B\n"
        "  AB\n"
        "  #endif\n"
        "#endif\n");
    CHECK(contains(out, "AB"), "#if anidados funcionan correctamente");
}

static void test_defined_operator() {
    auto out = pp(
        "#define X\n"
        "#if defined(X) && !defined(Y)\n"
        "correcto\n"
        "#else\n"
        "incorrecto\n"
        "#endif\n");
    CHECK(contains(out, "correcto"), "operador defined() en expresion #if");
}

static void test_expression_arithmetic() {
    auto out = pp("#if 2 + 3 == 5\nOK\n#else\nFAIL\n#endif\n");
    CHECK(contains(out, "OK"), "expresion aritmetica en #if");
}

static void test_expression_comparison() {
    auto out = pp("#if 10 > 5\ngreater\n#else\nless\n#endif\n");
    CHECK(contains(out, "greater"), "comparacion en #if");
}

static void test_expression_logical_and() {
    auto out = pp("#if 1 && 1\nboth\n#else\nnot_both\n#endif\n");
    CHECK(contains(out, "both"), "AND logico en #if");
}

static void test_expression_logical_or() {
    auto out = pp("#if 0 || 1\none_true\n#else\nnone_true\n#endif\n");
    CHECK(contains(out, "one_true"), "OR logico en #if");
}

static void test_expression_ternary() {
    auto out = pp("#if (1 ? 2 : 3) == 2\nyes\n#else\nno\n#endif\n");
    CHECK(contains(out, "yes"), "operador ternario en #if");
}

static void test_expression_bitwise() {
    auto out = pp("#if (0xF0 & 0x0F) == 0\nzero\n#else\nnothing\n#endif\n");
    CHECK(contains(out, "zero"), "AND de bits en #if");
}

static void test_expression_shift() {
    auto out = pp("#if (1 << 4) == 16\nyes\n#else\nno\n#endif\n");
    CHECK(contains(out, "yes"), "desplazamiento de bits en #if");
}

static void test_error_directive() {
    bool got_error = false;
    Preprocessor pre([&](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) got_error = true;
    });
    pre.process("#if 0\n#else\n#error mensaje de error\n#endif\n", "<test>");
    CHECK(got_error, "#error en rama activa emite error");
}

static void test_warning_directive() {
    bool got_warning = false;
    Preprocessor pre([&](const Diagnostic& d) {
        if (d.level == DiagLevel::WARNING) got_warning = true;
    });
    pre.process("#warning aviso de prueba\n", "<test>");
    CHECK(got_warning, "#warning emite una advertencia");
}

static void test_exclude_error_in_inactive_branch() {
    bool got_error = false;
    Preprocessor pre([&](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) got_error = true;
    });
    pre.process("#if 0\n#error este error no debe aparecer\n#endif\n", "<test>");
    CHECK(!got_error, "#error en rama inactiva no se emite");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de condicionales del preprocesador vpp ===\n";

    test_if_true();
    test_if_false();
    test_if_else_true();
    test_if_else_false();
    test_ifdef_defined();
    test_ifdef_undefined();
    test_ifndef_defined();
    test_ifndef_undefined();
    test_elif_chain();
    test_nested_if();
    test_defined_operator();
    test_expression_arithmetic();
    test_expression_comparison();
    test_expression_logical_and();
    test_expression_logical_or();
    test_expression_ternary();
    test_expression_bitwise();
    test_expression_shift();
    test_error_directive();
    test_warning_directive();
    test_exclude_error_in_inactive_branch();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
