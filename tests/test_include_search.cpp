/**
 * @file test_include_search.cpp
 * @brief Tests de la busqueda de ficheros de inclusion (IncludeSearch).
 *
 * Se prueba el componente SOLO, sin preprocesador alrededor.  Es lo que se gana
 * al tenerlo aparte: las reglas de precedencia -- primero al lado del que
 * incluye, despues por la lista de rutas, y `#include_next` reanudando en el
 * directorio siguiente -- se pueden comprobar directamente, sin montar un
 * pipeline entero ni adivinar por que salio un fichero y no otro.
 */

#include "preprocessor/pp_include.h"

#include <filesystem>
#include <fstream>
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

namespace {

/// Raiz temporal donde se monta el arbol de ficheros de prueba.
std::filesystem::path g_raiz;

/**
 * @brief Escribe un fichero, creando los directorios que hagan falta.
 * @param rel      Ruta relativa a la raiz temporal.
 * @param contenido Texto a escribir.
 * @return Ruta absoluta del fichero.
 */
std::filesystem::path escribir(const std::string& rel,
                               const std::string& contenido) {
    const std::filesystem::path p = g_raiz / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary);
    ofs << contenido;
    return p;
}

/**
 * @brief Monta el arbol de prueba.
 *
 * Dos directorios con una cabecera del MISMO nombre, que es lo que hace
 * interesante a `#include_next`, mas una cabecera con un vecino al lado.
 */
void montar() {
    g_raiz = std::filesystem::temp_directory_path() / "vpp_test_include";
    std::filesystem::remove_all(g_raiz);

    escribir("a/comun.h",  "DESDE_A\n");
    escribir("b/comun.h",  "DESDE_B\n");
    escribir("a/solo_a.h", "SOLO_EN_A\n");
    escribir("a/con_vecino.h", "#include \"vecino.h\"\n");
    escribir("a/vecino.h", "SOY_EL_VECINO\n");
    escribir("origen/principal.h", "#include \"hermano.h\"\n");
    escribir("origen/hermano.h", "SOY_EL_HERMANO\n");
}

/** @brief Rutas de inclusion del arbol de prueba, en orden a, b. */
std::vector<std::string> rutas() {
    return { (g_raiz / "a").string(), (g_raiz / "b").string() };
}

} // namespace

/* --- tests ---------------------------------------------------------------- */

/** @brief Se encuentra por la lista de rutas y se dice en cual aparecio. */
static void test_busqueda_por_rutas() {
    IncludeSearch s(rutas(), {}, {});

    ResolvedInclude r = s.resolve("solo_a.h", true, "");
    CHECK(r.found, "encuentra un fichero de la primera ruta");
    CHECK(r.content.find("SOLO_EN_A") != std::string::npos,
          "devuelve su contenido");
    CHECK(r.search_index == 0, "dice que aparecio en la ruta 0");

    r = s.resolve("no_existe_jamas.h", true, "");
    CHECK(!r.found, "no encuentra lo que no esta");
}

/** @brief Con el mismo nombre en dos rutas, gana la primera. */
static void test_precedencia_de_rutas() {
    IncludeSearch s(rutas(), {}, {});
    const ResolvedInclude r = s.resolve("comun.h", true, "");
    CHECK(r.found && r.content.find("DESDE_A") != std::string::npos,
          "gana la ruta que va antes en la lista");
    CHECK(r.search_index == 0, "y se sabe que fue la 0");
}

/**
 * @brief `#include_next` salta el directorio actual y sigue por el siguiente.
 *
 * Es como una biblioteca superpone su cabecera sobre la del sistema con el
 * mismo nombre: sin esto, volveria a encontrarse a si misma y no habria forma
 * de llegar a la de debajo.
 */
static void test_include_next() {
    IncludeSearch s(rutas(), {}, {});

    // partiendo de la ruta 0, reanudar en la 1
    const ResolvedInclude r = s.resolve("comun.h", true, "", /*start=*/1);
    CHECK(r.found, "encuentra la de la ruta siguiente");
    CHECK(r.content.find("DESDE_B") != std::string::npos,
          "y es la de la ruta siguiente, no la de la actual");
    CHECK(r.search_index == 1, "informa de la ruta correcta");

    // pasada la ultima ruta ya no queda nada
    const ResolvedInclude fin = s.resolve("comun.h", true, "", /*start=*/2);
    CHECK(!fin.found, "pasada la ultima ruta no encuentra nada");
}

/**
 * @brief Un vecino se busca al lado del fichero que lo incluye.
 *
 * Y ese fichero es el RESUELTO, no el de partida: es lo que hace que una
 * cabecera encontrada por una ruta de busqueda pueda incluir a la de al lado.
 */
static void test_vecino_relativo() {
    // sin ninguna ruta de inclusion: solo puede salir por la via relativa
    IncludeSearch s({}, {}, {});

    const std::string desde = (g_raiz / "origen" / "principal.h").string();
    const ResolvedInclude r = s.resolve("hermano.h", false, desde);
    CHECK(r.found, "encuentra al vecino del fichero que incluye");
    CHECK(r.content.find("SOY_EL_HERMANO") != std::string::npos,
          "y es el correcto");
    CHECK(r.search_index == -1,
          "marca que no salio de la lista de rutas");
}

/** @brief La forma <...> no mira al lado del que incluye. */
static void test_sistema_no_mira_al_lado() {
    IncludeSearch s({}, {}, {});
    const std::string desde = (g_raiz / "origen" / "principal.h").string();
    const ResolvedInclude r = s.resolve("hermano.h", true, desde);
    CHECK(!r.found,
          "con <...> no se busca junto al fichero que incluye");
}

/** @brief `#import` mira en sus propias rutas y prueba las extensiones. */
static void test_import() {
    escribir("mods/vesta/io.vph", "MODULO_IO\n");
    IncludeSearch s({}, {}, { (g_raiz / "mods").string() });

    ResolvedInclude r = s.resolve_import("vesta/io");
    CHECK(r.found && r.content.find("MODULO_IO") != std::string::npos,
          "encuentra el modulo anadiendo la extension");

    r = s.resolve_import("vesta/no_existe");
    CHECK(!r.found, "no encuentra un modulo que no esta");
}

/** @brief Un fichero vacio EXISTE; no es lo mismo que no encontrarlo. */
static void test_fichero_vacio() {
    escribir("a/vacio.h", "");
    IncludeSearch s(rutas(), {}, {});
    const ResolvedInclude r = s.resolve("vacio.h", true, "");
    CHECK(r.found, "una cabecera vacia cuenta como encontrada");
    CHECK(r.content.empty(), "y su contenido es vacio");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de busqueda de inclusiones ===\n";

    montar();

    test_busqueda_por_rutas();
    test_precedencia_de_rutas();
    test_include_next();
    test_vecino_relativo();
    test_sistema_no_mira_al_lado();
    test_import();
    test_fichero_vacio();

    std::error_code ec;
    std::filesystem::remove_all(g_raiz, ec);

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
