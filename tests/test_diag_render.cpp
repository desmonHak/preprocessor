/**
 * @file test_diag_render.cpp
 * @brief Tests de la presentacion de diagnosticos con contexto.
 *
 * Un mensaje que solo da `fichero:linea:columna` obliga a quien lo lee a abrir
 * el fichero y contar.  Citar la linea y senalar la columna es la diferencia
 * entre un diagnostico que se arregla y uno que se investiga.
 */

#include "preprocessor/pp_diag_render.h"
#include "preprocessor/pp_source_map.h"

#include <iostream>
#include <string>
#include <vector>

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

/**
 * @brief Construye un diagnostico de prueba.
 * @param file  Fichero.
 * @param line  Linea.
 * @param col   Columna.
 * @param msg   Mensaje.
 * @return El diagnostico.
 */
Diagnostic diag(const std::string& file, uint32_t line, uint32_t col,
                const std::string& msg) {
    return Diagnostic(DiagLevel::ERR, SourceLocation(file, line, col), msg);
}

/** @brief Devuelve la linea n-esima de un texto (base 0). */
std::string nth_line(const std::string& s, std::size_t n) {
    std::size_t inicio = 0;
    for (std::size_t i = 0; ; ++i) {
        std::size_t fin = s.find('\n', inicio);
        if (fin == std::string::npos) fin = s.size();
        if (i == n) return s.substr(inicio, fin - inicio);
        if (fin >= s.size()) return {};
        inicio = fin + 1;
    }
}

} // namespace

/* --- el almacen de fuentes ------------------------------------------------ */

/** @brief Devuelve la linea pedida, sin el salto. */
static void test_source_map_linea() {
    SourceMap m;
    m.add("t.c", "uno\ndos\ntres\n");

    std::string l;
    CHECK(m.line("t.c", 1, l) && l == "uno",  "devuelve la primera linea");
    CHECK(m.line("t.c", 2, l) && l == "dos",  "y una del medio");
    CHECK(m.line("t.c", 3, l) && l == "tres", "y la ultima");
}

/** @brief Pedir de mas, o de un fichero que no esta, no devuelve nada. */
static void test_source_map_limites() {
    SourceMap m;
    m.add("t.c", "uno\ndos\n");

    std::string l;
    CHECK(!m.line("t.c", 9, l),    "una linea que no existe no se inventa");
    CHECK(!m.line("t.c", 0, l),    "la linea cero tampoco: se cuenta desde 1");
    CHECK(!m.line("otro.c", 1, l), "ni un fichero que no se registro");
}

/**
 * @brief El retorno de carro no llega a la linea.
 *
 * Si se colara, ensuciaria la salida y descuadraria el cursor de debajo.
 */
static void test_source_map_crlf() {
    SourceMap m;
    m.add("t.c", "uno\r\ndos\r\n");

    std::string l;
    CHECK(m.line("t.c", 1, l) && l == "uno", "el retorno de carro se descarta");
}

/* --- la presentacion ------------------------------------------------------ */

/**
 * @brief Con el fuente disponible se cita la linea y se senala la columna.
 */
static void test_render_con_contexto() {
    SourceMap m;
    m.add("t.c", "int a = 1;\n#defien FOO 1\nint b = 2;\n");

    const std::string out =
        render_diagnostic(diag("t.c", 2, 1, "directiva desconocida"), m, false);

    CHECK(contains(out, "t.c:2:1: error: directiva desconocida"),
          "la primera linea dice donde y que paso");
    CHECK(contains(out, "#defien FOO 1"), "debajo se cita la linea culpable");
    CHECK(contains(out, "^"),             "y se senala con un cursor");
}

/** @brief El cursor cae justo bajo la columna indicada. */
static void test_cursor_en_su_columna() {
    SourceMap m;
    m.add("t.c", "0123456789\n");

    const std::string out =
        render_diagnostic(diag("t.c", 1, 5, "aqui"), m, false);

    const std::string linea_cursor = nth_line(out, 2);
    const std::size_t pos = linea_cursor.find('^');
    const std::size_t pos_texto = nth_line(out, 1).find('0');

    CHECK(pos != std::string::npos, "hay cursor");
    CHECK(pos == pos_texto + 4,
          "y cae bajo el caracter de la columna 5");
}

/**
 * @brief Con tabuladores, el cursor copia los tabuladores del original.
 *
 * Contarlos como un caracter y poner espacios deja el cursor senalando a otro
 * sitio, que es peor que no ponerlo.
 */
static void test_cursor_con_tabuladores() {
    SourceMap m;
    m.add("t.c", "\t\tx\n");

    const std::string out =
        render_diagnostic(diag("t.c", 1, 3, "aqui"), m, false);

    const std::string linea_cursor = nth_line(out, 2);
    // Tras la barra del margen tiene que haber los mismos dos tabuladores.
    const std::size_t barra = linea_cursor.find('|');
    CHECK(barra != std::string::npos, "la linea del cursor lleva su margen");
    CHECK(linea_cursor.substr(barra + 1) == " \t\t^",
          "el cursor reproduce los tabuladores en vez de contarlos como uno");
}

/**
 * @brief Sin el fuente se da el mensaje de siempre, sin adornos.
 *
 * Menos es mejor que nada: hay fuentes que no estan en el disco -- llegan por
 * la API, por la entrada estandar o de un `#exec` -- y a esos no se puede
 * volver.
 */
static void test_render_sin_fuente() {
    SourceMap m;   // vacio

    const std::string out =
        render_diagnostic(diag("t.c", 2, 1, "algo paso"), m, false);

    CHECK(out == "t.c:2:1: error: algo paso",
          "sin el fuente se devuelve solo la linea de siempre");
}

/** @brief Sin color no se cuela ninguna secuencia de escape. */
static void test_sin_color() {
    SourceMap m;
    m.add("t.c", "hola\n");
    const std::string out =
        render_diagnostic(diag("t.c", 1, 1, "x"), m, false);
    CHECK(!contains(out, "\x1b["), "sin color no hay secuencias ANSI");
}

/** @brief Y con color, si. */
static void test_con_color() {
    SourceMap m;
    m.add("t.c", "hola\n");
    const std::string out =
        render_diagnostic(diag("t.c", 1, 1, "x"), m, true);
    CHECK(contains(out, "\x1b["), "con color si las hay");
    CHECK(contains(out, "hola"),  "y la linea sigue estando");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de presentacion de diagnosticos ===\n";

    test_source_map_linea();
    test_source_map_limites();
    test_source_map_crlf();
    test_render_con_contexto();
    test_cursor_en_su_columna();
    test_cursor_con_tabuladores();
    test_render_sin_fuente();
    test_sin_color();
    test_con_color();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
