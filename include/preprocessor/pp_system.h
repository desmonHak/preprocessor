/**
 * @file pp_system.h
 * @brief Consultas al sistema donde corre vpp: entorno y rutas de plataforma.
 */
#pragma once

#include <string>
#include <vector>

namespace vpp {

/**
 * @brief Lee una variable de entorno.
 * @param name Nombre de la variable.
 * @return Su valor, o cadena vacia si no esta definida.
 */
std::string env_value(const char* name);

/**
 * @brief Lee una variable de entorno con forma de lista de rutas.
 *
 * El separador depende del sistema -- ';' en Windows, ':' en el resto -- y es
 * justo el detalle que no debe repartirse por el resto del codigo.
 *
 * @param name Nombre de la variable, p.ej. "PATH".
 * @return Los elementos NO vacios, en orden.
 */
std::vector<std::string> env_path_list(const char* name);

/**
 * @brief Localiza un ejecutable como lo haria el interprete de ordenes.
 *
 * Si el nombre trae separador de directorio se toma tal cual; si no, se busca
 * por el PATH.  En Windows se prueban ademas las extensiones de PATHEXT, porque
 * `gcc` en el PATH es en realidad `gcc.exe`.
 *
 * @param name Nombre o ruta del ejecutable.
 * @return Ruta absoluta y con los enlaces resueltos, o vacia si no aparece.
 */
std::string find_executable(const std::string& name);

/**
 * @brief Directorio donde este usuario guarda datos que se pueden regenerar.
 *
 * `$XDG_CACHE_HOME/vpp` o `~/.cache/vpp` en Unix, `%LOCALAPPDATA%\vpp` en
 * Windows.
 *
 * @return La ruta, o vacia si no se pudo averiguar.
 */
std::string user_cache_dir();

} // namespace vpp
