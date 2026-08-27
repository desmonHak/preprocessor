/**
 * @file pp_system.cpp
 * @brief Implementacion de las consultas al sistema.
 */

#include "preprocessor/pp_system.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace vpp {

namespace {

/// Separador de listas de rutas: ';' en Windows, ':' en el resto.
#ifdef _WIN32
const char kPathSeparator = ';';
#else
const char kPathSeparator = ':';
#endif

/**
 * @brief Comprueba que una ruta existe y es un fichero regular.
 * @param p Ruta a comprobar.
 * @return true si es un fichero regular.
 */
bool file_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

/**
 * @brief Devuelve la ruta canonica, o la absoluta si no se puede canonizar.
 * @param p Ruta de partida.
 * @return La ruta ya resuelta.
 */
std::string real_path(const std::filesystem::path& p) {
    std::error_code ec;
    const auto real = std::filesystem::canonical(p, ec);
    if (!ec) return real.string();
    const auto abs = std::filesystem::absolute(p, ec);
    return ec ? p.string() : abs.string();
}

/**
 * @brief Extensiones a probar al buscar un ejecutable.
 * @return La lista, empezando por la cadena vacia (el nombre tal cual).
 */
std::vector<std::string> executable_suffixes() {
    std::vector<std::string> out{ "" };
#ifdef _WIN32
    for (const auto& e : env_path_list("PATHEXT")) out.push_back(e);
#endif
    return out;
}

} // namespace

std::string env_value(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::vector<std::string> env_path_list(const char* name) {
    const std::string raw = env_value(name);

    std::vector<std::string> out;
    std::string current;
    for (const char c : raw) {
        if (c == kPathSeparator) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::string find_executable(const std::string& name) {
    if (name.empty()) return {};

    const bool has_directory = name.find('/')  != std::string::npos ||
                               name.find('\\') != std::string::npos;

    // Con separador de directorio la orden ya dice donde esta; no se busca.
    if (has_directory) {
        for (const auto& suffix : executable_suffixes()) {
            const std::filesystem::path candidate(name + suffix);
            if (file_exists(candidate)) return real_path(candidate);
        }
        return {};
    }

    for (const auto& dir : env_path_list("PATH")) {
        for (const auto& suffix : executable_suffixes()) {
            const std::filesystem::path candidate =
                std::filesystem::path(dir) / (name + suffix);
            if (file_exists(candidate)) return real_path(candidate);
        }
    }

    return {};
}

std::string user_cache_dir() {
#ifdef _WIN32
    std::string base = env_value("LOCALAPPDATA");
    if (base.empty()) base = env_value("APPDATA");
    if (base.empty()) base = env_value("TEMP");
    if (base.empty()) return {};
    return (std::filesystem::path(base) / "vpp").string();
#else
    std::string base = env_value("XDG_CACHE_HOME");
    if (base.empty()) {
        const std::string home = env_value("HOME");
        if (home.empty()) return {};
        base = (std::filesystem::path(home) / ".cache").string();
    }
    return (std::filesystem::path(base) / "vpp").string();
#endif
}

} // namespace vpp
