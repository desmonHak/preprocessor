# run_system_headers.cmake -- prueba de integracion con las cabeceras del
#                              sistema.
#
# El corpus de conformidad mide casos aislados.  Esto mide lo otro: que vpp
# sirva de verdad para procesar codigo real.  Preprocesa un fuente que incluye
# una cabecera del sistema, COMPILA la salida con el compilador de referencia y
# EJECUTA el binario.  Si algo del camino se rompe, se rompe aqui.
#
# Es la prueba que justifica --predef-from: sin las macros predefinidas del
# compilador, las cabeceras de la biblioteca estandar toman ramas equivocadas
# (creen que no hay GCC, por ejemplo) y la salida no compila aunque el
# preprocesado no de un solo error.
#
# Uso:
#   cmake -DVPP=<ruta> -DCC=<ruta> -DWORK_DIR=<dir> -P run_system_headers.cmake

if(NOT VPP OR NOT CC OR NOT WORK_DIR)
    message(FATAL_ERROR "faltan -DVPP=, -DCC= o -DWORK_DIR=")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

# --- fuente de prueba -------------------------------------------------------

set(SRC "${WORK_DIR}/sys_headers.c")
file(WRITE "${SRC}"
"#include <stdio.h>\n"
"#include <string.h>\n"
"int main(void) {\n"
"    char buf[32];\n"
"    strcpy(buf, \"vpp\");\n"
"    printf(\"%s:%d\", buf, (int)strlen(buf));\n"
"    return 0;\n"
"}\n")

# --- rutas de inclusion del compilador de referencia ------------------------

# Se le preguntan AL COMPILADOR en vez de adivinarlas: cambian con la version,
# el sistema y la distribucion.
execute_process(
    COMMAND "${CC}" -E -Wp,-v -x c -
    INPUT_FILE  "${SRC}"
    OUTPUT_QUIET
    ERROR_VARIABLE cc_verbose
    RESULT_VARIABLE cc_rc)

set(INC_ARGS "")
string(REPLACE "\n" ";" _lines "${cc_verbose}")
foreach(_l IN LISTS _lines)
    string(STRIP "${_l}" _l)
    # las rutas de busqueda salen como lineas indentadas con una ruta absoluta
    if(_l AND EXISTS "${_l}" AND IS_DIRECTORY "${_l}")
        list(APPEND INC_ARGS "-I${_l}")
    endif()
endforeach()

if(NOT INC_ARGS)
    message(STATUS "no se pudieron obtener las rutas de inclusion de ${CC}; "
                   "se omite la prueba")
    return()
endif()

# --- preprocesar con vpp ----------------------------------------------------

set(PP_OUT "${WORK_DIR}/sys_headers.pp.c")

execute_process(
    COMMAND "${VPP}" --predef-from "${CC} -dM -E -" ${INC_ARGS} "${SRC}"
    OUTPUT_FILE     "${PP_OUT}"
    ERROR_VARIABLE  pp_err
    RESULT_VARIABLE pp_rc)

if(NOT pp_rc EQUAL 0)
    # Se imprime el CONTEXTO de cada error, no solo su ubicacion.  Esta prueba
    # corre sobre las cabeceras de la maquina, que cambian con el sistema y con
    # la version del compilador, asi que cuando falla en un CI no hay forma de
    # ir a mirar el fichero: si la prueba no trae la linea culpable consigo, el
    # informe no sirve para arreglar nada.
    string(REPLACE "\n" ";" _errs "${pp_err}")
    foreach(_e IN LISTS _errs)
        if(_e MATCHES "^[ \t]*([^:]+):([0-9]+):[0-9]+: error")
            set(_f "${CMAKE_MATCH_1}")
            set(_n "${CMAKE_MATCH_2}")
            # la ruta del error es relativa; se busca en las de inclusion
            foreach(_inc IN LISTS INC_ARGS)
                string(REGEX REPLACE "^-I" "" _dir "${_inc}")
                file(GLOB_RECURSE _cand "${_dir}/${_f}")
                if(_cand)
                    list(GET _cand 0 _ruta)
                    file(STRINGS "${_ruta}" _lineas)
                    math(EXPR _desde "${_n} - 2")
                    math(EXPR _hasta "${_n} + 1")
                    message(STATUS "--- ${_ruta}:${_n} ---")
                    set(_i 1)
                    foreach(_l IN LISTS _lineas)
                        if(_i GREATER_EQUAL _desde AND _i LESS_EQUAL _hasta)
                            message(STATUS "  ${_i}: ${_l}")
                        endif()
                        math(EXPR _i "${_i} + 1")
                    endforeach()
                    break()
                endif()
            endforeach()
        endif()
    endforeach()
    message(FATAL_ERROR
        "vpp fallo al preprocesar cabeceras del sistema (${pp_rc}):\n${pp_err}")
endif()

file(SIZE "${PP_OUT}" pp_size)
if(pp_size EQUAL 0)
    message(FATAL_ERROR "vpp no emitio nada al preprocesar ${SRC}")
endif()
message(STATUS "preprocesado: ${pp_size} bytes")

# --- compilar la salida y ejecutarla ----------------------------------------

set(EXE "${WORK_DIR}/sys_headers_prog")

execute_process(
    COMMAND "${CC}" -x c "${PP_OUT}" -o "${EXE}"
    ERROR_VARIABLE  cc_err
    RESULT_VARIABLE cc_rc2)

if(NOT cc_rc2 EQUAL 0)
    # Mismo motivo que en el paso anterior, y con mas razon: el fichero que no
    # compila lo genera la propia prueba y solo existe en la maquina que la
    # ejecuta, asi que sin traerse las lineas culpables un fallo en un CI no se
    # puede diagnosticar sin reproducir el entorno entero.
    file(STRINGS "${PP_OUT}" _pplineas)
    list(LENGTH _pplineas _pptotal)
    string(REPLACE "
" ";" _cerrs "${cc_err}")
    set(_ya "")
    foreach(_e IN LISTS _cerrs)
        if(_e MATCHES "sys_headers\.pp\.c:([0-9]+):")
            set(_n "${CMAKE_MATCH_1}")
            list(FIND _ya "${_n}" _visto)
            if(_visto EQUAL -1)
                list(APPEND _ya "${_n}")
                math(EXPR _desde "${_n} - 2")
                math(EXPR _hasta "${_n} + 1")
                message(STATUS "--- salida generada, linea ${_n} de ${_pptotal} ---")
                set(_i 1)
                foreach(_l IN LISTS _pplineas)
                    if(_i GREATER_EQUAL _desde AND _i LESS_EQUAL _hasta)
                        message(STATUS "  ${_i}: ${_l}")
                    endif()
                    math(EXPR _i "${_i} + 1")
                endforeach()
            endif()
        endif()
    endforeach()
    message(FATAL_ERROR
        "la salida de vpp no compila (${cc_rc2}):
${cc_err}")
endif()

execute_process(
    COMMAND "${EXE}"
    OUTPUT_VARIABLE run_out
    RESULT_VARIABLE run_rc)

if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR "el binario fallo al ejecutarse (${run_rc})")
endif()

string(STRIP "${run_out}" run_out)
if(NOT run_out STREQUAL "vpp:3")
    message(FATAL_ERROR "salida inesperada del binario: '${run_out}'")
endif()

message(STATUS "cabeceras del sistema: preprocesa, compila y ejecuta ('${run_out}')")
