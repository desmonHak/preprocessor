/**
 * @file pp_atomic_write.cpp
 * @brief Implementacion de la escritura atomica de ficheros.
 */

#include "preprocessor/pp_atomic_write.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
    #include <process.h>
    #define VPP_GETPID _getpid
#else
    #include <unistd.h>
    #define VPP_GETPID getpid
#endif

namespace vpp {

bool write_file_atomically(const std::string& path,
                           const std::string& content) {
    std::error_code ec;
    const std::filesystem::path target(path);

    const std::filesystem::path dir = target.parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
        if (ec) return false;
    }

    // El temporal lleva el identificador del proceso: sin el, dos vpp a la vez
    // escribirian en el mismo fichero intermedio y se corromperia justo lo que
    // esto viene a evitar.  Y va en el MISMO directorio que el destino, porque
    // renombrar solo es atomico dentro de un mismo sistema de ficheros.
    const std::filesystem::path temporary =
        dir / (target.filename().string() + "." +
               std::to_string(static_cast<long long>(VPP_GETPID())) + ".tmp");

    {
        std::ofstream ofs(temporary, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs.write(content.data(),
                  static_cast<std::streamsize>(content.size()));
        ofs.close();
        if (!ofs) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        // Algunas implementaciones no reemplazan un destino que ya existe.
        // Deja una ventana en la que el fichero no esta, pero solo se llega
        // aqui cuando el renombrado directo no es posible.
        std::filesystem::remove(target, ec);
        std::filesystem::rename(temporary, target, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    return true;
}

} // namespace vpp
