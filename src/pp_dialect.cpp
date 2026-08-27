/**
 * @file pp_dialect.cpp
 * @brief Implementacion de la declaracion de dialecto en el fichero.
 */

#include "preprocessor/pp_dialect.h"

#include <cctype>

namespace vpp {

namespace {

/// Texto que introduce la declaracion.
const char* const kMarca = "vpp:";

/// Cuantas lineas del principio se miran (ver kDialectHeadLines).
const int kLineasMiradas = kDialectHeadLines;

/**
 * @brief Aplica un ajuste `clave=valor`.
 * @param clave Nombre del ajuste.
 * @param valor Valor.
 * @param opts  Opciones a modificar.
 * @param loc   Ubicacion, para diagnosticos.
 * @param diag  Motor de diagnosticos.
 */
void apply_setting(const std::string& clave,
                   const std::string& valor,
                   LexerOptions&      opts,
                   const SourceLocation& loc,
                   DiagnosticEngine&  diag) {
    if (clave == "marker") {
        if (valor.empty()) {
            diag.error(loc, "vpp:marker necesita un valor");
            return;
        }
        opts.directive_marker = valor;
        return;
    }
    if (clave == "strings") {
        opts.strings = (valor != "0" && valor != "no" && valor != "false");
        return;
    }
    if (clave == "char-literals") {
        opts.char_literals = (valor != "0" && valor != "no" && valor != "false");
        return;
    }
    if (clave == "raw-strings") {
        opts.raw_strings = (valor != "0" && valor != "no" && valor != "false");
        return;
    }
    if (clave == "line-comment") {
        // Declarar la secuencia la activa; vaciarla es apagarla.  Es lo que se
        // quiere decir al escribirla, y evita tener que poner dos ajustes.
        opts.line_comment        = valor;
        opts.strip_line_comments = !valor.empty();
        return;
    }
    if (clave == "block-comment-open") {
        opts.block_comment_open   = valor;
        opts.strip_block_comments = !valor.empty();
        return;
    }
    if (clave == "block-comment-close") {
        opts.block_comment_close = valor;
        return;
    }
    if (clave == "line-comments") {
        opts.strip_line_comments =
            (valor != "0" && valor != "no" && valor != "false");
        return;
    }
    if (clave == "block-comments") {
        opts.strip_block_comments =
            (valor != "0" && valor != "no" && valor != "false");
        return;
    }

    // La declaracion es sintaxis de vpp, no del lenguaje de destino, asi que
    // una errata aqui SI es un error nuestro y se dice.
    diag.error(loc, "ajuste de dialecto desconocido: " + clave);
}

} // namespace

bool apply_dialect_line(const std::string& source,
                        LexerOptions&      opts,
                        const std::string& file,
                        DiagnosticEngine&  diag) {
    std::size_t inicio = 0;
    for (int linea = 1; linea <= kLineasMiradas && inicio <= source.size();
         ++linea) {
        std::size_t fin = source.find('\n', inicio);
        if (fin == std::string::npos) fin = source.size();
        const std::string texto = source.substr(inicio, fin - inicio);

        const std::size_t marca = texto.find(kMarca);
        if (marca != std::string::npos) {
            const SourceLocation loc(file, static_cast<uint32_t>(linea),
                                     static_cast<uint32_t>(marca + 1));

            // Del `vpp:` en adelante, pares clave=valor separados por espacios.
            // Se para en el primero que no tenga forma de ajuste, para que el
            // cierre del comentario del lenguaje -- el `-->` de HTML, por
            // ejemplo -- no cuente como uno.
            std::size_t p = marca + 4;
            while (p < texto.size()) {
                while (p < texto.size() &&
                       std::isspace((unsigned char)texto[p])) ++p;
                if (p >= texto.size()) break;

                std::size_t q = p;
                while (q < texto.size() &&
                       !std::isspace((unsigned char)texto[q]) &&
                       texto[q] != '=') ++q;
                if (q >= texto.size() || texto[q] != '=') break;

                const std::string clave = texto.substr(p, q - p);
                std::size_t v = q + 1;
                std::size_t w = v;
                while (w < texto.size() &&
                       !std::isspace((unsigned char)texto[w])) ++w;

                apply_setting(clave, texto.substr(v, w - v), opts, loc, diag);
                p = w;
            }
            return true;
        }

        if (fin >= source.size()) break;
        inicio = fin + 1;
    }

    return false;
}

} // namespace vpp
