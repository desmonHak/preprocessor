/**
 * @file pp_capabilities.h
 * @brief Consulta las capacidades del compilador de destino.
 */
#pragma once

#include "pp_facts_cache.h"

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

namespace vpp {

/**
 * @brief Dice si un nombre es un operador de prueba de caracteristicas.
 *
 * La lista no se enumera: vale cualquier `__has_algo`.  Los compiladores no
 * comparten el mismo juego -- `__has_feature` y `__has_warning` son de clang,
 * `__has_c_attribute` es de C23 -- y quien manda es el compilador de destino,
 * que es a quien se le va a preguntar.  Enumerarlos aqui solo serviria para
 * quedarse corto cada vez que aparezca uno nuevo.
 *
 * @param name Identificador a examinar.
 * @return true si tiene la forma de uno de estos operadores.
 */
bool is_capability_operator(const std::string& name) noexcept;

/**
 * @brief Pseudo-operador con el que se pregunta si un nombre EXISTE.
 *
 * No es un operador de verdad: es la clave con la que se guarda esa pregunta
 * en la memoria del oraculo, para que conviva con las de valor sin mezclarse.
 */
extern const char* const kOpDefined;

/**
 * @brief Responde a los operadores `__has_builtin`, `__has_attribute` y demas.
 *
 * Esos operadores NO preguntan por una macro: preguntan por lo que sabe hacer
 * un compilador.  vpp no es un compilador, asi que la unica respuesta honesta
 * es preguntarselo al de destino y recordar lo que conteste.
 *
 * Vive aparte del preprocesador a proposito.  Es una responsabilidad
 * independiente -- hablar con un proceso externo y recordar sus respuestas --
 * con su propio estado y su propio ciclo de vida, y meterla dentro del
 * preprocesador solo lo habria hecho mas grande sin que ganara nada.  Ademas
 * asi se puede probar sola.  La memoria entre ejecuciones la pone FactsCache,
 * que es otro componente aparte: este sabe QUE preguntar y a quien, aquel sabe
 * guardar respuestas, y ninguno de los dos necesita saber del otro mas que eso.
 */
class CapabilityOracle {
public:
    /**
     * @brief Constructor.
     * @param command Orden con la que invocar al compilador de destino, a la
     *                que se le anadira la ruta de un fichero de consulta;
     *                p.ej. "gcc -E -P -x c".  Vacia significa que no hay a
     *                quien preguntar.
     */
    explicit CapabilityOracle(std::string command = {});

    /**
     * @brief Cambia el compilador al que se pregunta.
     *
     * Vacia el recuerdo: las respuestas de un compilador no valen para otro.
     *
     * @param command Nueva orden de invocacion.
     */
    void set_command(std::string command);

    /**
     * @brief Fija donde se recuerdan las respuestas entre ejecuciones.
     *
     * Sin esto la memoria dura lo que el proceso, y en una compilacion cada
     * fichero vuelve a pagar las mismas preguntas enteras.
     *
     * @param dir Directorio de la memoria.  Vacio la deja solo en memoria.
     */
    void set_cache_dir(std::string dir);

    /** @brief Orden en uso. @return La orden, vacia si no hay ninguna. */
    const std::string& command() const noexcept { return m_command; }

    /** @brief Indica si hay a quien preguntar. @return true si la hay. */
    bool available() const noexcept { return !m_command.empty(); }

    /**
     * @brief Resuelve un operador consultando al compilador.
     *
     * La respuesta se recuerda: en una unidad de traduccion de C++ la misma
     * pregunta aparece decenas de veces y, medido, una consulta suelta cuesta
     * unos 23 ms.  Sin memoria el coste seria inasumible.
     *
     * @param op  Operador, p.ej. "__has_builtin".
     * @param arg Texto entre parentesis, tal cual.
     * @return 1 o 0.  Tambien 0 cuando no hay compilador al que preguntar: dar
     *         por buena una capacidad sin comprobarla haria que las cabeceras
     *         tomasen ramas que ese compilador no soporta, y el fallo saldria
     *         mucho mas tarde y mucho peor.
     */
    int64_t query(const std::string& op, const std::string& arg);

    /**
     * @brief Pregunta al compilador si un nombre existe para el.
     *
     * Distinta de query(): esta no pide un valor sino la existencia.  Hace
     * falta porque el juego de operadores depende del modo -- clang tiene
     * `__has_cpp_attribute` compilando C++ y no compilando C -- y darlos todos
     * por buenos lleva a preguntar cosas que ese compilador rechaza, y con
     * ellas a ramas que el nunca habria tomado.
     *
     * @param name Nombre a comprobar.
     * @return true si el compilador lo tiene definido.
     */
    bool is_known(const std::string& name);

    /** @brief Cuantas respuestas hay recordadas. @return Numero de entradas. */
    std::size_t remembered() const noexcept { return m_cache.size(); }

    /** @brief Olvida todo lo aprendido. */
    void clear() noexcept { m_cache.clear(); }

private:
    /**
     * @brief Clave con la que se recuerda una respuesta.
     *
     * Incluye a QUIEN se pregunto, no solo que se pregunto.  Es lo que hace
     * correcto el recuerdo cuando en una misma maquina conviven varios
     * compiladores -- el de MinGW, el de MSVC, el de WSL -- y lo que hara
     * correcto un cache en disco el dia que se anada: la respuesta del gcc de
     * WSL no vale para el MinGW de Windows aunque la pregunta sea identica.
     *
     * @param op  Operador.
     * @param arg Argumento.
     * @return Clave de la terna (compilador, operador, argumento).
     */
    std::string key(const std::string& op, const std::string& arg) const;

    /**
     * @brief Lanza la consulta al compilador.
     *
     * Devuelve por separado SI hubo respuesta y CUAL fue.  No es un remilgo:
     * antes las dos cosas venian en el mismo 0 y no habia forma de distinguir
     * "el compilador dice que no" de "no pude preguntar".  Mientras la memoria
     * duraba lo que el proceso se aguantaba; guardando en disco, un fallo
     * pasajero -- el compilador ocupado, un PATH a medio poner -- quedaria
     * escrito como una propiedad del compilador y sobreviviria a su causa.
     *
     * @param op    Operador.
     * @param arg   Argumento.
     * @param value Recibe 1 o 0 si hubo respuesta.
     * @return true si el compilador contesto.
     */
    bool ask(const std::string& op, const std::string& arg,
             int64_t& value) const;

    /**
     * @brief Clave con la que se recuerda una respuesta EN DISCO.
     *
     * Aqui no hace falta meter el compilador: el fichero ya es suyo.  Se
     * escribe con un espacio de separador en vez de un caracter de control
     * para que el fichero se pueda leer a ojo.
     *
     * @param op  Operador.
     * @param arg Argumento.
     * @return Clave del par (operador, argumento).
     */
    static std::string disk_key(const std::string& op, const std::string& arg);

    /** @brief Rehace la memoria en disco tras cambiar de compilador o de sitio. */
    void rebuild_store();

    std::string m_command;    ///< Orden de invocacion del compilador
    std::string m_cache_dir;  ///< Directorio de la memoria; vacio la desactiva

    /**
     * @brief Respuestas ya conocidas, por clave de key().
     *
     * Primer nivel de la memoria: dura lo que el objeto y evita repetir una
     * pregunta dentro de una misma ejecucion.
     */
    std::unordered_map<std::string, int64_t> m_cache;

    /**
     * @brief Segundo nivel: lo aprendido en ejecuciones anteriores.
     *
     * Nulo cuando no hay memoria en disco -- porque no se pidio, o porque no se
     * pudo identificar al compilador y recordar seria peor que no recordar.
     */
    std::unique_ptr<FactsCache> m_store;
};

} // namespace vpp
