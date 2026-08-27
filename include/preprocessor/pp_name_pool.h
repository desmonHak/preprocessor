/**
 * @file pp_name_pool.h
 * @brief Pozo de nombres de fichero compartidos entre ubicaciones.
 */
#pragma once

#include <string>

namespace vpp {

/**
 * @brief Devuelve un nombre de fichero compartido y estable.
 *
 * Cada token arrastra de donde salio, y guardar ese nombre POR VALOR salia muy
 * caro: una ruta de cabecera pasa de los ochenta caracteres, no cabe en el
 * buffer pequeno de `std::string` y por tanto reserva memoria SIEMPRE -- al
 * crear el token y otra vez en cada copia.  Medido sobre una unidad de C++ de
 * verdad, eran 7 de cada 15,8 millones de reservas, para repetir el mismo
 * nombre millones de veces.
 *
 * El pozo lo reparte una sola vez y devuelve un puntero estable, con lo que una
 * ubicacion pasa a ser 16 bytes que se copian sin tocar el heap.
 *
 * Se interna UNA VEZ POR FICHERO, no por token: quien lexa un fichero pide aqui
 * su nombre al empezar y se lo pasa a todos los tokens que produzca.  Por eso el
 * cerrojo que hace falta para compartir el pozo entre hilos no cuesta nada.
 *
 * El pozo no se vacia nunca.  Esta acotado por el numero de ficheros distintos,
 * y los punteros tienen que seguir siendo validos mientras viva algo que los
 * apunte -- incluido un diagnostico que el ABI en C haya entregado a quien
 * llama.
 *
 * @param name Nombre a internar.
 * @return Puntero al nombre compartido; nunca nulo.
 */
const std::string* intern_file_name(const std::string& name);

/**
 * @brief Nombre vacio compartido.
 *
 * Es el que usa una ubicacion sin fichero.  Va aparte de intern_file_name para
 * que construir una ubicacion por defecto no tenga que tocar el pozo ni su
 * cerrojo.
 *
 * @return Puntero a la cadena vacia compartida; nunca nulo.
 */
const std::string* empty_file_name() noexcept;

} // namespace vpp
