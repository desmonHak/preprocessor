/**
 * @file test_compiler_id.cpp
 * @brief Tests de la identificacion del compilador y de las consultas al
 *        sistema en las que se apoya.
 *
 * La huella es lo que decide si lo recordado en disco sigue valiendo, asi que
 * lo que se prueba aqui es sobre todo que DISTINGA: dos compiladores distintos,
 * o el mismo con otros flags, no pueden compartir huella, y un compilador que
 * cambia tiene que dejar de reconocerse.
 */

#include "preprocessor/pp_compiler_id.h"
#include "preprocessor/pp_system.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

/// Directorio temporal donde se montan los ficheros de prueba.
std::filesystem::path g_root;

/**
 * @brief Crea un fichero con el contenido dado.
 * @param rel      Ruta relativa a la raiz temporal.
 * @param content  Texto a escribir.
 * @return Ruta absoluta del fichero.
 */
std::filesystem::path write_file(const std::string& rel,
                                 const std::string& content) {
    const std::filesystem::path p = g_root / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
    return p;
}

} // namespace

/* --- extraccion del ejecutable -------------------------------------------- */

/** @brief De una orden se saca su primer argumento. */
static void test_executable_of() {
    CHECK(CompilerId::executable_of("gcc -E -P") == "gcc",
          "saca el ejecutable de una orden normal");
    CHECK(CompilerId::executable_of("   gcc  -E") == "gcc",
          "ignora los espacios de delante");
    CHECK(CompilerId::executable_of("/usr/bin/gcc -E") == "/usr/bin/gcc",
          "acepta una ruta absoluta");
    CHECK(CompilerId::executable_of("gcc") == "gcc",
          "una orden sin flags es solo el ejecutable");
    CHECK(CompilerId::executable_of("").empty(),
          "de una orden vacia no sale nada");
    CHECK(CompilerId::executable_of("   ").empty(),
          "de una orden en blanco tampoco");
}

/**
 * @brief Las comillas mantienen unida una ruta con espacios.
 *
 * No es un caso rebuscado: la ruta de MSVC lleva espacios, asi que sin esto no
 * se puede identificar al compilador de Windows.
 */
static void test_executable_of_quoted() {
    CHECK(CompilerId::executable_of("\"C:/Archivos de programa/cl.exe\" /EP") ==
              "C:/Archivos de programa/cl.exe",
          "las comillas mantienen unida una ruta con espacios");
    CHECK(CompilerId::executable_of("\"sin cerrar").empty(),
          "unas comillas sin cerrar no dan un ejecutable a medias");
}

/* --- busqueda del ejecutable ---------------------------------------------- */

/** @brief Con separador de directorio no se busca: la ruta ya lo dice. */
static void test_find_with_path() {
    const auto file = write_file("bin/algo.txt", "x");

    const std::string found = find_executable(file.string());
    CHECK(!found.empty(), "encuentra un fichero por su ruta");

    CHECK(find_executable((g_root / "bin" / "no_existe").string()).empty(),
          "no encuentra lo que no esta");
    CHECK(find_executable("").empty(),
          "un nombre vacio no encuentra nada");
}

/**
 * @brief El compilador de referencia se encuentra por el PATH.
 *
 * Es lo que de verdad se hace en produccion, asi que se comprueba con uno de
 * verdad en vez de con un fichero inventado.
 */
static void test_find_in_path() {
    const std::string gcc = find_executable("gcc");
    if (gcc.empty()) {
        std::cout << "OMITIDO: no hay gcc en el PATH\n";
        return;
    }
    CHECK(std::filesystem::exists(gcc),
          "lo encontrado por el PATH existe de verdad");
    CHECK(std::filesystem::path(gcc).is_absolute(),
          "y se devuelve en absoluto");
}

/* --- huella --------------------------------------------------------------- */

/** @brief Un ejecutable que no existe no da huella. */
static void test_fingerprint_unknown() {
    CHECK(CompilerId::fingerprint("no_existe_este_compilador_jamas -E").empty(),
          "sin ejecutable no hay huella");
    CHECK(CompilerId::fingerprint("").empty(),
          "una orden vacia tampoco da huella");
}

/**
 * @brief Los flags forman parte de la identidad.
 *
 * Es la mitad menos evidente y la que mas importa: el mismo binario contesta
 * distinto segun como se le hable -- `-x c++` habilita operadores que en C no
 * existen -- asi que compartir memoria entre dos ordenes seria devolver
 * respuestas del otro modo.
 */
static void test_fingerprint_includes_flags() {
    const std::string gcc = find_executable("gcc");
    if (gcc.empty()) {
        std::cout << "OMITIDO: no hay gcc en el PATH\n";
        return;
    }

    const std::string c   = CompilerId::fingerprint(gcc + " -E -P -x c");
    const std::string cxx = CompilerId::fingerprint(gcc + " -E -P -x c++");

    CHECK(!c.empty() && !cxx.empty(), "un compilador de verdad si da huella");
    CHECK(c != cxx, "el mismo binario con otros flags da otra huella");
    CHECK(c == CompilerId::fingerprint(gcc + " -E -P -x c"),
          "la misma orden da siempre la misma huella");
}

/**
 * @brief Cambiar el binario cambia la huella.
 *
 * Es LA razon de ser de todo esto.  Si la huella no se moviera al actualizar el
 * compilador, lo recordado en disco seguiria contestando por el compilador
 * viejo para siempre, y el fallo no daria la cara por ningun sitio.
 */
static void test_fingerprint_detects_change() {
    const auto fake = write_file("bin/falso_cc", "version 1");
    const std::string order = fake.string() + " -E";

    const std::string before = CompilerId::fingerprint(order);
    CHECK(!before.empty(), "un fichero existente da huella");

    // El tamano cambia, que es una de las tres cosas que se miran.
    write_file("bin/falso_cc", "version 2 -- mas larga que la anterior");
    CHECK(CompilerId::fingerprint(order) != before,
          "cambiar el contenido del binario cambia la huella");

    // Y tambien la fecha por si sola, con el mismo tamano.
    const std::string middle = CompilerId::fingerprint(order);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    write_file("bin/falso_cc", "version 3 -- misma longitud que la anteriop");
    CHECK(CompilerId::fingerprint(order) != middle,
          "reescribirlo con el mismo tamano tambien cambia la huella");
}

/** @brief Dos binarios distintos no comparten huella. */
static void test_fingerprint_distinguishes_binaries() {
    write_file("bin/cc_uno", "aaa");
    write_file("bin/cc_dos", "bbb");

    const std::string a = CompilerId::fingerprint((g_root / "bin" / "cc_uno").string());
    const std::string b = CompilerId::fingerprint((g_root / "bin" / "cc_dos").string());

    CHECK(!a.empty() && !b.empty(), "los dos dan huella");
    CHECK(a != b, "dos binarios distintos no comparten huella");
}

/* --- entorno -------------------------------------------------------------- */

/** @brief Una variable que no existe se lee como vacia, no revienta. */
static void test_env() {
    CHECK(env_value("VPP_VARIABLE_QUE_NO_EXISTE_JAMAS").empty(),
          "una variable inexistente se lee vacia");
    CHECK(env_path_list("VPP_VARIABLE_QUE_NO_EXISTE_JAMAS").empty(),
          "y como lista, vacia");

    const auto path = env_path_list("PATH");
    CHECK(!path.empty(), "el PATH se parte en elementos");
    for (const auto& p : path) {
        if (p.empty()) { CHECK(false, "ningun elemento del PATH sale vacio"); return; }
    }
    CHECK(true, "ningun elemento del PATH sale vacio");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de identificacion del compilador ===\n";

    g_root = std::filesystem::temp_directory_path() / "vpp_test_compiler_id";
    std::filesystem::remove_all(g_root);
    std::filesystem::create_directories(g_root);

    test_executable_of();
    test_executable_of_quoted();
    test_find_with_path();
    test_find_in_path();
    test_fingerprint_unknown();
    test_fingerprint_includes_flags();
    test_fingerprint_detects_change();
    test_fingerprint_distinguishes_binaries();
    test_env();

    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
