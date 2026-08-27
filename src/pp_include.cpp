/**
 * @file pp_include.cpp
 * @brief Implementacion de la busqueda de ficheros de inclusion.
 */

#include "preprocessor/pp_include.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace vpp {

namespace {

/**
 * @brief Comprueba que un fichero existe, sin leerlo.
 *
 * Separar mirar de leer es lo que permite descartar una inclusion sin pagar el
 * fichero entero: una cabecera cuya guarda ya esta definida no puede aportar
 * nada, y para saberlo basta con saber CUAL es.
 *
 * @param p Ruta a comprobar.
 * @return true si existe y es un fichero.
 */
bool file_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

} // namespace

bool read_file(const std::string& path, std::string& out) {
    const std::filesystem::path p(path);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;

    // Se pide el tamano y se lee de una vez.  Recorrerlo con un iterador de
    // streambuf -- que es lo que se hacia -- saca los caracteres DE UNO EN UNO
    // y ademas hace crecer la cadena a base de reservas sucesivas.  Aqui se
    // leen cabeceras de decenas de kilobytes, cientos de veces.
    ifs.seekg(0, std::ios::end);
    const std::streamoff tam = ifs.tellg();
    if (tam > 0) {
        out.resize(static_cast<std::size_t>(tam));
        ifs.seekg(0, std::ios::beg);
        ifs.read(&out[0], tam);
        // Un fichero puede dar menos de lo que dijo -- por ejemplo si algo lo
        // acorta entre las dos llamadas -- asi que la cadena se ajusta a lo que
        // de verdad se leyo en lugar de dejar basura al final.
        out.resize(static_cast<std::size_t>(ifs.gcount()));
    } else {
        out.clear();
    }

    // Un fichero vacio es un fichero que EXISTE.  Distinguirlo importa: una
    // cabecera vacia es legitima, y confundirla con "no encontrado" hace que la
    // busqueda siga y acabe abriendo otra distinta.
    return true;
}

IncludeSearch::IncludeSearch(const std::vector<std::string>& include_paths,
                                 const std::vector<std::string>& import_paths)
    : m_include_paths(include_paths)
    , m_import_paths(import_paths) {}

std::string IncludeSearch::request_key(const std::string& path,
                                        bool               is_system,
                                        const std::string& from_file,
                                        int                start) {
    // De quien incluye solo importa su DIRECTORIO: es lo unico que cambia
    // donde se busca.  Usar el fichero entero partiria en claves distintas
    // peticiones que dan el mismo resultado, que es justo lo que se quiere
    // evitar -- de un mismo directorio salen decenas de cabeceras.
    std::string dir;
    if (!is_system && !from_file.empty()) {
        dir = std::filesystem::path(from_file).parent_path().string();
    }
    // El separador es un caracter de control: no puede aparecer en una ruta y
    // por tanto no puede confundir dos claves en una.
    return path + '\x1f' + (is_system ? '1' : '0') + '\x1f' + dir + '\x1f' +
           std::to_string(start > 0 ? start : 0);
}

ResolvedInclude IncludeSearch::locate(const std::string& path,
                                       bool               is_system,
                                       const std::string& from_file,
                                       int                start) const {
    // Lo ya buscado no se vuelve a buscar: la misma peticion se repite
    // constantemente y cada intento cuesta una consulta al sistema de ficheros
    // por candidato.
    const std::string key = request_key(path, is_system, from_file, start);
    {
        const auto it = m_located.find(key);
        if (it != m_located.end()) return it->second;
    }

    ResolvedInclude r;

    // Forma "..." y busqueda desde el principio: primero al lado del fichero
    // que incluye.  Con `start` mayor que cero venimos de un #include_next, y
    // ese paso ya se dio cuando se encontro el fichero actual.
    if (!is_system && start <= 0 && !from_file.empty()) {
        const std::filesystem::path base =
            std::filesystem::path(from_file).parent_path();
        const std::filesystem::path cand = base / path;
        if (file_exists(cand)) {
            r.found        = true;
            r.path         = cand.string();
            r.search_index = -1;   // no salio de la lista de rutas
        }
    }

    // Despues, las rutas de busqueda, desde donde toque.
    const int n = static_cast<int>(m_include_paths.size());
    for (int i = (start > 0 ? start : 0); !r.found && i < n; ++i) {
        const std::filesystem::path cand =
            std::filesystem::path(m_include_paths[static_cast<std::size_t>(i)])
            / path;
        if (file_exists(cand)) {
            r.found        = true;
            r.path         = cand.string();
            r.search_index = i;
        }
    }

    // Tambien se recuerda que NO aparecio: un `#include` que falla se reintenta
    // igual que uno que acierta, y recorrer la lista entera para volver a no
    // encontrarlo cuesta lo mismo.
    m_located.emplace(key, r);
    return r;
}

ResolvedInclude IncludeSearch::resolve(const std::string& path,
                                        bool               is_system,
                                        const std::string& from_file,
                                        int                start) const {
    ResolvedInclude r = locate(path, is_system, from_file, start);
    if (r.found && !read_file(r.path, r.content)) {
        // Estaba al mirar y ya no esta al abrir: se trata como no encontrado.
        r = ResolvedInclude{};
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
            if (read_file(cand.string(), r.content)) {
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
