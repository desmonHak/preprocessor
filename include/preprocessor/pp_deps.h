/**
 * @file pp_deps.h
 * @brief Emision de la lista de dependencias en formato de make.
 */
#pragma once

#include <string>
#include <vector>

namespace vpp {

/**
 * @brief Como se quiere la lista de dependencias.
 */
struct DepsOptions {
    /**
     * @brief Nombre del objetivo de la regla.
     *
     * Vacio significa deducirlo: el fichero de salida si se dio uno, y si no el
     * del fuente con la extension cambiada, que es lo que hace cc.
     */
    std::string target;

    /**
     * @brief Anadir un objetivo ficticio por cada dependencia.
     *
     * Sirve para que borrar una cabecera no rompa el build: sin esos objetivos,
     * make se para diciendo que no sabe construir un fichero que ya no existe y
     * que ademas ya no hace falta.  Es el `-MP` de cc.
     */
    bool phony_targets = false;
};

/**
 * @brief Escribe la regla de make que declara de que depende un fuente.
 *
 * Es lo que permite que un build incremental sepa que hay que rehacer cuando
 * cambia una cabecera.  Sin esto, quien use vpp solo puede recompilar siempre o
 * equivocarse.
 *
 * El formato es el de cc, para que sirva tal cual a make y a ninja:
 *
 *     salida.o: fuente.c cabecera.h otra.h
 *
 * Las rutas son las RESUELTAS, que son las unicas que le sirven a make; la
 * escrita depende de desde donde se incluyera.
 *
 * @param target_hint Fichero de salida, si se dio; se usa como objetivo cuando
 *                    las opciones no traen uno.
 * @param source      Fuente principal, que tambien es una dependencia.
 * @param included    Ficheros incluidos, en el orden en que aparecieron.
 * @param opts        Ajustes.
 * @return El texto de la regla, terminado en salto de linea.
 */
std::string format_make_deps(const std::string&              target_hint,
                             const std::string&              source,
                             const std::vector<std::string>& included,
                             const DepsOptions&              opts);

} // namespace vpp
