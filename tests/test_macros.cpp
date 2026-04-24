/**
 * @file test_macros.cpp
 * @brief Tests de expansion de macros del preprocesador vpp.
 */

#include "preprocessor/preprocessor.h"
#include <cassert>
#include <iostream>
#include <string>
#include <algorithm>

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

// elimina espacios extra para comparacion flexible
static std::string strip(std::string s) {
    // colapsar espacios multiples en uno
    std::string result;
    bool last_space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!last_space && !result.empty()) { result += ' '; last_space = true; }
        } else if (c == '\n') {
            last_space = false;
            result += c;
        } else {
            last_space = false;
            result += c;
        }
    }
    // eliminar espacio final
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// procesa una cadena fuente y devuelve la salida
static std::string preprocess(const std::string& src) {
    Preprocessor pp([](const Diagnostic&){});
    return pp.process(src, "<test>");
}

/* --- tests ---------------------------------------------------------------- */

static void test_simple_object_macro() {
    auto out = preprocess("#define FOO 42\nresult FOO end\n");
    CHECK(contains(out, "42"), "macro objeto simple sustituye valor");
}

static void test_multitoken_object_macro() {
    auto out = preprocess("#define MSG hello world\nMSG\n");
    CHECK(contains(out, "hello world") || contains(out, "helloworld"),
          "macro objeto multi-token");
}

static void test_function_macro_noargs() {
    auto out = preprocess("#define NL()\nnewline: NL()\n");
    CHECK(!contains(out, "NL()"), "macro funcion sin argumentos sustituida");
}

static void test_function_macro_one_arg() {
    auto out = preprocess("#define DOUBLE(x) x x\nDOUBLE(hello)\n");
    CHECK(contains(out, "hello hello"), "macro funcion un argumento");
}

static void test_function_macro_two_args() {
    auto out = preprocess("#define ADD(a, b) a + b\nADD(1, 2)\n");
    CHECK(contains(out, "1 + 2") || contains(out, "1+2"), "macro funcion dos argumentos");
}

static void test_nested_macro_expansion() {
    auto out = preprocess(
        "#define X 10\n"
        "#define Y X\n"
        "result: Y\n");
    CHECK(contains(out, "10"), "expansion anidada de macros");
}

static void test_undef() {
    auto out = preprocess(
        "#define FOO present\n"
        "#undef FOO\n"
        "FOO\n");
    CHECK(!contains(out, "present"), "#undef elimina la macro");
}

static void test_recursive_macro_no_infinite_loop() {
    // una macro que se llama a si misma no debe hacer bucle infinito
    auto out = preprocess("#define SELF SELF\nSELF\n");
    // debe terminar y emitir "SELF" sin expandir
    CHECK(contains(out, "SELF"), "macro auto-referencial no causa bucle infinito");
}

static void test_stringify_operator() {
    auto out = preprocess("#define STR(x) #x\nSTR(hello)\n");
    CHECK(contains(out, "\"hello\""), "operador # stringify");
}

static void test_token_paste_operator() {
    auto out = preprocess("#define PASTE(a,b) a##b\nPASTE(foo,bar)\n");
    CHECK(contains(out, "foobar"), "operador ## token paste");
}

static void test_variadic_macro() {
    auto out = preprocess("#define LOG(...) __VA_ARGS__\nLOG(a, b, c)\n");
    // debe contener los argumentos variadicos
    CHECK(contains(out, "a") && contains(out, "b"), "macro variadic __VA_ARGS__");
}

static void test_predefined_windows_or_linux() {
    // al menos una de las macros de plataforma debe estar definida
    auto out = preprocess(
        "#ifdef __WINDOWS__\nwin\n#endif\n"
        "#ifdef __LINUX__\nlinux\n#endif\n"
        "#ifdef __MACOS__\nmacos\n#endif\n");
    bool any_defined = contains(out, "win") || contains(out, "linux") || contains(out, "macos");
    CHECK(any_defined, "al menos una macro de plataforma esta definida");
}

static void test_predefined_arch() {
    auto out = preprocess(
        "#ifdef __ARCH_64__\n64bits\n#endif\n"
        "#ifdef __ARCH_32__\n32bits\n#endif\n"
        "#ifdef __ARCH_16__\n16bits\n#endif\n");
    bool any = contains(out, "64bits") || contains(out, "32bits") || contains(out, "16bits");
    CHECK(any, "al menos una macro de arquitectura esta definida");
}

static void test_redefinition_warning() {
    bool got_warning = false;
    Preprocessor pp([&](const Diagnostic& d) {
        if (d.level == DiagLevel::WARNING) got_warning = true;
    });
    pp.process("#define FOO 1\n#define FOO 2\nFOO\n", "<test>");
    CHECK(got_warning, "redefinicion de macro emite advertencia");
}

static void test_macro_in_string_not_expanded() {
    auto out = preprocess("#define FOO replaced\n\"FOO\"\n");
    // FOO dentro de una cadena no debe expandirse
    CHECK(contains(out, "\"FOO\""), "macro no se expande dentro de cadenas");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de macros del preprocesador vpp ===\n";

    test_simple_object_macro();
    test_multitoken_object_macro();
    test_function_macro_noargs();
    test_function_macro_one_arg();
    test_function_macro_two_args();
    test_nested_macro_expansion();
    test_undef();
    test_recursive_macro_no_infinite_loop();
    test_stringify_operator();
    test_token_paste_operator();
    test_variadic_macro();
    test_predefined_windows_or_linux();
    test_predefined_arch();
    test_redefinition_warning();
    test_macro_in_string_not_expanded();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
