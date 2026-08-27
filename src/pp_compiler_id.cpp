/**
 * @file pp_compiler_id.cpp
 * @brief Implementacion de la identificacion del compilador.
 */

#include "preprocessor/pp_compiler_id.h"
#include "preprocessor/pp_system.h"

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace vpp {

namespace {

/**
 * @brief Anade un campo al acumulador de una huella FNV-1a de 64 bits.
 * @param hash  Acumulador, que se modifica.
 * @param field Campo a incorporar.
 */
void add_field(uint64_t& hash, const std::string& field) {
    for (const unsigned char c : field) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    // La marca de fin impide que dos campos distintos se confundan al pegarse:
    // ("ab", "c") y ("a", "bc") tienen que dar huellas distintas.
    hash ^= 0xFFu;
    hash *= 1099511628211ULL;
}

} // namespace

std::string CompilerId::executable_of(const std::string& command) {
    std::size_t start = 0;
    while (start < command.size() &&
           (command[start] == ' ' || command[start] == '\t')) ++start;
    if (start >= command.size()) return {};

    // Entrecomillado: hace falta de verdad, la ruta de MSVC lleva espacios.
    if (command[start] == '"') {
        const std::size_t close = command.find('"', start + 1);
        if (close == std::string::npos) return {};
        return command.substr(start + 1, close - start - 1);
    }

    std::size_t end = start;
    while (end < command.size() &&
           command[end] != ' ' && command[end] != '\t') ++end;
    return command.substr(start, end - start);
}

std::string CompilerId::fingerprint(const std::string& command) {
    const std::string path = find_executable(executable_of(command));
    if (path.empty()) return {};

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {};
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) return {};

    // Los tres campos del binario contestan a "que compilador es"; la orden
    // entera contesta a "como se le habla", y eso cambia la respuesta tanto
    // como el binario, asi que forma parte de la identidad igual que el.
    uint64_t hash = 14695981039346656037ULL;
    add_field(hash, path);
    add_field(hash, std::to_string(static_cast<unsigned long long>(size)));
    add_field(hash, std::to_string(static_cast<long long>(
        modified.time_since_epoch().count())));
    add_field(hash, command);

    std::ostringstream os;
    os << std::hex << hash;
    return os.str();
}

} // namespace vpp
