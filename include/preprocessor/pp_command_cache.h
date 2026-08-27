/**
 * @file pp_command_cache.h
 * @brief Memoria entre ejecuciones de la salida completa de un comando.
 */
#pragma once

#include <string>

namespace vpp {

/**
 * @brief Recuerda lo que imprimio un comando, por compilador.
 *
 * Es el hermano de FactsCache para lo que no cabe alli.  Aquel guarda muchas
 * respuestas cortas; esto guarda UN texto grande y de una pieza -- el volcado
 * de `gcc -dM -E -` son cientos de lineas -- que no se puede meter en un
 * formato de una linea por registro sin inventar un escapado.
 *
 * Vale la pena por separado porque el volcado de macros predefinidas se pide en
 * CADA invocacion de vpp, no unas cuantas veces: en una compilacion de N
 * ficheros es un proceso lanzado N veces para obtener exactamente lo mismo.
 *
 * La clave es la identidad del compilador (ver CompilerId), que ya incluye sus
 * flags, asi que dos ordenes distintas nunca comparten fichero.
 */
class CommandOutputCache {
public:
    /**
     * @brief Constructor.
     *
     * @param dir         Directorio donde vive la memoria.  Vacio la desactiva.
     * @param fingerprint Huella del compilador.  Vacia la desactiva.
     */
    CommandOutputCache(std::string dir, std::string fingerprint);

    /** @brief Indica si la memoria esta operativa. @return true si lo esta. */
    bool enabled() const noexcept { return m_enabled; }

    /**
     * @brief Recupera la salida recordada.
     *
     * @param output Recibe el texto si estaba.
     * @return true si estaba.
     */
    bool load(std::string& output) const;

    /**
     * @brief Guarda la salida de un comando.
     *
     * No debe llamarse con la salida de una invocacion que fallo: quedaria
     * escrito un problema pasajero como si fuera lo que el compilador produce,
     * y sobreviviria a su causa.  Una salida vacia tampoco se guarda, por lo
     * mismo.
     *
     * @param output Texto a recordar.
     * @return true si se escribio.
     */
    bool store(const std::string& output) const;

    /**
     * @brief Ruta del fichero de un compilador dentro de un directorio.
     *
     * Publica para que las pruebas puedan mirar lo que se escribio sin
     * replicar como se nombra.
     *
     * @param dir         Directorio de la memoria.
     * @param fingerprint Huella del compilador.
     * @return Ruta del fichero.
     */
    static std::string file_path(const std::string& dir,
                                 const std::string& fingerprint);

private:
    std::string m_dir;          ///< Directorio de la memoria
    std::string m_fingerprint;  ///< Huella del compilador
    bool        m_enabled;      ///< false si falta el directorio o la huella
};

} // namespace vpp
