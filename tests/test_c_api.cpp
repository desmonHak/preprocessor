/**
 * @file test_c_api.cpp
 * @brief Tests del ABI en C del preprocesador vpp (preprocessor/vpp_c.h).
 *
 * Estos tests se escriben deliberadamente CONTRA EL HEADER C, sin tocar la API
 * C++, para ejercitar exactamente la superficie que exporta la DLL/.so: si un
 * simbolo falta o una firma cambia, el fallo aparece aqui y no en el consumidor
 * de la biblioteca compartida, que es donde seria mucho mas caro de encontrar.
 */

#include "preprocessor/vpp_c.h"

#include <iostream>
#include <cstring>
#include <string>

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

/**
 * @brief Comprueba si una cadena C contiene una subcadena.
 * @param haystack Cadena donde buscar; puede ser NULL.
 * @param needle   Subcadena buscada.
 * @return true si aparece.
 */
static bool contains(const char* haystack, const char* needle) {
    if (!haystack) return false;
    return std::strstr(haystack, needle) != nullptr;
}

/* --- tests ---------------------------------------------------------------- */

/** @brief La version reportada es coherente entre las dos formas de leerla. */
static void test_version() {
    int major = -1, minor = -1, patch = -1;
    vpp_version(&major, &minor, &patch);
    CHECK(major >= 1, "vpp_version devuelve un major valido");

    const char* s = vpp_version_string();
    CHECK(s != nullptr && s[0] != '\0', "vpp_version_string no esta vacia");

    std::string expected = std::to_string(major) + "." +
                           std::to_string(minor) + "." +
                           std::to_string(patch);
    CHECK(expected == s, "vpp_version y vpp_version_string coinciden");

    // NULL en cualquier parametro es valido: no debe petar
    vpp_version(nullptr, nullptr, nullptr);
    CHECK(true, "vpp_version acepta punteros NULL");
}

/** @brief Ciclo de vida basico del handle. */
static void test_lifecycle() {
    vpp_preprocessor* pp = vpp_create();
    CHECK(pp != nullptr, "vpp_create devuelve un handle");
    vpp_destroy(pp);

    // destruir NULL debe ser un no-op, no un fallo
    vpp_destroy(nullptr);
    CHECK(true, "vpp_destroy(NULL) es un no-op valido");
}

/** @brief Expansion de una macro simple a traves del ABI C. */
static void test_process_simple() {
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    vpp_status st = vpp_process(pp,
                                "#define SALUDO hola\nSALUDO mundo\n",
                                "test.vel",
                                &out);

    CHECK(st == VPP_OK, "vpp_process devuelve VPP_OK sin errores");
    CHECK(out != nullptr, "vpp_process rellena out_result");
    CHECK(contains(out, "hola mundo"), "la macro se expandio");
    CHECK(vpp_error_count(pp) == 0u, "no hay errores");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Los defines externos (-D) llegan al fuente. */
static void test_add_define() {
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    CHECK(vpp_add_define(pp, "MODO=produccion") == VPP_OK,
          "vpp_add_define acepta NAME=valor");
    CHECK(vpp_add_define(pp, "FLAG_ACTIVO") == VPP_OK,
          "vpp_add_define acepta NAME suelto");

    vpp_status st = vpp_process(pp,
                                "#ifdef FLAG_ACTIVO\nactivo:MODO\n#endif\n",
                                "test.vel",
                                &out);

    CHECK(st == VPP_OK, "el fuente con defines externos se procesa");
    CHECK(contains(out, "activo:produccion"),
          "el define externo se expande dentro del condicional");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Un error de preprocesado se reporta como diagnostico, no como crash. */
static void test_diagnostics() {
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    // #include de un fichero que no existe: error, pero recuperable
    vpp_status st = vpp_process(pp,
                                "#include \"no_existe_jamas_12345.h\"\n",
                                "test.vel",
                                &out);

    CHECK(st == VPP_ERR_DIAGNOSTIC,
          "un include inexistente devuelve VPP_ERR_DIAGNOSTIC");
    CHECK(vpp_error_count(pp) > 0u, "vpp_error_count refleja el error");
    CHECK(vpp_diagnostic_count(pp) > 0u, "hay diagnosticos acumulados");

    vpp_diagnostic d;
    std::memset(&d, 0, sizeof(d));
    CHECK(vpp_diagnostic_at(pp, 0, &d) == VPP_OK,
          "vpp_diagnostic_at lee el primer diagnostico");
    CHECK(d.message != nullptr && d.message[0] != '\0',
          "el diagnostico trae mensaje");
    CHECK(d.file != nullptr, "el diagnostico trae fichero");

    // fuera de rango debe fallar limpiamente
    CHECK(vpp_diagnostic_at(pp, 99999, &d) == VPP_ERR_INVALID_ARG,
          "un indice fuera de rango devuelve VPP_ERR_INVALID_ARG");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief El resolvedor de includes en C sirve el contenido desde memoria. */
static void test_include_resolver() {
    struct Vfs {
        const char* nombre;
        const char* contenido;
    };
    static Vfs vfs = { "cabecera.h", "#define DESDE_HEADER 42\n" };

    vpp_preprocessor* pp = vpp_create();

    vpp_status st = vpp_set_include_resolver(
        pp,
        [](void* user, const char*, const char* requested, int) -> const char* {
            Vfs* v = static_cast<Vfs*>(user);
            if (std::strcmp(requested, v->nombre) == 0) return v->contenido;
            return nullptr;  // no encontrado
        },
        &vfs);
    CHECK(st == VPP_OK, "vpp_set_include_resolver instala el callback");

    char* out = nullptr;
    st = vpp_process(pp,
                     "#include \"cabecera.h\"\nvalor=DESDE_HEADER\n",
                     "test.vel",
                     &out);

    CHECK(st == VPP_OK, "el include resuelto por callback no da error");
    CHECK(contains(out, "valor=42"),
          "la macro definida en el fichero virtual se expande");

    // el fichero servido debe quedar registrado como dependencia
    CHECK(vpp_included_file_count(pp) > 0u,
          "el fichero incluido se registra como dependencia");
    const char* dep = vpp_included_file_at(pp, 0);
    CHECK(dep != nullptr, "vpp_included_file_at devuelve la ruta");
    CHECK(vpp_included_file_at(pp, 99999) == nullptr,
          "un indice fuera de rango devuelve NULL");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Un resolvedor que devuelve NULL se traduce a "no encontrado". */
static void test_include_resolver_not_found() {
    vpp_preprocessor* pp = vpp_create();

    vpp_set_include_resolver(
        pp,
        [](void*, const char*, const char*, int) -> const char* {
            return nullptr;
        },
        nullptr);

    char* out = nullptr;
    vpp_status st = vpp_process(pp, "#include \"lo_que_sea.h\"\n",
                                "test.vel", &out);

    CHECK(st == VPP_ERR_DIAGNOSTIC,
          "un resolvedor que devuelve NULL produce error de include");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Inspeccion de la tabla de macros. */
static void test_macro_inspection() {
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    // las macros de plataforma se registran en el constructor
    CHECK(vpp_macro_count(pp) > 0u,
          "la tabla arranca con las macros predefinidas");

    vpp_process(pp, "#define MI_MACRO 1\n", "test.vel", &out);

    CHECK(vpp_is_defined(pp, "MI_MACRO") == 1,
          "vpp_is_defined ve una macro definida en el fuente");
    CHECK(vpp_is_defined(pp, "NO_DEFINIDA_JAMAS") == 0,
          "vpp_is_defined devuelve 0 para una macro inexistente");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Todos los puntos de entrada toleran argumentos NULL. */
static void test_null_safety() {
    char* out = nullptr;

    CHECK(vpp_process(nullptr, "x", "f", &out) == VPP_ERR_INVALID_ARG,
          "vpp_process rechaza un handle NULL");
    CHECK(vpp_add_define(nullptr, "A") == VPP_ERR_INVALID_ARG,
          "vpp_add_define rechaza un handle NULL");
    CHECK(vpp_error_count(nullptr) == 0u,
          "vpp_error_count(NULL) devuelve 0");
    CHECK(vpp_diagnostic_count(nullptr) == 0u,
          "vpp_diagnostic_count(NULL) devuelve 0");
    CHECK(vpp_included_file_count(nullptr) == 0u,
          "vpp_included_file_count(NULL) devuelve 0");
    CHECK(vpp_is_defined(nullptr, "A") == 0,
          "vpp_is_defined(NULL) devuelve 0");

    vpp_preprocessor* pp = vpp_create();
    CHECK(vpp_process(pp, nullptr, "f", &out) == VPP_ERR_INVALID_ARG,
          "vpp_process rechaza un fuente NULL");
    CHECK(vpp_process(pp, "x", "f", nullptr) == VPP_ERR_INVALID_ARG,
          "vpp_process rechaza out_result NULL");
    CHECK(vpp_add_define(pp, nullptr) == VPP_ERR_INVALID_ARG,
          "vpp_add_define rechaza un define NULL");
    vpp_destroy(pp);

    // liberar NULL debe ser un no-op
    vpp_string_free(nullptr);
    CHECK(true, "vpp_string_free(NULL) es un no-op valido");
}

/** @brief Un fichero de entrada inexistente se reporta como VPP_ERR_IO. */
static void test_process_file_missing() {
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    vpp_status st = vpp_process_file(pp,
                                     "ruta/que/no/existe/jamas_98765.vel",
                                     &out);
    CHECK(st == VPP_ERR_IO,
          "vpp_process_file devuelve VPP_ERR_IO si no puede abrir el fichero");

    vpp_string_free(out);
    vpp_destroy(pp);
}

/** @brief Los codigos de estado tienen nombre legible. */
static void test_status_string() {
    CHECK(std::strcmp(vpp_status_string(VPP_OK), "ok") == 0,
          "vpp_status_string(VPP_OK) es \"ok\"");
    const char* s = vpp_status_string(VPP_ERR_INTERNAL);
    CHECK(s != nullptr && s[0] != '\0',
          "vpp_status_string devuelve texto para cada codigo");
}

/** @brief Los conmutadores de opciones cambian el comportamiento observable. */
static void test_options() {
    // con expansion desactivada, la macro debe salir intacta
    vpp_preprocessor* pp = vpp_create();
    char* out = nullptr;

    CHECK(vpp_set_expand_macros(pp, 0) == VPP_OK,
          "vpp_set_expand_macros acepta el cambio");
    vpp_process(pp, "#define X 99\nvalor=X\n", "test.vel", &out);
    CHECK(contains(out, "valor=X"),
          "sin expansion, la macro no se sustituye");

    vpp_string_free(out);
    vpp_destroy(pp);

    // las rutas de busqueda se aceptan sin error
    pp = vpp_create();
    CHECK(vpp_add_include_path(pp, ".") == VPP_OK,
          "vpp_add_include_path acepta una ruta");
    CHECK(vpp_add_import_path(pp, ".") == VPP_OK,
          "vpp_add_import_path acepta una ruta");
    CHECK(vpp_set_line_markers(pp, 1) == VPP_OK,
          "vpp_set_line_markers acepta el cambio");
    CHECK(vpp_set_track_includes(pp, 1) == VPP_OK,
          "vpp_set_track_includes acepta el cambio");
    vpp_destroy(pp);
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests del ABI en C del preprocesador vpp ===\n";

    test_version();
    test_lifecycle();
    test_process_simple();
    test_add_define();
    test_diagnostics();
    test_include_resolver();
    test_include_resolver_not_found();
    test_macro_inspection();
    test_null_safety();
    test_process_file_missing();
    test_status_string();
    test_options();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
