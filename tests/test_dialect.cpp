/**
 * @file test_dialect.cpp
 * @brief Tests de la declaracion de dialecto en la cabecera del fichero.
 *
 * Lo que se prueba aqui es que vpp deje de dar por hecho que `#` marca una
 * directiva en todos los lenguajes.  En Python, shell, Ruby o Make el `#` es un
 * COMENTARIO, y tomarlo por directiva se comia lineas ajenas y ademas avisaba
 * de una "directiva desconocida" que era, en realidad, un comentario.
 *
 * Y lo que NO puede perderse por el camino: con el marcador declarado, una
 * directiva mal escrita sigue siendo un error.  Ese era el riesgo de la
 * alternativa facil -- dejar pasar lo desconocido -- porque entonces `#defien`
 * se colaria como texto.
 */

#include "preprocessor/preprocessor.h"
#include "preprocessor/pp_dialect.h"

#include <filesystem>
#include <fstream>
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

namespace {

/** @brief Indica si un texto contiene a otro. */
bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

/// Numero de errores del ultimo preproceso.
int g_errores = 0;

/**
 * @brief Preprocesa un texto contando los errores que salgan.
 * @param src Fuente.
 * @return La salida.
 */
std::string pp(const std::string& src) {
    g_errores = 0;
    Preprocessor pre([](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) ++g_errores;
    });
    return pre.process(src, "prueba.txt");
}

} // namespace

/* --- el marcador ---------------------------------------------------------- */

/**
 * @brief Un fichero de Python conserva sus comentarios y usa su marcador.
 *
 * Es el caso que motiva todo esto: sin declarar el dialecto, cada `#` de
 * comentario se tomaba por una directiva desconocida y la linea desaparecia de
 * la salida.
 */
static void test_python_conserva_sus_comentarios() {
    const std::string out = pp(
        "#!/usr/bin/env python3\n"
        "# vpp:marker=%\n"
        "# esto es un comentario y tiene que sobrevivir\n"
        "%define DOBLE 2\n"
        "def f(x): return x * DOBLE\n");

    CHECK(contains(out, "# esto es un comentario"),
          "el comentario de Python llega intacto a la salida");
    CHECK(contains(out, "#!/usr/bin/env python3"),
          "y el shebang de delante tambien");
    CHECK(contains(out, "return x * 2"),
          "la directiva con el marcador declarado si se aplica");
    CHECK(!contains(out, "%define"),
          "y la propia directiva no sale");
    CHECK(g_errores == 0, "sin errores");
}

/**
 * @brief Con el marcador declarado, una errata SIGUE siendo un error.
 *
 * Es la propiedad que no se podia perder.  El marcador es inequivoco, asi que
 * algo que empieza por el y no es ninguna directiva conocida solo puede ser una
 * equivocacion.
 */
static void test_errata_sigue_siendo_error() {
    pp("# vpp:marker=%\n%defien FOO 1\n");
    CHECK(g_errores == 1, "una directiva mal escrita es un error");
}

/** @brief Y tambien con el marcador por omision. */
static void test_errata_con_marcador_por_omision() {
    pp("#defien FOO 1\n");
    CHECK(g_errores == 1, "lo mismo con el marcador de siempre");
}

/** @brief Sin declaracion, todo se comporta como antes. */
static void test_sin_declaracion_es_c() {
    const std::string out = pp("#define A 3\nvalor A\n");
    CHECK(contains(out, "valor 3"), "sin declaracion el marcador sigue siendo #");
    CHECK(g_errores == 0, "y no hay errores");
}

/* --- donde puede ir la declaracion ---------------------------------------- */

/**
 * @brief Se reconoce dentro del comentario de cualquier lenguaje.
 *
 * Es lo que rompe el circulo vicioso: no hace falta saber el lenguaje para leer
 * la linea que dice cual es el lenguaje.  Se busca el texto `vpp:` sin exigir
 * nada a su alrededor.
 */
static void test_declaracion_en_cualquier_comentario() {
    struct Caso { const char* nombre; const char* linea; };
    const Caso casos[] = {
        { "comentario de Lua o SQL",  "-- vpp:marker=%"        },
        { "comentario de C",          "// vpp:marker=%"        },
        { "comentario de HTML",       "<!-- vpp:marker=% -->"  },
        { "comentario de Lisp",       ";; vpp:marker=%"        },
    };

    for (const auto& c : casos) {
        const std::string out =
            pp(std::string(c.linea) + "\n%define Q 9\nvalor Q\n");
        CHECK(contains(out, "valor 9"),
              std::string("la declaracion se lee desde un ") + c.nombre);
    }
}

/**
 * @brief Solo cuenta al principio del fichero.
 *
 * Si valiera en cualquier sitio, el texto `vpp:` en mitad de un fuente
 * cambiaria las reglas por accidente.
 */
static void test_declaracion_solo_al_principio() {
    const std::string out = pp(
        "uno\ndos\ntres\ncuatro\n# vpp:marker=%\n#define Z 1\nvalor Z\n");
    CHECK(contains(out, "valor 1"),
          "una declaracion tardia no cambia el marcador");
    CHECK(g_errores == 0, "y el fichero se procesa como C");
}

/** @brief Un ajuste mal escrito en la declaracion es un error nuestro. */
static void test_ajuste_desconocido() {
    pp("# vpp:markr=%\n");
    CHECK(g_errores == 1, "un ajuste de dialecto desconocido es un error");
}


/* --- comillas ------------------------------------------------------------- */

/**
 * @brief Sin literales de caracter, un apostrofo es un apostrofo.
 *
 * Con el tratamiento de C, `It's a test` fallaba con "literal de cadena sin
 * cerrar": una frase corriente en ingles.  Le pasa a cualquier prosa, y a
 * cualquier lenguaje donde la comilla simple no delimite nada.
 */
static void test_apostrofo_en_texto() {
    const std::string out = pp(
        "// vpp:char-literals=0\nIt's a test, don't panic\nsegunda linea\n");
    CHECK(contains(out, "It's a test, don't panic"),
          "el texto con apostrofos llega intacto");
    CHECK(contains(out, "segunda linea"), "y la linea siguiente tambien");
    CHECK(g_errores == 0, "sin errores");
}

/** @brief Y sin cadenas, una comilla doble suelta tampoco rompe nada. */
static void test_comilla_suelta_en_texto() {
    const std::string out = pp(
        "// vpp:strings=0\nuna \" suelta y ya\notra linea\n");
    CHECK(contains(out, "una \" suelta y ya"),
          "una comilla doble sin pareja es texto");
    CHECK(g_errores == 0, "sin errores");
}

/**
 * @brief Las directivas conservan su sintaxis aunque el texto no.
 *
 * `strings` y `char-literals` hablan del texto del lenguaje de destino; dentro
 * de una directiva rige la sintaxis de vpp.  Si no, apagarlas romperia
 * `#include "fichero.h"`, que es justo lo que se necesita en el lenguaje para
 * el que se apagan.
 */
static void test_directivas_conservan_sus_cadenas() {
    const std::string out = pp(
        "// vpp:strings=0 char-literals=0\n"
        "#define S \"hola\"\n"
        "valor S y un ' suelto\n");
    CHECK(contains(out, "valor \"hola\""),
          "una directiva sigue leyendo su cadena");
    CHECK(contains(out, "un ' suelto"), "y el texto sigue siendo texto");
    CHECK(g_errores == 0, "sin errores");
}

/** @brief Por omision se comportan como en C. */
static void test_comillas_por_omision() {
    const std::string out = pp(
        "#define M 1\nauto s = \"aqui M no se expande\";\n");
    CHECK(contains(out, "\"aqui M no se expande\""),
          "por omision una cadena protege su contenido");
    CHECK(g_errores == 0, "sin errores");
}
/* --- alcance -------------------------------------------------------------- */

/**
 * @brief El dialecto es de cada fichero y no se hereda al incluir.
 *
 * Sin esto, un fuente en Python no podria incluir una cabecera de C: se
 * intentaria leerla con el marcador del que la incluye y ninguna de sus
 * directivas se reconoceria.
 */
static void test_dialecto_por_fichero() {
    namespace fs = std::filesystem;
    const fs::path raiz = fs::temp_directory_path() / "vpp_test_dialecto";
    fs::remove_all(raiz);
    fs::create_directories(raiz);
    {
        std::ofstream ofs(raiz / "cabecera.h", std::ios::binary);
        ofs << "#define DESDE_C 42\n";
    }

    g_errores = 0;
    Preprocessor pre([](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) ++g_errores;
    });
    pre.options().include_paths.push_back(raiz.string());

    const std::string out = pre.process(
        "# vpp:marker=%\n"
        "# comentario de Python\n"
        "%include \"cabecera.h\"\n"
        "valor DESDE_C\n", "prueba.py");

    CHECK(contains(out, "valor 42"),
          "un fichero con otro marcador puede incluir una cabecera de C");
    CHECK(contains(out, "# comentario de Python"),
          "y conserva sus propios comentarios");
    CHECK(g_errores == 0, "sin errores");

    std::error_code ec;
    fs::remove_all(raiz, ec);
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de dialecto del preprocesador vpp ===\n";

    test_python_conserva_sus_comentarios();
    test_errata_sigue_siendo_error();
    test_errata_con_marcador_por_omision();
    test_sin_declaracion_es_c();
    test_declaracion_en_cualquier_comentario();
    test_declaracion_solo_al_principio();
    test_ajuste_desconocido();
    test_apostrofo_en_texto();
    test_comilla_suelta_en_texto();
    test_directivas_conservan_sus_cadenas();
    test_comillas_por_omision();
    test_dialecto_por_fichero();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
