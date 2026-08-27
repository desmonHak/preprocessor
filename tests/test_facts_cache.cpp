/**
 * @file test_facts_cache.cpp
 * @brief Tests de la memoria entre ejecuciones.
 *
 * Lo que se prueba no es tanto que recuerde -- eso es lo facil -- como que se
 * NIEGUE a recordar cuando no debe: sin saber a quien se pregunta, o con datos
 * que no se podrian volver a leer.  Una memoria que devuelve la respuesta de
 * otro compilador es peor que no tener memoria, porque el fallo no da la cara.
 */

#include "preprocessor/pp_facts_cache.h"
#include "preprocessor/pp_command_cache.h"
#include "preprocessor/pp_atomic_write.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

/// Directorio temporal que hace de memoria en las pruebas.
std::filesystem::path g_dir;

/**
 * @brief Lee un fichero entero.
 * @param path Ruta del fichero.
 * @return Su contenido, o vacio si no se pudo leer.
 */
std::string read_all(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return std::string((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
}

} // namespace

/* --- escritura atomica ---------------------------------------------------- */

/** @brief Escribe, crea los directorios que falten y deja el contenido exacto. */
static void test_atomic_write() {
    const std::string path = (g_dir / "sub" / "dir" / "f.txt").string();

    CHECK(write_file_atomically(path, "hola"), "escribe creando los directorios");
    CHECK(read_all(path) == "hola", "el contenido es el que se pidio");

    CHECK(write_file_atomically(path, "adios y algo mas"),
          "vuelve a escribir encima");
    CHECK(read_all(path) == "adios y algo mas",
          "y no quedan restos del contenido anterior");
}

/** @brief No deja temporales tirados por el directorio. */
static void test_atomic_write_leaves_no_temp() {
    const std::filesystem::path dir = g_dir / "limpio";
    write_file_atomically((dir / "f.txt").string(), "contenido");

    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        (void)e;
        ++files;
    }
    CHECK(files == 1, "solo queda el fichero destino, sin temporales");
}

/* --- la memoria de respuestas --------------------------------------------- */

/** @brief Lo guardado se encuentra dentro de la misma ejecucion. */
static void test_store_and_lookup() {
    FactsCache cache(g_dir.string(), "huella_a");
    CHECK(cache.enabled(), "con directorio y huella queda operativa");

    std::string value;
    CHECK(!cache.lookup("__has_builtin x", value),
          "lo que no se ha guardado no aparece");

    cache.store("__has_builtin x", "1");
    CHECK(cache.lookup("__has_builtin x", value) && value == "1",
          "lo guardado se encuentra");
}

/**
 * @brief Lo aprendido sobrevive al proceso.
 *
 * Es la razon de existir de todo esto: sin esto, cada fichero de una
 * compilacion vuelve a preguntarle lo mismo al compilador.
 */
static void test_persists() {
    {
        FactsCache cache(g_dir.string(), "huella_b");
        cache.store("__has_builtin expect", "1");
        cache.store("__has_attribute inline", "0");
    }   // el destructor escribe

    FactsCache reopened(g_dir.string(), "huella_b");
    std::string value;
    CHECK(reopened.lookup("__has_builtin expect", value) && value == "1",
          "lo aprendido sigue ahi en la siguiente ejecucion");
    CHECK(reopened.lookup("__has_attribute inline", value) && value == "0",
          "tambien las respuestas negativas");
    CHECK(reopened.size() == 2, "y no aparece nada de mas");
}

/**
 * @brief Cada compilador tiene su memoria.
 *
 * Sin esta separacion, la respuesta de un compilador se le daria a otro, que es
 * exactamente el fallo silencioso que la huella viene a impedir.
 */
static void test_separated_by_fingerprint() {
    {
        FactsCache uno(g_dir.string(), "huella_c1");
        uno.store("__has_builtin algo", "1");
    }
    {
        FactsCache dos(g_dir.string(), "huella_c2");
        dos.store("__has_builtin algo", "0");
    }

    FactsCache uno(g_dir.string(), "huella_c1");
    FactsCache dos(g_dir.string(), "huella_c2");
    std::string a, b;
    CHECK(uno.lookup("__has_builtin algo", a) && a == "1",
          "el primer compilador conserva su respuesta");
    CHECK(dos.lookup("__has_builtin algo", b) && b == "0",
          "y el segundo la suya, que es la contraria");
}

/**
 * @brief Sin huella no se recuerda nada.
 *
 * Es deliberado: la huella vacia significa que no se pudo identificar al
 * compilador, y sin saber quien es no hay forma de notar que ha cambiado.
 * Recordar a ciegas seria peor que no recordar.
 */
static void test_disabled_without_fingerprint() {
    FactsCache cache(g_dir.string(), "");
    CHECK(!cache.enabled(), "sin huella no queda operativa");

    cache.store("k", "1");
    std::string value;
    CHECK(!cache.lookup("k", value), "y no guarda nada");
    CHECK(!cache.flush(), "ni escribe fichero alguno");
}

/** @brief Sin directorio tampoco. */
static void test_disabled_without_dir() {
    FactsCache cache("", "huella_d");
    CHECK(!cache.enabled(), "sin directorio no queda operativa");
    cache.store("k", "1");
    std::string value;
    CHECK(!cache.lookup("k", value), "y no guarda nada");
}

/**
 * @brief Lo que no se podria volver a leer no se guarda.
 *
 * El formato es de una linea por registro con tabuladores; antes que inventar
 * un escapado para un caso que casi no ocurre, esos valores se dejan sin
 * recordar y se vuelven a preguntar.
 */
static void test_rejects_unstorable() {
    FactsCache cache(g_dir.string(), "huella_e");
    cache.store("con\ttab", "1");
    cache.store("con\nsalto", "1");
    cache.store("clave", "valor\tcon\ttab");

    std::string value;
    CHECK(!cache.lookup("con\ttab", value), "no guarda una clave con tabulador");
    CHECK(!cache.lookup("con\nsalto", value), "ni una con salto de linea");
    CHECK(!cache.lookup("clave", value), "ni un valor con tabulador");
}

/**
 * @brief Un fichero ajeno se ignora entero.
 *
 * La memoria vive en un directorio del usuario donde puede haber cualquier
 * cosa; leer a medias algo que no es nuestro daria respuestas inventadas.
 */
static void test_ignores_foreign_file() {
    const std::string path = FactsCache::file_path(g_dir.string(), "huella_f");
    write_file_atomically(path, "esto no es un fichero de vpp\nclave\tvalor\n");

    FactsCache cache(g_dir.string(), "huella_f");
    std::string value;
    CHECK(!cache.lookup("clave", value),
          "un fichero sin nuestra cabecera se ignora entero");
}

/**
 * @brief Al escribir no se tira lo que haya aprendido otro proceso.
 *
 * Es la situacion normal en una compilacion en paralelo: varios vpp aprenden
 * cosas distintas del mismo compilador a la vez.
 */
static void test_merges_concurrent_writes() {
    FactsCache primero(g_dir.string(), "huella_g");
    primero.store("de_primero", "1");

    // Otro proceso aprende otra cosa y escribe mientras el primero sigue vivo.
    {
        FactsCache segundo(g_dir.string(), "huella_g");
        segundo.store("de_segundo", "1");
    }

    primero.flush();

    FactsCache leido(g_dir.string(), "huella_g");
    std::string a, b;
    CHECK(leido.lookup("de_primero", a) && a == "1",
          "sobrevive lo que aprendio este proceso");
    CHECK(leido.lookup("de_segundo", b) && b == "1",
          "y tambien lo que aprendio el otro");
}

/** @brief Sin nada nuevo que decir, no se escribe. */
static void test_no_write_when_clean() {
    FactsCache cache(g_dir.string(), "huella_h");
    CHECK(!cache.flush(), "sin cambios no se escribe");
}

/* --- la memoria de salidas de comando ------------------------------------- */

/** @brief Un texto grande se recuerda de una pieza. */
static void test_command_output() {
    const std::string dump =
        "#define __GNUC__ 13\n#define __SIZE_TYPE__ long unsigned int\n";

    {
        const CommandOutputCache cache(g_dir.string(), "huella_i");
        CHECK(cache.enabled(), "con directorio y huella queda operativa");
        CHECK(cache.store(dump), "guarda el volcado");
    }

    const CommandOutputCache reopened(g_dir.string(), "huella_i");
    std::string out;
    CHECK(reopened.load(out), "lo recupera en la siguiente ejecucion");
    CHECK(out == dump, "y con los saltos de linea intactos");
}

/**
 * @brief Una salida vacia no se guarda.
 *
 * Una invocacion que no imprimio nada es casi siempre una que fallo, y
 * recordarla dejaria escrito ese fallo como si fuera lo que el compilador
 * predefine.
 */
static void test_command_output_rejects_empty() {
    const CommandOutputCache cache(g_dir.string(), "huella_j");
    CHECK(!cache.store(""), "no guarda una salida vacia");

    std::string out;
    CHECK(!cache.load(out), "y por tanto no hay nada que recuperar");
}

/** @brief Sin huella tampoco recuerda salidas. */
static void test_command_output_disabled() {
    const CommandOutputCache cache(g_dir.string(), "");
    CHECK(!cache.enabled(), "sin huella no queda operativa");
    CHECK(!cache.store("algo"), "y no guarda nada");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de la memoria entre ejecuciones ===\n";

    g_dir = std::filesystem::temp_directory_path() / "vpp_test_facts_cache";
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir);

    test_atomic_write();
    test_atomic_write_leaves_no_temp();
    test_store_and_lookup();
    test_persists();
    test_separated_by_fingerprint();
    test_disabled_without_fingerprint();
    test_disabled_without_dir();
    test_rejects_unstorable();
    test_ignores_foreign_file();
    test_merges_concurrent_writes();
    test_no_write_when_clean();
    test_command_output();
    test_command_output_rejects_empty();
    test_command_output_disabled();

    std::error_code ec;
    std::filesystem::remove_all(g_dir, ec);

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
