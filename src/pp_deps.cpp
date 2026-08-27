/**
 * @file pp_deps.cpp
 * @brief Implementacion de la emision de dependencias.
 */

#include "preprocessor/pp_deps.h"

#include <filesystem>

namespace vpp {

namespace {

/// Columna a partir de la cual se parte la linea, como hace cc.
const std::size_t kAncho = 76;

/**
 * @brief Deja una ruta como make espera verla.
 *
 * Dos cosas.  Las barras invertidas pasan a normales: en un Makefile la barra
 * invertida es un escape, asi que una ruta de Windows tal cual se leeria mal.
 * Y se escapan el espacio y el dolar, que make interpreta.
 *
 * @param path Ruta original.
 * @return La ruta lista para escribirla en la regla.
 */
std::string escape_path(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (const char c : path) {
        switch (c) {
            case '\\': out += '/';   break;
            case ' ':  out += "\\ "; break;
            case '$':  out += "$$";  break;
            default:   out += c;     break;
        }
    }
    return out;
}

/**
 * @brief Anade un elemento a la regla, partiendo la linea si se alarga.
 * @param out      Texto de la regla.
 * @param columna  Columna actual, que se actualiza.
 * @param elemento Texto ya escapado.
 */
void append_item(std::string& out, std::size_t& columna,
                 const std::string& elemento) {
    if (columna + elemento.size() + 1 > kAncho) {
        out += " \\\n ";
        columna = 1;
    }
    out += ' ';
    out += elemento;
    columna += elemento.size() + 1;
}

/**
 * @brief Deduce el objetivo cuando no se dio ninguno.
 *
 * Se prefiere el fichero de salida: es lo que esta ejecucion produce, y por
 * tanto lo que hay que rehacer.  Sin el se cae a lo que hace cc -- el nombre
 * del fuente con la extension cambiada -- que es lo que espera quien viene de
 * un Makefile de toda la vida.
 *
 * @param target_hint Fichero de salida, si lo hubo.
 * @param source      Fuente principal.
 * @return El objetivo de la regla.
 */
std::string deduce_target(const std::string& target_hint,
                          const std::string& source) {
    if (!target_hint.empty()) return target_hint;
    std::filesystem::path p(source);
    p.replace_extension(".o");
    return p.filename().string();
}

} // namespace

std::string format_make_deps(const std::string&              target_hint,
                             const std::string&              source,
                             const std::vector<std::string>& included,
                             const DepsOptions&              opts) {
    const std::string objetivo = opts.target.empty()
                                   ? deduce_target(target_hint, source)
                                   : opts.target;

    std::string out = escape_path(objetivo);
    out += ':';
    std::size_t columna = out.size();

    // El propio fuente es una dependencia, y va primero.
    if (!source.empty()) append_item(out, columna, escape_path(source));
    for (const auto& dep : included) {
        append_item(out, columna, escape_path(dep));
    }
    out += '\n';

    /* Objetivos ficticios: una regla vacia por cada dependencia.
     *
     * Existen para que borrar una cabecera no rompa el build.  Sin ellos, make
     * lee la regla vieja, ve una dependencia que ya no existe, y se para
     * diciendo que no sabe construirla -- cuando en realidad ya no hace falta,
     * porque el fuente dejo de incluirla.  Con la regla vacia, make se encoge de
     * hombros y rehace lo que dependia de ella.
     *
     * El fuente principal se queda fuera a proposito: si ESE falta, pararse es
     * lo correcto. */
    if (opts.phony_targets) {
        for (const auto& dep : included) {
            out += escape_path(dep);
            out += ":\n";
        }
    }

    return out;
}

} // namespace vpp
