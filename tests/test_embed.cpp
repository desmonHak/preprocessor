/**
 * @file test_embed.cpp
 * @brief Tests de la directiva #embed (C23) y de __has_embed.
 *
 * Incrusta un recurso binario como lista de bytes, para meter un icono, una
 * tabla o una clave dentro del fuente sin generar el fichero intermedio con un
 * script aparte.
 *
 * Las expectativas van fijadas aqui y NO en el corpus diferencial: #embed llego
 * en GCC 15, asi que compararlo contra el compilador de la maquina pasaria en
 * unos sistemas y fallaria en otros.  Lo que hay abajo se comprobo antes contra
 * gcc 15.2, incluidos los casos raros -- que un recurso vacio no emite ni
 * prefijo ni sufijo, y que limit(0) cuenta como vacio.
 */

#include "preprocessor/preprocessor.h"

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

/// Raiz temporal donde viven los recursos de prueba.
std::filesystem::path g_raiz;

/// Errores del ultimo preproceso.
int g_errores = 0;

/** @brief Indica si un texto contiene a otro. */
bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

/**
 * @brief Escribe un recurso binario.
 * @param nombre Nombre del fichero dentro de la raiz.
 * @param bytes  Contenido, byte a byte.
 */
void recurso(const std::string& nombre, const std::string& bytes) {
    std::ofstream ofs(g_raiz / nombre, std::ios::binary);
    ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/**
 * @brief Preprocesa un fuente con la raiz temporal como ruta de busqueda.
 * @param src Fuente.
 * @return La salida.
 */
std::string pp(const std::string& src) {
    g_errores = 0;
    Preprocessor pre([](const Diagnostic& d) {
        if (d.level == DiagLevel::ERR) ++g_errores;
    });
    pre.options().include_paths.push_back(g_raiz.string());
    return pre.process(src, "t.c");
}

} // namespace

/* --- lo basico ------------------------------------------------------------ */

/** @brief Los bytes salen como enteros separados por comas. */
static void test_basico() {
    recurso("abc.bin", "abc");
    const std::string out = pp("int a[]={\n#embed \"abc.bin\"\n};\n");
    CHECK(contains(out, "97,98,99"), "los bytes salen como enteros con comas");
    CHECK(g_errores == 0, "sin errores");
}

/**
 * @brief Datos binarios de verdad: bytes altos y ceros.
 *
 * Es lo que distingue leer un recurso de leer texto.  Un byte por encima de 127
 * tiene que salir sin signo -- 255, no -1 -- y un cero no puede cortar la
 * lectura como si terminara una cadena.
 */
static void test_datos_binarios() {
    const std::string bytes("\x00\x01\x7F\x80\xFF", 5);
    recurso("bin.dat", bytes);

    const std::string out = pp("int a[]={\n#embed \"bin.dat\"\n};\n");
    CHECK(contains(out, "0,1,127,128,255"),
          "los bytes altos salen sin signo y el cero no corta");
    CHECK(!contains(out, "-1"), "ninguno sale negativo");
}

/** @brief Sin el salto final, los datos se pegaban a la linea siguiente. */
static void test_ocupa_su_linea() {
    recurso("abc.bin", "abc");
    const std::string out = pp("uno\n#embed \"abc.bin\"\ntres\n");
    CHECK(contains(out, "97,98,99\n"),
          "los datos terminan en salto de linea");
    CHECK(!contains(out, "99tres"),
          "y no se pegan al texto de la linea siguiente");
}

/* --- parametros ----------------------------------------------------------- */

/** @brief `limit` recorta cuantos bytes se leen. */
static void test_limit() {
    recurso("abc.bin", "abc");
    const std::string out = pp("int a[]={\n#embed \"abc.bin\" limit(2)\n};\n");
    CHECK(contains(out, "97,98"), "limit(2) deja dos bytes");
    CHECK(!contains(out, "99"),   "y descarta el resto");
}

/** @brief El limite puede ser una macro, porque se evalua como expresion. */
static void test_limit_con_macro() {
    recurso("abc.bin", "abc");
    const std::string out =
        pp("#define N 1\nint a[]={\n#embed \"abc.bin\" limit(N)\n};\n");
    CHECK(contains(out, "97"),  "limit acepta una macro");
    CHECK(!contains(out, "98"), "y la respeta");
}

/**
 * @brief `limit(0)` cuenta como recurso vacio.
 *
 * Medido contra gcc: no emite nada, ni siquiera el prefijo.
 */
static void test_limit_cero() {
    recurso("abc.bin", "abc");
    const std::string out =
        pp("int a[]={\n#embed \"abc.bin\" limit(0) prefix(1,)\n};\n");
    CHECK(!contains(out, "97"), "limit(0) no emite datos");
    CHECK(!contains(out, "1,"), "ni el prefijo");
}

/** @brief `if_empty` sustituye a todo cuando no hay datos. */
static void test_if_empty() {
    recurso("vacio.bin", "");
    const std::string con = pp("int a[]={\n#embed \"vacio.bin\" if_empty(9)\n};\n");
    CHECK(contains(con, "9"), "un recurso vacio emite lo que diga if_empty");

    const std::string sin = pp("int a[]={\n#embed \"vacio.bin\"\n};\n");
    CHECK(!contains(sin, "9"), "y sin if_empty no emite nada");
    CHECK(g_errores == 0, "un recurso vacio no es un error");
}

/**
 * @brief `prefix` y `suffix` NO se emiten si el recurso esta vacio.
 *
 * Tiene sentido y esta medido contra gcc: el prefijo suele ser el separador que
 * une los datos con lo que tienen al lado, y sin datos sobra.
 */
static void test_prefix_suffix() {
    recurso("abc.bin", "abc");
    const std::string con = pp(
        "int a[]={\n#embed \"abc.bin\" prefix(1,) suffix(,2)\n};\n");
    CHECK(contains(con, "1,97,98,99,2"),
          "con datos se emiten prefijo y sufijo");

    recurso("vacio.bin", "");
    const std::string sin = pp(
        "int a[]={\n#embed \"vacio.bin\" prefix(1,) suffix(,2)\n};\n");
    CHECK(!contains(sin, "1"), "sin datos no se emite el prefijo");
    CHECK(!contains(sin, "2"), "ni el sufijo");
}

/* --- errores -------------------------------------------------------------- */

/** @brief Un recurso que no aparece es un error, no un silencio. */
static void test_no_encontrado() {
    pp("int a[]={\n#embed \"no_existe_jamas.bin\"\n};\n");
    CHECK(g_errores == 1, "un recurso que falta se diagnostica");
}

/** @brief Una directiva sin recurso tambien. */
static void test_sin_recurso() {
    pp("#embed\n");
    CHECK(g_errores == 1, "#embed sin recurso es un error");
}

/* --- __has_embed ---------------------------------------------------------- */

/**
 * @brief Devuelve TRES valores, no dos.
 *
 * 1 si esta y tiene datos, 2 si esta y esta vacio, 0 si no esta.  La diferencia
 * importa: un recurso vacio no se puede incrustar igual que uno con datos, y
 * quien pregunta necesita distinguirlo.
 */
static void test_has_embed() {
    recurso("abc.bin", "abc");
    recurso("vacio.bin", "");

    CHECK(contains(pp("#if __has_embed(\"abc.bin\") == 1\nSI\n#endif\n"), "SI"),
          "un recurso con datos da 1");
    CHECK(contains(pp("#if __has_embed(\"vacio.bin\") == 2\nSI\n#endif\n"), "SI"),
          "uno vacio da 2");
    CHECK(contains(pp("#if __has_embed(\"no_esta.bin\") == 0\nSI\n#endif\n"), "SI"),
          "y uno que no esta da 0");
    CHECK(g_errores == 0, "preguntar por uno que no esta NO es un error");
}

/** @brief Sirve para decidir si se puede incrustar. */
static void test_has_embed_como_guarda() {
    recurso("abc.bin", "abc");
    const std::string out = pp(
        "#if __has_embed(\"abc.bin\")\n"
        "int a[]={\n#embed \"abc.bin\"\n};\n"
        "#else\n"
        "int a[]={0};\n"
        "#endif\n");
    CHECK(contains(out, "97,98,99"), "con el recurso presente se incrusta");
    CHECK(!contains(out, "{0}"),     "y no se toma la rama de reserva");
}

/* --- main ----------------------------------------------------------------- */

int main() {
    std::cout << "=== Tests de #embed ===\n";

    g_raiz = std::filesystem::temp_directory_path() / "vpp_test_embed";
    std::filesystem::remove_all(g_raiz);
    std::filesystem::create_directories(g_raiz);

    test_basico();
    test_datos_binarios();
    test_ocupa_su_linea();
    test_limit();
    test_limit_con_macro();
    test_limit_cero();
    test_if_empty();
    test_prefix_suffix();
    test_no_encontrado();
    test_sin_recurso();
    test_has_embed();
    test_has_embed_como_guarda();

    std::error_code ec;
    std::filesystem::remove_all(g_raiz, ec);

    std::cout << "\nResultados: " << tests_passed << " pasados, "
              << tests_failed  << " fallados\n";
    return tests_failed > 0 ? 1 : 0;
}
