/**
 * @file pp_capabilities.h
 * @brief Consulta las capacidades del compilador de destino.
 */
#pragma once

#include <cstdint>
#include <string>
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
 * asi se puede probar sola, y el cache persistente que venga despues se anade
 * aqui sin tocar nada mas.
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
     * @param op  Operador.
     * @param arg Argumento.
     * @return 1 o 0 segun conteste; 0 si la invocacion falla.
     */
    int64_t ask(const std::string& op, const std::string& arg) const;

    std::string m_command;   ///< Orden de invocacion del compilador

    /**
     * @brief Respuestas ya conocidas, por clave de key().
     *
     * Dura lo que el objeto.  Es AQUI donde entra un cache persistente cuando
     * se quiera: basta rellenarlo al construir y volcarlo al destruir, sin que
     * nada de fuera se entere, porque la clave ya distingue el compilador.
     */
    std::unordered_map<std::string, int64_t> m_cache;
};

} // namespace vpp
