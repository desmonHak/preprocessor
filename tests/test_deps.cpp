/**
 * @file test_deps.cpp
 * @brief Tests de la lista de dependencias en formato de make.
 *
 * Es lo que permite que un build incremental sepa que hay que rehacer cuando
 * cambia una cabecera.  Sin ella, quien use vpp solo puede recompilar siempre o
 * equivocarse.
 *
 * Se prueba el formateo por separado del preproceso: la regla tiene que salir
 * bien escapada y bien partida sea cual sea la lista, y eso se comprueba mejor
 * dandole las listas a mano.
 */

#include "preprocessor/pp_deps.h"

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

} // namespace

/* --- forma de la regla ---------------------------------------------------- */

/**
 * @brief La regla lista el fuente y todo lo que incluye.
 *
 * Comprobado ademas contra `gcc -M`, que da exactamente el mismo texto.
 */
static void test_regla_basica() {
    DepsOptions opts;
    const std::string r = format_make_deps(
        "", "main.c", { "a.h", "c.h", "inc/b.h" }, opts);

    CHECK(r == "main.o: main.c a.h c.h inc/b.h\n",
          "la regla lleva el objetivo, el fuente y las cabeceras");
}

/**
 * @brief Sin objetivo se prefiere el fichero de salida.
 *
 * Es lo que esta ejecucion produce y por tanto lo que hay que rehacer.  Solo
 * cuando no hay salida se cae al nombre del fuente con otra extension, que es
 * lo que hace cc.
 */
static void test_objetivo_deducido() {
    DepsOptions opts;
    CHECK(contains(format_make_deps("salida.i", "main.c", {}, opts), "salida.i:"),
          "sin objetivo explicito se usa el fichero de salida");
    CHECK(contains(format_make_deps("", "main.c", {}, opts), "main.o:"),
          "y sin salida, el fuente con la extension cambiada");
}

/** @brief Un objetivo explicito gana a todo. */
static void test_objetivo_explicito() {
    DepsOptions opts;
    opts.target = "obj/main.o";
    CHECK(contains(format_make_deps("salida.i", "main.c", {}, opts),
                   "obj/main.o:"),
          "el objetivo explicito manda sobre el deducido");
}

/* --- objetivos ficticios -------------------------------------------------- */

/**
 * @brief Cada dependencia recibe una regla vacia.
 *
 * Existen para que borrar una cabecera no rompa el build: sin ellas make lee la
 * regla vieja, ve un fichero que ya no esta y se para, cuando en realidad ya no
 * hace falta.  El fuente principal se queda fuera a proposito: si ESE falta,
 * pararse es lo correcto.
 */
static void test_objetivos_ficticios() {
    DepsOptions opts;
    opts.phony_targets = true;
    const std::string r = format_make_deps("", "main.c", { "a.h", "b.h" }, opts);

    CHECK(contains(r, "\na.h:\n"), "cada cabecera recibe su regla vacia");
    CHECK(contains(r, "\nb.h:\n"), "todas ellas");
    CHECK(!contains(r, "\nmain.c:\n"),
          "el fuente principal no recibe una: si falta, hay que pararse");
}

/* --- escapado ------------------------------------------------------------- */

/**
 * @brief Las barras invertidas pasan a normales.
 *
 * En un Makefile la barra invertida es un escape, asi que una ruta de Windows
 * tal cual se leeria mal.
 */
static void test_barras_de_windows() {
    DepsOptions opts;
    const std::string r = format_make_deps(
        "", "src\\main.c", { "inc\\a.h" }, opts);
    CHECK(contains(r, "src/main.c"),   "la ruta del fuente se normaliza");
    CHECK(contains(r, "inc/a.h"),      "y la de las cabeceras tambien");
    CHECK(!contains(r, "\\"),          "no queda ninguna barra invertida");
}

/** @brief Los espacios y el dolar se escapan, que make los interpreta. */
static void test_escapes_de_make() {
    DepsOptions opts;
    const std::string r = format_make_deps(
        "", "con espacio.c", { "con$dolar.h" }, opts);
    CHECK(contains(r, "con\\ espacio.c"), "el espacio va escapado");
    CHECK(contains(r, "con$$dolar.h"),    "y el dolar duplicado");
}

/* --- partido de lineas ---------------------------------------------------- */

/**
 * @brief Una lista larga se parte con la barra de continuacion.
 *
 * Igual que cc.  Una sola linea kilometrica es dificil de leer y algunas
 * herramientas viejas no la tragan.
 */
static void test_lineas_largas() {
    DepsOptions opts;
    std::vector<std::string> muchas;
    for (int i = 0; i < 20; ++i) {
        muchas.push_back("un/directorio/bastante/largo/cabecera" +
                         std::to_string(i) + ".h");
    }
    const std::string r = format_make_deps("", "main.c", muchas, opts);

    CHECK(contains(r, " \\\n "), "la regla se parte en varias lineas");

    // Ninguna linea deberia pasarse de largo.
    std::size_t inicio = 0;
    bool todas_cortas = true;
    while (inicio < r.size()) {
        std::size_t fin = r.find('\n', inicio);
        if (fin == std::string::npos) fin = r.size();
        if (fin - inicio > 80) { todas_cortas = false; break; }
        inicio = fin + 1;
    }
    CHECK(todas_cortas, "y ninguna linea se desmadra");

    // Y no se pierde ninguna dependencia por el camino.
    bool todas = true;
    for (const auto& m : muchas) if (!contains(r, m)) { todas = false; break; }
    CHECK(todas, "estan todas las dependencias");
}

/** @brief Un fuente sin inclusiones da una regla con solo el fuente. */
static void test_sin_inclusiones() {
    DepsOptions opts;
    CHECK(format_make_deps("", "solo.c", {}, opts) == "solo.o: solo.c\n",
          "sin inclusiones la regla lleva solo el fuente");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de dependencias del preprocesador vpp ===\n";

    test_regla_basica();
    test_objetivo_deducido();
    test_objetivo_explicito();
    test_objetivos_ficticios();
    test_barras_de_windows();
    test_escapes_de_make();
    test_lineas_largas();
    test_sin_inclusiones();

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
