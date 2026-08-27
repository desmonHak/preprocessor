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


/**
 * @brief Un fichero guardado a la manera clasica se procesa una sola vez.
 *
 * Es la optimizacion de inclusion multiple: la segunda vez no se abre siquiera,
 * porque su propia guarda ya esta definida y no podria producir nada.
 */
static void test_guarda_clasica_una_sola_vez() {
    auto pp = make_pp_with_fs({
        {"g.h", "#ifndef G_H\n#define G_H\nCONTENIDO\n#endif\n"}
    });

    const std::string out = pp.process(
        "#include \"g.h\"\n#include \"g.h\"\n#include \"g.h\"\n", "t.c");

    int veces = 0;
    for (size_t i = out.find("CONTENIDO"); i != std::string::npos;
         i = out.find("CONTENIDO", i + 1)) ++veces;
    CHECK(veces == 1, "una cabecera con guarda clasica se procesa una vez");
}

/**
 * @brief Lo que esta FUERA de la guarda se emite en cada inclusion.
 *
 * Es el limite de la optimizacion, y saltarselo seria perder contenido: el
 * fichero solo se puede omitir si la guarda lo envuelve TODO.
 */
static void test_contenido_fuera_de_la_guarda() {
    auto pp = make_pp_with_fs({
        {"f.h", "FUERA\n#ifndef F_H\n#define F_H\nDENTRO\n#endif\n"}
    });

    const std::string out = pp.process("#include \"f.h\"\n#include \"f.h\"\n",
                                        "t.c");

    int fuera = 0, dentro = 0;
    for (size_t i = out.find("FUERA"); i != std::string::npos;
         i = out.find("FUERA", i + 1)) ++fuera;
    for (size_t i = out.find("DENTRO"); i != std::string::npos;
         i = out.find("DENTRO", i + 1)) ++dentro;

    CHECK(fuera == 2, "lo de fuera de la guarda se emite en cada inclusion");
    CHECK(dentro == 1, "y lo de dentro solo la primera vez");
}

/**
 * @brief Un `#undef` de la guarda vuelve a hacer significativo el fichero.
 *
 * Sin comprobar que la macro SIGA definida, el fichero se saltaria para siempre
 * y el contenido no volveria a aparecer.
 */
static void test_undef_de_la_guarda() {
    auto pp = make_pp_with_fs({
        {"u.h", "#ifndef U_H\n#define U_H\nCUERPO\n#endif\n"}
    });

    const std::string out = pp.process(
        "#include \"u.h\"\n#undef U_H\n#include \"u.h\"\n", "t.c");

    int veces = 0;
    for (size_t i = out.find("CUERPO"); i != std::string::npos;
         i = out.find("CUERPO", i + 1)) ++veces;
    CHECK(veces == 2, "tras #undef de la guarda el fichero vuelve a contar");
}

/**
 * @brief Una guarda que no define su propio nombre no vale.
 *
 * Se veria igual desde fuera -- `#ifndef X` envolviendolo todo -- pero entrar
 * una segunda vez SI tiene efectos, porque X nunca se define.
 */
static void test_guarda_que_no_se_define() {
    auto pp = make_pp_with_fs({
        {"n.h", "#ifndef N_H\n#define OTRA_COSA\nCUERPO\n#endif\n"}
    });

    const std::string out = pp.process("#include \"n.h\"\n#include \"n.h\"\n",
                                        "t.c");

    int veces = 0;
    for (size_t i = out.find("CUERPO"); i != std::string::npos;
         i = out.find("CUERPO", i + 1)) ++veces;
    CHECK(veces == 2, "una guarda que no define su nombre no se toma por tal");
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
    test_guarda_clasica_una_sola_vez();
    test_contenido_fuera_de_la_guarda();
    test_undef_de_la_guarda();
    test_guarda_que_no_se_define();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
