/**
 * @file pp_source_map.cpp
 * @brief Implementacion del almacen de fuentes.
 */

#include "preprocessor/pp_source_map.h"

namespace vpp {

void SourceMap::add(const std::string& file, const std::string& text) {
    if (file.empty()) return;
    m_sources.emplace(file, text);
}

bool SourceMap::line(const std::string& file, uint32_t line,
                     std::string& out) const {
    if (line == 0) return false;

    const auto it = m_sources.find(file);
    if (it == m_sources.end()) return false;

    // Se recorre hasta la linea pedida en vez de guardar un indice de saltos.
    // Esto se hace UNA vez por diagnostico, y los diagnosticos son raros; un
    // indice costaria memoria en todas las ejecuciones para acelerar las que
    // van mal.
    const std::string& text = it->second;
    std::size_t inicio = 0;
    for (uint32_t n = 1; ; ++n) {
        std::size_t fin = text.find('\n', inicio);
        if (fin == std::string::npos) fin = text.size();

        if (n == line) {
            // El retorno de carro se quita para que no descuadre el cursor de
            // debajo ni ensucie la salida.
            if (fin > inicio && text[fin - 1] == '\r') --fin;
            out.assign(text, inicio, fin - inicio);
            return true;
        }

        if (fin >= text.size()) return false;   // el fichero se acabo antes
        inicio = fin + 1;
    }
}

} // namespace vpp
