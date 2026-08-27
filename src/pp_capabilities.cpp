/**
 * @file pp_capabilities.cpp
 * @brief Implementacion del consultor de capacidades del compilador.
 */

#include "preprocessor/pp_capabilities.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
    #define VPP_CAP_POPEN  _popen
    #define VPP_CAP_PCLOSE _pclose
#else
    #define VPP_CAP_POPEN  popen
    #define VPP_CAP_PCLOSE pclose
#endif

namespace vpp {

namespace {
/// Marca que se busca en la salida del compilador para leer su respuesta.
const char* const kMarca = "__VPP_CAP__";
}

CapabilityOracle::CapabilityOracle(std::string command)
    : m_command(std::move(command)) {}

void CapabilityOracle::set_command(std::string command) {
    // Cambiar de compilador invalida lo aprendido: la misma pregunta puede
    // tener otra respuesta.
    if (command != m_command) {
        m_cache.clear();
    }
    m_command = std::move(command);
}

std::string CapabilityOracle::key(const std::string& op,
                                  const std::string& arg) const {
    // El separador es un caracter de control para que no pueda aparecer dentro
    // de una orden, de un operador ni de un argumento y confundir dos claves
    // distintas en una sola.
    return m_command + '\x1f' + op + '\x1f' + arg;
}

int64_t CapabilityOracle::ask(const std::string& op,
                              const std::string& arg) const {
    // Se le da al compilador un fuente minimo cuya unica salida es 1 o 0.  Se
    // usa un fichero y no la entrada estandar porque la orden la escribe el
    // usuario y no se puede dar por hecho que acepte leer de stdin.
    std::error_code ec;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path(ec) / "vpp_cap_query.c";
    if (ec) return 0;

    {
        std::ofstream ofs(tmp, std::ios::binary);
        if (!ofs) return 0;
        ofs << "#if " << op << "(" << arg << ")\n"
            << kMarca << " 1\n"
            << "#else\n"
            << kMarca << " 0\n"
            << "#endif\n";
    }

    int64_t valor = 0;
    const std::string cmd = m_command + " \"" + tmp.string() + "\"";
    if (FILE* pipe = VPP_CAP_POPEN(cmd.c_str(), "r")) {
        char buf[512];
        std::string out;
        while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
        VPP_CAP_PCLOSE(pipe);

        // Se busca la marca y el primer 0 o 1 que la siga.  Leer asi, y no la
        // salida entera, hace que sobrevivan los avisos que el compilador
        // pueda escribir por su cuenta.
        const std::size_t marca = out.find(kMarca);
        if (marca != std::string::npos) {
            const std::size_t d = out.find_first_of("01", marca + 11);
            if (d != std::string::npos) valor = (out[d] == '1') ? 1 : 0;
        }
    }

    std::filesystem::remove(tmp, ec);
    return valor;
}

int64_t CapabilityOracle::query(const std::string& op,
                                const std::string& arg) {
    if (!available()) return 0;

    const std::string k = key(op, arg);
    const auto it = m_cache.find(k);
    if (it != m_cache.end()) return it->second;

    const int64_t valor = ask(op, arg);
    m_cache.emplace(k, valor);
    return valor;
}

} // namespace vpp
