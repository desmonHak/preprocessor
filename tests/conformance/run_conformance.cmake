# run_conformance.cmake -- corpus de conformidad con el preprocesador de C.
#
# Compara la salida de vpp con la de un preprocesador de referencia (gcc/clang)
# sobre un corpus de casos.  La razon de existir: la suite de tests unitarios
# comprueba lo que vpp CONSTRUYO, no lo que el estandar de C EXIGE, y por eso
# podia estar entera en verde mientras `#if defined(X)` -- el idioma mas comun
# del preprocesador -- estaba roto.  Aqui el oraculo es el compilador de
# verdad, asi que un hueco de conformidad no puede pasar desapercibido.
#
# Uso:
#   cmake -DVPP=<ruta> -DCC=<ruta> -DCASES_DIR=<dir> [-DXFAIL_FILE=<f>]
#         -P run_conformance.cmake
#
# Fallos esperados: XFAIL_FILE lista, uno por linea, los casos que se sabe que
# fallan.  Un XFAIL que falla no rompe la suite; uno que PASA se reporta como
# XPASS y SI la rompe, para que nadie se olvide de quitarlo de la lista cuando
# se arregla el bug.  Asi la suite documenta la realidad en vez de esconderla.

if(NOT VPP OR NOT CC OR NOT CASES_DIR)
    message(FATAL_ERROR "faltan -DVPP=, -DCC= o -DCASES_DIR=")
endif()

# --- normalizacion ----------------------------------------------------------

# @brief Reduce una salida a su secuencia de tokens.
#
# La comparacion es a nivel de TOKEN, no de linea: los saltos de linea cuentan
# como un blanco mas.  Ese es el contrato real -- dos salidas con los mismos
# tokens compilan igual -- y ademas vpp diverge de gcc en el reparto por lineas
# A PROPOSITO, para no descuadrar la numeracion de las etapas siguientes:
# conserva los saltos dentro de un comentario de bloque y dentro de los
# argumentos de una macro repartida en varias lineas.  Comparando por lineas,
# esas divergencias deliberadas salian como fallos y tapaban las de verdad.
#
# Lo que SI sobrevive a la normalizacion es lo que importa: un token de mas o de
# menos, un espacio ausente entre dos tokens ("ab" frente a "a b"), una comilla
# sin escapar dentro de una cadena o una coma colgando.
#
# @param in_var  Nombre de la variable con el texto.
# @param out_var Nombre de la variable donde dejar el resultado.
function(vpp_normalize in_var out_var)
    set(s "${${in_var}}")
    string(REGEX REPLACE "\r\n" "\n" s "${s}")      # finales de linea
    string(REGEX REPLACE "[ \t\n]+" " " s "${s}")   # todo blanco -> un espacio
    string(STRIP "${s}" s)
    set(${out_var} "${s}" PARENT_SCOPE)
endfunction()

# --- lista de fallos esperados ---------------------------------------------

set(XFAIL_LIST "")
if(XFAIL_FILE AND EXISTS "${XFAIL_FILE}")
    file(STRINGS "${XFAIL_FILE}" _lines)
    foreach(_l IN LISTS _lines)
        string(STRIP "${_l}" _l)
        # se permiten comentarios para dejar escrito POR QUE falla cada uno
        if(_l AND NOT _l MATCHES "^#")
            string(REGEX REPLACE " *#.*$" "" _l "${_l}")
            string(STRIP "${_l}" _l)
            list(APPEND XFAIL_LIST "${_l}")
        endif()
    endforeach()
endif()

# --- recorrido del corpus ---------------------------------------------------

file(GLOB CASES "${CASES_DIR}/*.c")
list(SORT CASES)

set(N_OK 0)
set(N_XFAIL 0)
set(FAILED "")
set(XPASSED "")

foreach(case IN LISTS CASES)
    get_filename_component(name "${case}" NAME)
    get_filename_component(dir  "${case}" DIRECTORY)

    # Referencia.  -P quita los marcadores de linea, que son ruido para esto.
    execute_process(
        COMMAND "${CC}" -E -P "${name}"
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE ref_out
        ERROR_VARIABLE  ref_err
        RESULT_VARIABLE ref_rc)

    execute_process(
        COMMAND "${VPP}" "${name}"
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE vpp_out
        ERROR_VARIABLE  vpp_err
        RESULT_VARIABLE vpp_rc)

    vpp_normalize(ref_out ref_n)
    vpp_normalize(vpp_out vpp_n)

    set(reason "")
    if(NOT ref_rc EQUAL 0)
        # El caso esta mal escrito: si ni la referencia lo procesa, no mide nada.
        set(reason "el compilador de referencia fallo: ${ref_err}")
    elseif(NOT vpp_rc EQUAL 0)
        string(REGEX REPLACE "\n.*$" "" _first "${vpp_err}")
        set(reason "vpp fallo (${vpp_rc}): ${_first}")
    elseif(NOT ref_n STREQUAL vpp_n)
        set(reason "salida distinta")
    endif()

    list(FIND XFAIL_LIST "${name}" _xf)
    if(reason)
        if(_xf GREATER -1)
            math(EXPR N_XFAIL "${N_XFAIL} + 1")
            message(STATUS "XFAIL  ${name}  -- ${reason}")
        else()
            list(APPEND FAILED "${name}")
            message(STATUS "FALLO  ${name}  -- ${reason}")
            if(reason STREQUAL "salida distinta")
                message(STATUS "         esperado: ${ref_n}")
                message(STATUS "         obtenido: ${vpp_n}")
            endif()
        endif()
    else()
        if(_xf GREATER -1)
            list(APPEND XPASSED "${name}")
            message(STATUS "XPASS  ${name}  -- pasa; quitalo de la lista de fallos esperados")
        else()
            math(EXPR N_OK "${N_OK} + 1")
            message(STATUS "OK     ${name}")
        endif()
    endif()
endforeach()

# --- resumen ----------------------------------------------------------------

list(LENGTH CASES  N_TOTAL)
list(LENGTH FAILED N_FAIL)
list(LENGTH XPASSED N_XPASS)

message(STATUS "")
message(STATUS "conformidad: ${N_OK} ok, ${N_XFAIL} fallos esperados, "
               "${N_FAIL} fallos, ${N_XPASS} pasan sin deber, de ${N_TOTAL}")

if(N_FAIL GREATER 0 OR N_XPASS GREATER 0)
    message(FATAL_ERROR "la suite de conformidad no esta en verde")
endif()
