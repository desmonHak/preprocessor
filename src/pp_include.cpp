/**
 * @file pp_include.cpp
 * @brief Implementacion de la busqueda de ficheros de inclusion.
 */

#include "preprocessor/pp_include.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace vpp {

namespace {

/**
 * @brief Lee un fichero entero, o nada si no se puede abrir.
 * @param p Ruta del fichero.
 * @param out Contenido leido.
 * @return true si se pudo abrir.
 */
bool leer(const std::filesystem::path& p, std::string& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;
    out.assign((std::istreambuf_iterator<char>(ifs)),
                std::istreambuf_iterator<char>());
    // Un fichero vacio es un fichero que EXISTE.  Distinguirlo importa: una
    // cabecera vacia es legitima, y confundirla con "no encontrado" hace que la
    // busqueda siga y acabe abriendo otra distinta.
    return true;
}

} // namespace

IncludeSearch::IncludeSearch(const std::vector<std::string>& include_paths,
                                 const std::vector<std::string>& import_paths)
    : m_include_paths(include_paths)
    , m_import_paths(import_paths) {}

ResolvedInclude IncludeSearch::resolve(const std::string& path,
                                         bool               is_system,
                                         const std::string& from_file,
                                         int                start) const {
    ResolvedInclude r;

    // Forma "..." y busqueda desde el principio: primero al lado del fichero
    // que incluye.  Con `start` mayor que cero venimos de un #include_next, y
    // ese paso ya se dio cuando se encontro el fichero actual.
    if (!is_system && start <= 0 && !from_file.empty()) {
        const std::filesystem::path base =
            std::filesystem::path(from_file).parent_path();
        const std::filesystem::path cand = base / path;
        if (leer(cand, r.content)) {
            r.found        = true;
            r.path         = cand.string();
            r.search_index = -1;   // no salio de la lista de rutas
            return r;
        }
    }

    // Despues, las rutas de busqueda, desde donde toque.
    const int n = static_cast<int>(m_include_paths.size());
    for (int i = (start > 0 ? start : 0); i < n; ++i) {
        const std::filesystem::path cand =
            std::filesystem::path(m_include_paths[static_cast<std::size_t>(i)])
            / path;
        if (leer(cand, r.content)) {
            r.found        = true;
            r.path         = cand.string();
            r.search_index = i;
            return r;
        }
    }

    return r;
}

ResolvedInclude IncludeSearch::resolve_import(const std::string& path) const {
    ResolvedInclude r;

    // Extensiones del dialecto, para que `#import <vesta/io>` encuentre io.vph.
    // La cadena vacia va la ultima: si el usuario escribio la extension, ya
    // esta en la ruta.
    static const char* const kExts[] = { ".vph", ".vel", "" };

    const int n = static_cast<int>(m_import_paths.size());
    for (int i = 0; i < n; ++i) {
        const std::filesystem::path base =
            std::filesystem::path(m_import_paths[static_cast<std::size_t>(i)])
            / path;
        for (const char* ext : kExts) {
            std::filesystem::path cand = base;
            if (ext[0] != '\0') cand += ext;
            if (leer(cand, r.content)) {
                r.found        = true;
                r.path         = cand.string();
                r.search_index = i;
                return r;
            }
        }
    }

    return r;
}

} // namespace vpp
