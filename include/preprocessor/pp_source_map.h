/**
 * @file pp_source_map.h
 * @brief Guarda el texto de cada fuente para poder citarlo en un diagnostico.
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vpp {

/**
 * @brief Los fuentes que se han leido, por nombre de fichero.
 *
 * Un mensaje que solo da `fichero:linea:columna` obliga a quien lo lee a abrir
 * el fichero y contar.  Citar la linea y senalar la columna es la diferencia
 * entre un diagnostico que se arregla y uno que se investiga -- y no es una
 * suposicion: en esta sesion, tres fallos que solo aparecian en el CI de macOS
 * fueron diagnosticables unicamente porque la prueba se traia consigo las
 * lineas culpables.
 *
 * Hace falta un sitio donde vivan los textos porque el que lexa un fichero lo
 * suelta al terminar, y para entonces el diagnostico todavia no se ha
 * formateado.  Ademas no todos los fuentes estan en el disco: los hay que
 * llegan por la API, por la entrada estandar o de un `#exec`, y a esos no se
 * puede volver.
 *
 * Guardar el texto entero cuesta memoria proporcional a la entrada.  Es lo que
 * hace gcc -- que por eso usa el doble que vpp en la misma carga -- y es ademas
 * el mismo almacen que necesitaria que un token apuntase a su fuente en lugar
 * de copiarlo.
 */
class SourceMap {
public:
    /**
     * @brief Guarda el texto de un fichero.
     *
     * Si ya estaba, no se toca: el primero que se registro es el que se lexo, y
     * volver a guardarlo solo copiaria lo mismo.
     *
     * @param file Nombre con el que se conoce al fichero.
     * @param text Su contenido.
     */
    void add(const std::string& file, const std::string& text);

    /**
     * @brief Devuelve una linea concreta de un fichero.
     *
     * @param file  Nombre del fichero.
     * @param line  Numero de linea, base 1.
     * @param out   Recibe la linea, sin el salto final.
     * @return true si se pudo obtener.
     */
    bool line(const std::string& file, uint32_t line, std::string& out) const;

    /** @brief Cuantos fuentes hay guardados. @return Numero de ficheros. */
    std::size_t size() const noexcept { return m_sources.size(); }

    /** @brief Suelta todo lo guardado. */
    void clear() noexcept { m_sources.clear(); }

private:
    std::unordered_map<std::string, std::string> m_sources; ///< Texto por fichero
};

} // namespace vpp
