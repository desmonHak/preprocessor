/**
 * @file pp_command_cache.cpp
 * @brief Implementacion de la memoria de salidas de comando.
 */

#include "preprocessor/pp_command_cache.h"
#include "preprocessor/pp_atomic_write.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace vpp {

namespace {

/**
 * @brief Version del formato.
 *
 * Va en el nombre del directorio para que un cambio deje lo viejo donde esta
 * en vez de leerlo mal.  Es la misma que usa FactsCache a proposito: las dos
 * memorias hablan del mismo compilador y se limpian juntas.
 */
const char* const kFormatVersion = "v1";

} // namespace

CommandOutputCache::CommandOutputCache(std::string dir,
                                       std::string fingerprint)
    : m_dir(std::move(dir))
    , m_fingerprint(std::move(fingerprint))
    , m_enabled(!m_dir.empty() && !m_fingerprint.empty())
{}

std::string CommandOutputCache::file_path(const std::string& dir,
                                          const std::string& fingerprint) {
    return (std::filesystem::path(dir) / kFormatVersion /
            (fingerprint + ".output")).string();
}

bool CommandOutputCache::load(std::string& output) const {
    if (!m_enabled) return false;

    std::ifstream ifs(file_path(m_dir, m_fingerprint), std::ios::binary);
    if (!ifs) return false;

    output.assign((std::istreambuf_iterator<char>(ifs)),
                   std::istreambuf_iterator<char>());
    return !output.empty();
}

bool CommandOutputCache::store(const std::string& output) const {
    if (!m_enabled || output.empty()) return false;
    return write_file_atomically(file_path(m_dir, m_fingerprint), output);
}

} // namespace vpp
