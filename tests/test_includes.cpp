/**
 * @file test_includes.cpp
 * @brief Tests de directivas #include y #pragma once del preprocesador vpp.
 */

#include "preprocessor/preprocessor.h"
#include <iostream>
#include <string>
#include <unordered_map>

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

// crea un preprocesador con un sistema de archivos virtual
static Preprocessor make_pp_with_fs(
    const std::unordered_map<std::string, std::string>& vfs)
{
    Preprocessor pp([](const Diagnostic&){});
    pp.set_include_resolver(
        [vfs](const std::string&, const std::string& path, bool) -> std::string {
            auto it = vfs.find(path);
            if (it != vfs.end()) return it->second;
            return "";
        });
    return pp;
}

/* --- tests ---------------------------------------------------------------- */

static void test_include_user_file() {
    std::unordered_map<std::string, std::string> vfs = {
        { "header.h", "#define INCLUDED\nfrom_header\n" }
    };
    auto pp = make_pp_with_fs(vfs);
    auto out = pp.process("#include \"header.h\"\nfrom_main\n", "<test>");
    CHECK(contains(out, "from_header"), "#include inserta el contenido del archivo");
    CHECK(contains(out, "from_main"),   "#include preserva el contenido posterior");
}

static void test_include_propagates_macros() {
    // las macros definidas en el archivo incluido son visibles despues
    std::unordered_map<std::string, std::string> vfs = {
        { "defs.h", "#define MY_VALUE 99\n" }
    };
    auto pp = make_pp_with_fs(vfs);
    auto out = pp.process("#include \"defs.h\"\nresult: MY_VALUE\n", "<test>");
    CHECK(contains(out, "99"), "macros del archivo incluido son visibles en el padre");
}

static void test_include_not_found_gives_error() {
    bool got_error = false;
    Preprocessor pp([&](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) got_error = true;
    });
    pp.set_include_resolver([](const std::string&, const std::string&, bool) {
        return std::string("");
    });
    pp.process("#include \"missing.h\"\n", "<test>");
    CHECK(got_error, "#include de archivo no encontrado emite error");
}

static void test_pragma_once_prevents_double_include() {
    std::unordered_map<std::string, std::string> vfs = {
        { "once.h", "#pragma once\nUNIQUE_CONTENT\n" }
    };
    auto pp = make_pp_with_fs(vfs);
    auto out = pp.process(
        "#include \"once.h\"\n"
        "#include \"once.h\"\n"
        "end\n", "<test>");
    // UNIQUE_CONTENT debe aparecer exactamente una vez
    size_t count = 0;
    size_t pos = 0;
    while ((pos = out.find("UNIQUE_CONTENT", pos)) != std::string::npos) {
        ++count; ++pos;
    }
    CHECK(count == 1, "#pragma once evita la inclusion doble");
}

static void test_nested_include() {
    std::unordered_map<std::string, std::string> vfs = {
        { "a.h", "#include \"b.h\"\nfrom_a\n" },
        { "b.h", "from_b\n" }
    };
    auto pp = make_pp_with_fs(vfs);
    auto out = pp.process("#include \"a.h\"\n", "<test>");
    CHECK(contains(out, "from_b"), "include anidado incluye el archivo interno");
    CHECK(contains(out, "from_a"), "include anidado incluye el archivo externo");
}

static void test_include_with_conditional() {
    std::unordered_map<std::string, std::string> vfs = {
        { "cond.h",
          "#ifndef COND_H\n"
          "#define COND_H\n"
          "guarded_content\n"
          "#endif\n" }
    };
    auto pp = make_pp_with_fs(vfs);
    auto out = pp.process(
        "#include \"cond.h\"\n"
        "#include \"cond.h\"\n",
        "<test>");
    // con guardia de inclusion manual, guarded_content aparece solo una vez
    size_t count = 0;
    size_t pos = 0;
    while ((pos = out.find("guarded_content", pos)) != std::string::npos) {
        ++count; ++pos;
    }
    CHECK(count == 1, "guardia de inclusion manual evita inclusion doble");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de includes del preprocesador vpp ===\n";

    test_include_user_file();
    test_include_propagates_macros();
    test_include_not_found_gives_error();
    test_pragma_once_prevents_double_include();
    test_nested_include();
    test_include_with_conditional();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
