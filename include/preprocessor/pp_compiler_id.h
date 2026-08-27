/**
 * @file pp_compiler_id.h
 * @brief Identifica al compilador que nombra una orden de invocacion.
 */
#pragma once

#include <string>

namespace vpp {

/**
 * @brief Identidad de un compilador, deducida de la orden con la que se invoca.
 *
 * Existe para poder RECORDAR entre ejecuciones lo que un compilador contesta.
 * La orden por si sola no sirve de clave, y usarla seria un error silencioso en
 * dos direcciones: `gcc` y `/usr/bin/gcc` son el mismo compilador con dos
 * cadenas distintas, y -- lo que de verdad hace dano -- la misma cadena pasa a
 * significar otra cosa en cuanto se actualiza el compilador.  Una memoria con
 * esa clave devolveria respuestas viejas para siempre.
 *
 * Lo que identifica de verdad es el BINARIO: su ruta real, su tamano y su fecha.
 * Un cambio de version cambia los tres.  Y con el van los FLAGS, porque tambien
 * cambian la respuesta: `-x c++` habilita operadores que en C no existen, y
 * `--target=` cambia lo que contestan los predicados de objetivo.
 */
class CompilerId {
public:
    /**
     * @brief Calcula la huella de la orden dada.
     *
     * @param command Orden completa, p.ej. "gcc -E -P -x c".
     * @return Huella en hexadecimal, o cadena VACIA si no se pudo identificar
     *         el ejecutable.  Vacia significa "no me fio": sin saber a quien se
     *         pregunta no hay forma de notar que ha cambiado, y quien la reciba
     *         debe renunciar a recordar nada en disco.
     */
    static std::string fingerprint(const std::string& command);

    /**
     * @brief Extrae el ejecutable de una orden, sin resolverlo.
     *
     * Respeta las comillas, que hacen falta de verdad en Windows: la ruta de
     * MSVC lleva espacios.
     *
     * @param command Orden completa.
     * @return Primer argumento, ya sin comillas; vacio si la orden lo esta.
     */
    static std::string executable_of(const std::string& command);
};

} // namespace vpp
