/**
 * @file pp_dialect.h
 * @brief Declaracion del dialecto en la cabecera del propio fichero.
 */
#pragma once

#include "pp_lexer.h"
#include "pp_diagnostics.h"

#include <string>

namespace vpp {

/**
 * @brief Cuantas lineas del principio se miran buscando la declaracion.
 *
 * Mas de una para dejar sitio a un shebang delante, que es el motivo por el que
 * Python mira dos para su declaracion de codificacion.  Y pocas, para que el
 * texto `vpp:` en mitad de un fichero no cambie las reglas por accidente.
 */
constexpr int kDialectHeadLines = 3;

/**
 * @brief Lee la declaracion de dialecto que traiga el fichero y la aplica.
 *
 * vpp no es un preprocesador de C, y el marcador de directiva no puede ser `#`
 * en todos los lenguajes: en Python, shell, Ruby, Make o YAML el `#` es un
 * COMENTARIO.  El fichero puede decir cual es el suyo:
 *
 *     # vpp:marker=%
 *
 * A partir de ahi `%define` es una directiva y `# lo que sea` es texto, con lo
 * que se acaba la disyuntiva entre cazar erratas y no comerse comentarios: lo
 * que empieza por el marcador SIEMPRE es una directiva, y un nombre desconocido
 * ahi siempre es un error.
 *
 * @par Por que en el fichero y no en la linea de ordenes
 * Porque asi el dialecto viaja con el fuente.  Una bandera hay que acordarse de
 * pasarla, se pierde al mover el fichero a otro build y no le dice nada a quien
 * lo abre.  Es el mismo motivo por el que Python declara su codificacion en las
 * dos primeras lineas (PEP 263) en vez de esperarla del entorno.  La bandera
 * sigue existiendo para los ficheros que no se pueden tocar -- generados, o de
 * terceros -- y la declaracion del fichero le gana, por ser mas concreta.
 *
 * @par Como se reconoce
 * Buscando el texto `vpp:` en las PRIMERAS LINEAS, sin exigir ninguna sintaxis
 * alrededor.  Eso es lo que la hace utilizable desde cualquier lenguaje, porque
 * se puede escribir dentro de su comentario:
 *
 *     # vpp:marker=%           Python, shell, Ruby, Make, YAML
 *     -- vpp:marker=%          Lua, SQL, Haskell
 *     // vpp:marker=%          C, C++, Rust, Java
 *     <!-- vpp:marker=% -->    HTML, XML
 *
 * Y resuelve el circulo vicioso de tener que conocer el lenguaje para leer la
 * linea que dice cual es el lenguaje.  Se mira mas de una linea a proposito,
 * para dejar sitio a un shebang delante.
 *
 * El dialecto es POR FICHERO: uno incluido no hereda el de quien lo incluye.
 * Sin eso, un fuente en Python no podria incluir una cabecera de C.
 *
 * @param source Texto del fichero.
 * @param opts   Opciones del lexer, que se modifican si hay declaracion.
 * @param file   Nombre del fichero, para los diagnosticos.
 * @param diag   Motor de diagnosticos.
 * @return true si el fichero traia una declaracion.
 */
bool apply_dialect_line(const std::string& source,
                        LexerOptions&      opts,
                        const std::string& file,
                        DiagnosticEngine&  diag);

} // namespace vpp
