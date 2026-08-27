/**
 * @file pp_diag_render.h
 * @brief Presentacion de un diagnostico con la linea del fuente y el cursor.
 */
#pragma once

#include "pp_diagnostics.h"
#include "pp_source_map.h"

#include <string>

namespace vpp {

/**
 * @brief Da forma a un diagnostico citando la linea que lo provoco.
 *
 * Produce lo mismo que cc:
 *
 *     t.c:2:9: error: directiva de preprocesador desconocida: #defien
 *         2 | #defien FOO 1
 *           |         ^
 *
 * Va aparte del propio diagnostico porque son dos cosas distintas: `Diagnostic`
 * dice QUE paso y donde, y esto decide COMO se ensena.  Separarlas deja que la
 * misma informacion salga tambien en otro formato -- para un editor, por
 * ejemplo -- sin tocar a quien la produce.
 *
 * Si no se tiene el fuente, se devuelve la linea de siempre sin adornos: es
 * preferible a no decir nada.
 *
 * @param d       El diagnostico.
 * @param sources Los fuentes leidos, de donde se saca la linea.
 * @param color   true para resaltar con secuencias ANSI.
 * @return El texto ya formateado, sin salto de linea final.
 */
std::string render_diagnostic(const Diagnostic& d,
                              const SourceMap&  sources,
                              bool              color);

} // namespace vpp
