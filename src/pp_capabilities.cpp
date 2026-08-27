/**
 * @file pp_capabilities.cpp
 * @brief Implementacion del consultor de capacidades del compilador.
 */

#include "preprocessor/pp_capabilities.h"
#include "preprocessor/pp_compiler_id.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
    #include <process.h>
    #define VPP_CAP_POPEN  _popen
    #define VPP_CAP_PCLOSE _pclose
    #define VPP_CAP_GETPID _getpid
#else
    #include <unistd.h>
    #define VPP_CAP_POPEN  popen
    #define VPP_CAP_PCLOSE pclose
    #define VPP_CAP_GETPID getpid
#endif

namespace vpp {

namespace {
/// Marca que se busca en la salida del compilador para leer su respuesta.
const char* const kMarker = "__VPP_CAP__";

/// Destino al que se tira la salida de error del compilador.
#ifdef _WIN32
const char* const kNullOutput = "NUL";
#else
const char* const kNullOutput = "/dev/null";
#endif
}

const char* const kOpDefined = "#defined";

bool is_capability_operator(const std::string& name) noexcept {
    return name.compare(0, 6, "__has_") == 0 && name.size() > 6;
}

CapabilityOracle::CapabilityOracle(std::string command)
    : m_command(std::move(command)) {}

void CapabilityOracle::set_command(std::string command) {
    // Cambiar de compilador invalida lo aprendido: la misma pregunta puede
    // tener otra respuesta.
    if (command != m_command) {
        m_cache.clear();
        m_command = std::move(command);
        rebuild_store();
    }
}

void CapabilityOracle::set_cache_dir(std::string dir) {
    if (dir == m_cache_dir) return;
    m_cache_dir = std::move(dir);
    rebuild_store();
}

void CapabilityOracle::rebuild_store() {
    // El destructor del anterior escribe lo que hubiera aprendido.
    m_store.reset();

    if (m_cache_dir.empty() || m_command.empty()) return;

    // Si no se puede identificar al compilador, no se recuerda nada: sin saber
    // a quien se pregunta no hay forma de notar que ha cambiado, y una memoria
    // que no detecta una actualizacion es peor que no tener memoria.
    const std::string fingerprint = CompilerId::fingerprint(m_command);
    if (fingerprint.empty()) return;

    m_store.reset(new FactsCache(m_cache_dir, fingerprint));
}

std::string CapabilityOracle::key(const std::string& op,
                                  const std::string& arg) const {
    // El separador es un caracter de control para que no pueda aparecer dentro
    // de una orden, de un operador ni de un argumento y confundir dos claves
    // distintas en una sola.
    return m_command + '\x1f' + op + '\x1f' + arg;
}

std::string CapabilityOracle::disk_key(const std::string& op,
                                       const std::string& arg) {
    return op + ' ' + arg;
}

bool CapabilityOracle::ask(const std::string& op,
                           const std::string& arg,
                           int64_t& value) const {
    // Se le da al compilador un fuente minimo cuya unica salida es 1 o 0.  Se
    // usa un fichero y no la entrada estandar porque la orden la escribe el
    // usuario y no se puede dar por hecho que acepte leer de stdin.
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) return false;

    // El nombre lleva el identificador del proceso.  Con un nombre fijo, una
    // compilacion en paralelo -- que lanza muchos vpp a la vez -- hacia que
    // unos sobrescribieran el fichero de consulta de otros y se contestaran
    // preguntas ajenas.
    const std::filesystem::path tmp = dir /
        ("vpp_cap_query_" +
         std::to_string(static_cast<long long>(VPP_CAP_GETPID())) + ".c");

    {
        std::ofstream ofs(tmp, std::ios::binary);
        if (!ofs) return false;
        // El pseudo-operador kOpDefined no pregunta cuanto vale algo sino si
        // existe siquiera.  Es una pregunta distinta y hace falta: en modo C,
        // clang no tiene `__has_cpp_attribute`, y darlo por bueno lleva a
        // consultarlo y a que el compilador falle por una pregunta que nunca
        // debio hacerse.
        const std::string cond = (op == kOpDefined)
                                   ? ("defined(" + arg + ")")
                                   : (op + "(" + arg + ")");
        ofs << "#if " << cond << "\n"
            << kMarker << " 1\n"
            << "#else\n"
            << kMarker << " 0\n"
            << "#endif\n";
    }

    bool answered = false;
    // La salida de error del compilador se descarta.  Una consulta que el
    // compilador rechace es una RESPUESTA -- "eso no lo tengo" -- y dejar que
    // sus quejas salgan por la salida de error de vpp las convertiria en
    // diagnosticos del fuente del usuario, que no lo son.
    const std::string cmd = m_command + " \"" + tmp.string() + "\" 2>" + kNullOutput;
    if (FILE* pipe = VPP_CAP_POPEN(cmd.c_str(), "r")) {
        char buf[512];
        std::string out;
        while (std::fgets(buf, sizeof(buf), pipe)) out += buf;
        VPP_CAP_PCLOSE(pipe);

        // Se busca la marca y el primer 0 o 1 que la siga.  Leer asi, y no la
        // salida entera, hace que sobrevivan los avisos que el compilador
        // pueda escribir por su cuenta.
        const std::size_t mark = out.find(kMarker);
        if (mark != std::string::npos) {
            const std::size_t digit =
                out.find_first_of("01", mark + std::char_traits<char>::length(kMarker));
            if (digit != std::string::npos) {
                value    = (out[digit] == '1') ? 1 : 0;
                answered = true;
            }
        }
    }

    std::filesystem::remove(tmp, ec);
    return answered;
}

int64_t CapabilityOracle::query(const std::string& op,
                                const std::string& arg) {
    if (!available()) return 0;

    // Primer nivel: lo preguntado en esta misma ejecucion.
    const std::string k = key(op, arg);
    const auto it = m_cache.find(k);
    if (it != m_cache.end()) return it->second;

    // Segundo nivel: lo aprendido en ejecuciones anteriores.
    const std::string dk = disk_key(op, arg);
    if (m_store) {
        std::string remembered;
        if (m_store->lookup(dk, remembered)) {
            const int64_t value = (remembered == "1") ? 1 : 0;
            m_cache.emplace(k, value);
            return value;
        }
    }

    // No consta: hay que preguntarle al compilador.
    int64_t value = 0;
    if (!ask(op, arg, value)) {
        // No hubo respuesta.  No se recuerda NADA, ni siquiera en memoria: lo
        // que ha fallado es la consulta, no el compilador, y darlo por un 0
        // dejaria escrito un problema pasajero como si fuera una propiedad
        // suya.  El operador vale 0 para esta vez y se reintentara.
        return 0;
    }

    m_cache.emplace(k, value);
    if (m_store) m_store->store(dk, value ? "1" : "0");
    return value;
}

bool CapabilityOracle::is_known(const std::string& name) {
    return query(kOpDefined, name) != 0;
}

} // namespace vpp
