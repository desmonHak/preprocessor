/**
 * @file test_import.cpp
 * @brief Tests para la directiva #import del preprocesador vpp.
 */
#include "preprocessor/preprocessor.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace vpp;
namespace fs = std::filesystem;

// --- utilidades de test -----------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

static void write_file(const fs::path& p, const std::string& content) {
    std::ofstream ofs(p);
    ofs << content;
}

static std::string run(const std::string& source,
                       const std::string& import_dir = "") {
    Preprocessor pp;
    if (!import_dir.empty())
        pp.options().import_paths.push_back(import_dir);
    return pp.process(source, "<test>");
}

static std::string run_with_diag(const std::string& source,
                                  const std::string& import_dir,
                                  bool& had_errors) {
    Preprocessor pp;
    if (!import_dir.empty())
        pp.options().import_paths.push_back(import_dir);
    std::string out = pp.process(source, "<test>");
    had_errors = pp.diagnostics().has_errors();
    return out;
}

static void check(const std::string& name,
                  const std::string& result,
                  const std::string& expected) {
    if (result == expected) {
        std::cout << "[PASS] " << name << "\n";
        ++g_pass;
    } else {
        std::cerr << "[FAIL] " << name << "\n";
        std::cerr << "  expected: " << expected << "\n";
        std::cerr << "  got:      " << result << "\n";
        ++g_fail;
    }
}

static void check_contains(const std::string& name,
                            const std::string& result,
                            const std::string& substr) {
    if (result.find(substr) != std::string::npos) {
        std::cout << "[PASS] " << name << "\n";
        ++g_pass;
    } else {
        std::cerr << "[FAIL] " << name << "\n";
        std::cerr << "  resultado no contiene: " << substr << "\n";
        std::cerr << "  resultado completo:    " << result << "\n";
        ++g_fail;
    }
}

// --- tests ------------------------------------------------------------------

static void test_import_basic(const std::string& tmpdir) {
    // modulo con extension .vph
    write_file(fs::path(tmpdir) / "mymod.vph",
        "#define MYVAL 42\n");

    std::string src = "#import <mymod>\nMYVAL\n";
    std::string out = run(src, tmpdir);
    check_contains("import_basic: expande macro del modulo", out, "42");
}

static void test_import_auto_once(const std::string& tmpdir) {
    // importar dos veces el mismo modulo no produce redefinicion de macro
    write_file(fs::path(tmpdir) / "once.vph",
        "#define ONCE_VAL 99\n");

    std::string src =
        "#import <once>\n"
        "#import <once>\n"   // segunda importacion: debe ser ignorada
        "ONCE_VAL\n";
    bool had_errors = false;
    std::string out = run_with_diag(src, tmpdir, had_errors);
    check("import_auto_once: sin errores de redefinicion", had_errors ? "error" : "ok", "ok");
    check_contains("import_auto_once: macro expandida", out, "99");
}

static void test_import_extension_vel(const std::string& tmpdir) {
    // modulo con extension .vel (segunda preferencia tras .vph)
    write_file(fs::path(tmpdir) / "velmod.vel",
        "#define VEL_CONST 77\n");

    std::string src = "#import <velmod>\nVEL_CONST\n";
    std::string out = run(src, tmpdir);
    check_contains("import_extension_vel: modulo .vel encontrado", out, "77");
}

static void test_import_subdir(const std::string& tmpdir) {
    // modulo en subdirectorio: #import <vesta/types> estilo
    fs::create_directories(fs::path(tmpdir) / "sublib");
    write_file(fs::path(tmpdir) / "sublib" / "defs.vph",
        "#define SUBLIB_OK 1\n");

    std::string src = "#import <sublib/defs>\nSUBLIB_OK\n";
    std::string out = run(src, tmpdir);
    check_contains("import_subdir: modulo en subdirectorio", out, "1");
}

static void test_import_not_found(const std::string& tmpdir) {
    // modulo inexistente debe producir error
    std::string src = "#import <nonexistent_module_xyz>\n";
    bool had_errors = false;
    run_with_diag(src, tmpdir, had_errors);
    check("import_not_found: error si modulo no existe",
          had_errors ? "error" : "ok", "error");
}

static void test_import_macros_visible(const std::string& tmpdir) {
    // macros definidas en un modulo importado son visibles en el archivo fuente
    write_file(fs::path(tmpdir) / "visibility.vph",
        "#define VALX 10\n"
        "#define VALY 20\n"
        "#define VALSUM 30\n");

    std::string src =
        "#import <visibility>\n"
        "VALX VALY VALSUM\n";
    std::string out = run(src, tmpdir);
    check_contains("import_macros_visible: VALX expandida", out, "10");
    check_contains("import_macros_visible: VALSUM expandida", out, "30");
}

static void test_import_pragma_once_explicit(const std::string& tmpdir) {
    // un modulo con #pragma once dentro tambien funciona (doble proteccion)
    write_file(fs::path(tmpdir) / "pragmod.vph",
        "#pragma once\n"
        "#define PRAG_VAL 55\n");

    std::string src =
        "#import <pragmod>\n"
        "#import <pragmod>\n"
        "PRAG_VAL\n";
    bool had_errors = false;
    std::string out = run_with_diag(src, tmpdir, had_errors);
    check("import_pragma_once: sin errores con pragma once explicito",
          had_errors ? "error" : "ok", "ok");
    check_contains("import_pragma_once: macro expandida", out, "55");
}

static void test_import_with_set(const std::string& tmpdir) {
    // usar variables #set dentro de un modulo importado
    write_file(fs::path(tmpdir) / "setmod.vph",
        "#set COUNTER = 0\n"
        "#set COUNTER++\n"
        "#set COUNTER++\n"
        "#define COUNTER_VAL COUNTER\n");

    std::string src =
        "#import <setmod>\n"
        "COUNTER_VAL\n";
    std::string out = run(src, tmpdir);
    check_contains("import_with_set: #set en modulo importado", out, "2");
}

// --- main -------------------------------------------------------------------

int main() {
    // crear directorio temporal para los archivos .vph de prueba
    fs::path tmpdir = fs::temp_directory_path() / "vpp_test_import";
    fs::create_directories(tmpdir);
    std::string td = tmpdir.string();

    test_import_basic(td);
    test_import_auto_once(td);
    test_import_extension_vel(td);
    test_import_subdir(td);
    test_import_not_found(td);
    test_import_macros_visible(td);
    test_import_pragma_once_explicit(td);
    test_import_with_set(td);

    // limpiar directorio temporal
    fs::remove_all(tmpdir);

    std::cout << "\n--- Resultados: " << g_pass << " pass, " << g_fail << " fail ---\n";
    return g_fail > 0 ? 1 : 0;
}
