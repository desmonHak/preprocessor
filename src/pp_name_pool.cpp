/**
 * @file pp_name_pool.cpp
 * @brief Implementacion del pozo de nombres de fichero.
 */

#include "preprocessor/pp_name_pool.h"

#include <mutex>
#include <unordered_set>

namespace vpp {

namespace {

/**
 * @brief El pozo y su cerrojo.
 *
 * Se guardan en un contenedor por NODOS a proposito: hace falta que la
 * direccion de cada nombre siga siendo valida aunque despues entren mas, y eso
 * un contenedor contiguo no lo garantiza.  `unordered_set` no mueve sus
 * elementos al crecer, solo reordena los cubos.
 *
 * Viven dentro de una funcion para que se construyan la primera vez que se usan
 * y no dependan del orden de inicializacion de los estaticos.
 *
 * @return Referencia al pozo.
 */
std::unordered_set<std::string>& pool() {
    static std::unordered_set<std::string> instance;
    return instance;
}

/** @brief Cerrojo del pozo. @return Referencia al cerrojo. */
std::mutex& pool_mutex() {
    static std::mutex instance;
    return instance;
}

} // namespace

const std::string* intern_file_name(const std::string& name) {
    if (name.empty()) return empty_file_name();

    // Se toma el cerrojo sin miramientos porque aqui se entra una vez por
    // fichero, no una vez por token: a esa frecuencia el coste no se mide.
    const std::lock_guard<std::mutex> lock(pool_mutex());
    return &*pool().insert(name).first;
}

const std::string* empty_file_name() noexcept {
    static const std::string instance;
    return &instance;
}

} // namespace vpp
