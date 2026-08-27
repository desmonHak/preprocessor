/**
 * @file pp_include.h
 * @brief Busqueda de ficheros para #include, #include_next e #import.
 */
#pragma once

#include <string>
#include <vector>

namespace vpp {

/**
 * @brief Lo que se sabe de una inclusion despues de buscarla.
 *
 * Devuelve DONDE aparecio, no solo el contenido, y eso no es un adorno: sin
 * ese dato no se puede resolver un `#include "vecino.h"` escrito dentro de una
 * cabecera que se encontro por una ruta de busqueda -- el vecino esta al lado
 * de ELLA, no del fichero de partida -- ni se puede atender un
 * `#include_next`, que por definicion tiene que reanudar la busqueda justo
 * despues del directorio donde aparecio el fichero actual.
 */
struct ResolvedInclude {
    bool        found = false;   ///< true si se encontro
    std::string content;         ///< Contenido del fichero
    std::string path;            ///< Ruta con la que se abrio

    /**
     * @brief Indice del directorio de busqueda en el que aparecio.
     *
     * -1 cuando se encontro relativo al fichero que lo incluye, es decir sin
     * consultar la lista de rutas.
     */
    int search_index = -1;
};

/**
 * @brief Encuentra los ficheros que piden las directivas de inclusion.
 *
 * Es un componente propio y no un metodo mas del preprocesador porque tiene su
 * propia informacion -- las listas de rutas de busqueda -- y unas reglas de
 * precedencia que se pueden razonar y probar sin nada del pipeline alrededor.
 */
class IncludeSearch {
public:
    /** @brief Constructor por defecto: sin rutas de busqueda. */
    IncludeSearch() = default;

    /**
     * @brief Constructor.
     * @param include_paths Rutas para `#include <...>`, en orden.
     * @param import_paths  Rutas para `#import <...>`, en orden.
     */
    IncludeSearch(const std::vector<std::string>& include_paths,
                  const std::vector<std::string>& import_paths);

    /**
     * @brief Busca el fichero de un `#include`.
     *
     * @param path      Ruta tal y como se escribio en la directiva.
     * @param is_system true para la forma `<...>`, false para `"..."`.
     * @param from_file Ruta RESUELTA del fichero que incluye.  Es la de verdad
     *                  y no la que se escribio, porque el vecino de una
     *                  cabecera esta al lado de donde ESA cabecera aparecio.
     * @param start     Primer indice de la lista de rutas por el que empezar.
     *                  Cero es la busqueda normal; un valor mayor sirve a
     *                  `#include_next`, que ha de saltarse los directorios ya
     *                  recorridos.
     * @return El resultado; `found` a false si no aparecio.
     */
    ResolvedInclude resolve(const std::string& path,
                            bool               is_system,
                            const std::string& from_file,
                            int                start = 0) const;

    /**
     * @brief Localiza el fichero de un `#include` SIN leerlo.
     *
     * Misma busqueda que resolve(), pero devolviendo solo donde esta.  Existe
     * porque saber CUAL es el fichero basta para decidir que no hace falta: una
     * cabecera cuya guarda de inclusion ya esta definida no puede aportar nada,
     * y asi se descarta sin pagar la lectura.
     *
     * Ademas es lo unico que da la ruta RESUELTA a tiempo de usarla como
     * identidad.  La ruta escrita no sirve: `#include "_types.h"` desde dos
     * directorios distintos nombra ficheros distintos.
     *
     * @param path      Ruta tal y como se escribio en la directiva.
     * @param is_system true para la forma `<...>`, false para `"..."`.
     * @param from_file Ruta RESUELTA del fichero que incluye.
     * @param start     Primer indice de la lista de rutas por el que empezar.
     * @return El resultado con `content` VACIO; `found` a false si no aparecio.
     */
    ResolvedInclude locate(const std::string& path,
                           bool               is_system,
                           const std::string& from_file,
                           int                start = 0) const;

    /**
     * @brief Busca el modulo de un `#import`.
     *
     * Tiene reglas propias: mira solo en las rutas de importacion y prueba las
     * extensiones del dialecto, de modo que `#import <vesta/io>` encuentre un
     * `io.vph`.
     *
     * @param path Ruta tal y como se escribio.
     * @return El resultado; `found` a false si no aparecio.
     */
    ResolvedInclude resolve_import(const std::string& path) const;

    /** @brief Cuantas rutas de inclusion hay. @return Numero de rutas. */
    std::size_t include_path_count() const noexcept {
        return m_include_paths.size();
    }

private:
    std::vector<std::string> m_include_paths;  ///< Rutas de `#include <...>`
    std::vector<std::string> m_import_paths;   ///< Rutas de `#import <...>`
};

} // namespace vpp
