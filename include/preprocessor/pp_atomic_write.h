/**
 * @file pp_atomic_write.h
 * @brief Escritura de un fichero completo sin que nadie lo vea a medias.
 */
#pragma once

#include <string>

namespace vpp {

/**
 * @brief Escribe un fichero de forma que nunca se lea incompleto.
 *
 * Se escribe en un temporal del mismo directorio y despues se renombra encima
 * del destino.  Renombrar es atomico en los dos sistemas, asi que quien lea a
 * la vez ve el contenido viejo entero o el nuevo entero, jamas una mezcla.
 *
 * Sirve para escribir desde varios procesos a la vez sin ningun cerrojo, que es
 * la situacion normal de vpp: una compilacion en paralelo lo lanza N veces.  Lo
 * que NO da es control de concurrencia -- dos escrituras simultaneas siguen
 * pudiendo pisarse y gana la ultima -- de modo que solo vale cuando perder una
 * escritura es aceptable.
 *
 * @param path    Ruta del fichero destino.  Se crean los directorios que falten.
 * @param content Contenido completo a escribir.
 * @return true si el fichero quedo escrito.
 */
bool write_file_atomically(const std::string& path, const std::string& content);

} // namespace vpp
