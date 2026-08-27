/**
 * @file pp_facts_cache.cpp
 * @brief Implementacion de la memoria entre ejecuciones.
 */

#include "preprocessor/pp_facts_cache.h"
#include "preprocessor/pp_atomic_write.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace vpp {

namespace {

/**
 * @brief Version del formato.
 *
 * Va en el nombre del directorio y no dentro del fichero: cambiarla deja las
 * memorias viejas donde estan en vez de leerlas mal, y limpiarlas es tirar un
 * directorio.
 */
const char* const kFormatVersion = "v1";

/// Cabecera con la que empieza todo fichero de memoria.
const char* const kHeader = "# vpp facts";

/**
 * @brief Indica si un texto puede escribirse tal cual en un registro.
 *
 * El formato es una linea por registro con un tabulador de separador, asi que
 * un texto que traiga tabulador o salto no se podria volver a leer.  Antes que
 * inventar un escapado que casi nunca haria falta, esos casos no se guardan: se
 * vuelven a preguntar y ya esta.
 *
 * @param s Texto a comprobar.
 * @return true si es seguro escribirlo.
 */
bool is_storable(const std::string& s) {
    return s.find('\t') == std::string::npos &&
           s.find('\n') == std::string::npos &&
           s.find('\r') == std::string::npos;
}

/**
 * @brief Lee los registros de un fichero de memoria.
 * @param path    Ruta del fichero.
 * @param entries Mapa donde depositar lo leido; no se borra lo que ya tenga.
 */
void read_records(const std::string& path,
                  std::unordered_map<std::string, std::string>& entries) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return;   // todavia no hay memoria de este compilador

    std::string line;
    if (!std::getline(ifs, line)) return;
    // Sin la cabecera no es un fichero nuestro: se ignora entero en vez de
    // intentar interpretarlo.
    if (line.rfind(kHeader, 0) != 0) return;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;   // registro roto: se salta
        entries.emplace(line.substr(0, tab), line.substr(tab + 1));
    }
}

} // namespace

FactsCache::FactsCache(std::string dir, std::string fingerprint)
    : m_dir(std::move(dir))
    , m_fingerprint(std::move(fingerprint))
    , m_enabled(!m_dir.empty() && !m_fingerprint.empty())
    , m_dirty(false)
{
    if (m_enabled) load();
}

FactsCache::~FactsCache() {
    // Se escribe al final y no en cada respuesta: durante una ejecucion se
    // aprenden decenas, y volcar el fichero entero cada vez costaria mas de lo
    // que la memoria ahorra.
    flush();
}

std::string FactsCache::file_path(const std::string& dir,
                                  const std::string& fingerprint) {
    return (std::filesystem::path(dir) / kFormatVersion /
            (fingerprint + ".facts")).string();
}

void FactsCache::load() {
    read_records(file_path(m_dir, m_fingerprint), m_entries);
}

bool FactsCache::lookup(const std::string& key, std::string& value) const {
    if (!m_enabled) return false;
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) return false;
    value = it->second;
    return true;
}

void FactsCache::store(const std::string& key, std::string value) {
    if (!m_enabled) return;
    if (!is_storable(key) || !is_storable(value)) return;

    m_entries.insert_or_assign(key, std::move(value));
    m_dirty = true;
}

bool FactsCache::flush() {
    if (!m_enabled || !m_dirty) return false;

    const std::string path = file_path(m_dir, m_fingerprint);

    // Se relee justo antes de escribir para no tirar lo que haya aprendido otro
    // proceso mientras tanto.  No es un cerrojo -- dos escrituras a la vez
    // siguen pudiendo pisarse -- pero eso solo cuesta volver a preguntar,
    // porque lo que se guarda son hechos y nadie puede escribir algo distinto
    // de lo que escribiria el otro.
    read_records(path, m_entries);

    std::ostringstream os;
    os << kHeader << ' ' << kFormatVersion << '\n';
    for (const auto& entry : m_entries) {
        os << entry.first << '\t' << entry.second << '\n';
    }

    if (!write_file_atomically(path, os.str())) return false;

    m_dirty = false;
    return true;
}

} // namespace vpp
