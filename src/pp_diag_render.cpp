/**
 * @file pp_diag_render.cpp
 * @brief Implementacion de la presentacion de diagnosticos.
 */

#include "preprocessor/pp_diag_render.h"

#include <sstream>

namespace vpp {

namespace {

/// Secuencias ANSI, solo cuando se pidan.
const char* const kRojo    = "\x1b[1;31m";
const char* const kAmarillo= "\x1b[1;33m";
const char* const kAzul    = "\x1b[1;34m";
const char* const kNegrita = "\x1b[1m";
const char* const kFin     = "\x1b[0m";

/**
 * @brief Ancho minimo reservado al numero de linea.
 *
 * Fijo para que varios mensajes del mismo fichero queden alineados entre si,
 * aunque unos esten en la linea 7 y otros en la 1200.
 */
const std::size_t kAnchoMargen = 4;

/**
 * @brief Color con el que se resalta un nivel.
 * @param level Nivel del diagnostico.
 * @return La secuencia ANSI.
 */
const char* level_color(DiagLevel level) {
    switch (level) {
        case DiagLevel::ERR:
        case DiagLevel::FATAL:   return kRojo;
        case DiagLevel::WARNING: return kAmarillo;
        default:                 return kAzul;
    }
}

/**
 * @brief Escribe la barra del margen izquierdo.
 * @param oss   Donde escribir.
 * @param ancho Ancho reservado para el numero de linea.
 * @param texto Numero de linea, o espacios para las lineas de adorno.
 * @param color true para resaltar.
 */
void margin(std::ostringstream& oss, std::size_t ancho,
            const std::string& texto, bool color) {
    if (color) oss << kAzul;
    const std::size_t w = (ancho > texto.size()) ? ancho - texto.size() : 0;
    oss << std::string(w, ' ') << texto << " |";
    if (color) oss << kFin;
}

} // namespace

std::string render_diagnostic(const Diagnostic& d,
                              const SourceMap&  sources,
                              bool              color) {
    std::ostringstream oss;

    // Primera linea: donde, que clase y que pasa.  Es la de siempre.
    if (color) oss << kNegrita;
    oss << d.loc.to_string() << ": ";
    if (color) oss << kFin << level_color(d.level);
    oss << diag_level_name(d.level);
    if (color) oss << kFin;
    oss << ": ";
    if (color) oss << kNegrita;
    oss << d.message;
    if (color) oss << kFin;

    // Y debajo, la linea culpable con el cursor.  Sin el fuente no hay nada que
    // ensenar, y se devuelve solo lo de arriba: menos es mejor que nada.
    std::string texto;
    if (!sources.line(d.loc.file(), d.loc.line, texto)) return oss.str();

    const std::string numero = std::to_string(d.loc.line);
    oss << '\n';
    const std::size_t ancho = (numero.size() > kAnchoMargen) ? numero.size()
                                                             : kAnchoMargen;
    margin(oss, ancho, numero, color);
    oss << ' ' << texto << '\n';

    /* El cursor va bajo la columna que senala el diagnostico.
     *
     * Se copian los tabuladores del original en lugar de contarlos como un
     * caracter: si la linea esta indentada con tabuladores y aqui se ponen
     * espacios, el cursor acaba senalando a otro sitio, que es peor que no
     * ponerlo. */
    margin(oss, ancho, std::string(), color);
    oss << ' ';
    const std::size_t col = (d.loc.col > 0) ? d.loc.col - 1 : 0;
    for (std::size_t i = 0; i < col; ++i) {
        oss << ((i < texto.size() && texto[i] == '\t') ? '\t' : ' ');
    }
    if (color) oss << level_color(d.level);
    oss << '^';
    if (color) oss << kFin;

    return oss.str();
}

} // namespace vpp
