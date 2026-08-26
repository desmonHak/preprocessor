# check_exports.cmake -- la biblioteca compartida solo exporta el ABI en C.
#
# Es LA invariante del diseno.  Que la .dll/.so exponga unicamente funciones C
# es lo que la hace consumible desde otro compilador y desde otro lenguaje; en
# cuanto se escapa un simbolo de C++ eso deja de ser cierto, y ademas los de
# libstdc++ son debiles y pueden interponerse con los del programa anfitrion.
#
# No es una preocupacion teorica: en ELF ya paso.  CXX_VISIBILITY_PRESET=hidden
# no basta, porque las plantillas de libstdc++ llegan desde sus cabeceras con
# visibilidad "default"; se colaron 9 simbolos y hubo que anadir un version
# script.  Sin una comprobacion automatica, el siguiente se cuela igual.
#
# Uso:
#   cmake -DLIB=<ruta a la biblioteca> -P check_exports.cmake

if(NOT LIB)
    message(FATAL_ERROR "falta -DLIB=<ruta>")
endif()
if(NOT EXISTS "${LIB}")
    message(FATAL_ERROR "no existe: ${LIB}")
endif()

# --- obtener la lista de simbolos exportados --------------------------------

# Se PRUEBAN todas las herramientas disponibles y se usa la primera que
# devuelva algo, en lugar de elegir una por plataforma.  El formato de salida
# varia entre versiones y entre distribuciones, y comprometerse con una sola
# convierte una diferencia de formato en un fallo de la suite.
find_program(NM_TOOL      nm)
find_program(OBJDUMP_TOOL objdump)
find_program(DUMPBIN_TOOL dumpbin)

set(SIMBOLOS "")
set(INTENTOS "")

# @brief Extrae nombres de simbolos de la salida de una herramienta.
# @param texto  Salida completa de la herramienta.
# @param patron Expresion regular con UN grupo: el nombre.
# @param dest   Variable donde acumular los nombres.
function(extraer texto patron dest)
    set(acc "${${dest}}")
    string(REPLACE "\n" ";" _lineas "${texto}")
    foreach(_l IN LISTS _lineas)
        if(_l MATCHES "${patron}")
            list(APPEND acc "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${dest} "${acc}" PARENT_SCOPE)
endfunction()

# El orden depende de la plataforma: sobre un PE, `nm -D` no lista la tabla de
# exportacion sino TODOS los simbolos del objeto -- miles -- asi que ahi hay que
# ir a objdump o dumpbin.  Dentro de cada plataforma si se prueba una tras otra.
if(WIN32)
    set(USAR_NM OFF)
else()
    set(USAR_NM ON)
endif()

# nm -D: tabla dinamica de un ELF/Mach-O.  --defined-only deja fuera lo que la
# biblioteca NECESITA, que no es lo que exporta.
if(USAR_NM AND NM_TOOL AND NOT SIMBOLOS)
    execute_process(COMMAND "${NM_TOOL}" -D --defined-only "${LIB}"
                    OUTPUT_VARIABLE salida ERROR_QUIET RESULT_VARIABLE rc)
    if(rc EQUAL 0)
        # "direccion TIPO nombre"; en Mach-O el nombre lleva guion bajo delante
        extraer("${salida}" "^[0-9A-Fa-f]+[ \t]+[TtDdBbWwRrSs][ \t]+(.+)$" SIMBOLOS)
        list(APPEND INTENTOS "nm")
    endif()
endif()

# objdump -p: tabla de exportacion de un PE.
if(OBJDUMP_TOOL AND NOT SIMBOLOS)
    execute_process(COMMAND "${OBJDUMP_TOOL}" -p "${LIB}"
                    OUTPUT_VARIABLE salida ERROR_QUIET RESULT_VARIABLE rc)
    if(rc EQUAL 0)
        # Hay que ACOTAR a la seccion de nombres.  La salida de objdump -p trae
        # tambien la tabla de reubicaciones, cuyas lineas acaban en "] DIR64" y
        # encajan igual de bien en un patron generico: capturarlas daba miles de
        # "simbolos" inexistentes.
        string(REPLACE "
" ";" _ls "${salida}")
        set(_dentro OFF)
        foreach(_l IN LISTS _ls)
            if(_l MATCHES "Name Pointer")
                set(_dentro ON)
            elseif(_dentro)
                if(_l MATCHES "^[ 	]*\[[ 	]*[0-9]+\][ 	]+([A-Za-z_][A-Za-z0-9_@?$]*)[ 	]*$")
                    list(APPEND SIMBOLOS "${CMAKE_MATCH_1}")
                else()
                    set(_dentro OFF)   # se acabo la seccion
                endif()
            endif()
        endforeach()
        list(APPEND INTENTOS "objdump")
    endif()
endif()

# dumpbin /exports: el equivalente de MSVC.
if(DUMPBIN_TOOL AND NOT SIMBOLOS)
    execute_process(COMMAND "${DUMPBIN_TOOL}" /exports "${LIB}"
                    OUTPUT_VARIABLE salida ERROR_QUIET RESULT_VARIABLE rc)
    if(rc EQUAL 0)
        extraer("${salida}" "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([A-Za-z_?][A-Za-z0-9_@?$]*)" SIMBOLOS)
        list(APPEND INTENTOS "dumpbin")
    endif()
endif()

if(NOT INTENTOS)
    message(STATUS "sin nm, objdump ni dumpbin: no se puede comprobar; se omite")
    return()
endif()

if(NOT SIMBOLOS)
    # Se falla, no se omite: una comprobacion que no lee la tabla no comprueba
    # nada, y darla por buena seria peor que no tenerla.  Se vuelca lo que vio
    # la herramienta, porque este script corre sobre todo en maquinas de CI
    # donde no se puede depurar a mano.
    message(STATUS "herramientas probadas: ${INTENTOS}")
    string(SUBSTRING "${salida}" 0 1200 _muestra)
    message(STATUS "primeros bytes de la salida:\n${_muestra}")
    message(FATAL_ERROR
        "no se extrajo ningun simbolo de ${LIB}")
endif()

# --- veredicto ---------------------------------------------------------------

set(AJENOS "")
set(N_VPP 0)

foreach(sym IN LISTS SIMBOLOS)
    if(sym MATCHES "^_?vpp_")
        math(EXPR N_VPP "${N_VPP} + 1")
    # Lo que mete el propio enlazador en un PE no es codigo nuestro.
    elseif(sym MATCHES "^(_head_|__imp_|_DllMain)" OR sym MATCHES "_dll_iname$")
        # ruido del enlazador de Windows
    else()
        list(APPEND AJENOS "${sym}")
    endif()
endforeach()

if(AJENOS)
    list(LENGTH AJENOS n)
    message(STATUS "simbolos que NO son del ABI en C (${n}):")
    foreach(s IN LISTS AJENOS)
        message(STATUS "    ${s}")
    endforeach()
    message(FATAL_ERROR
        "la biblioteca compartida exporta simbolos ajenos al ABI en C.  Si son "
        "de C++ (empiezan por _Z o ?), revisa el version script de "
        "cmake/vpp.map o la lista de cmake/vpp.symbols.")
endif()

if(N_VPP EQUAL 0)
    message(FATAL_ERROR "la biblioteca no exporta NINGUNA funcion vpp_*")
endif()

message(STATUS "exporta ${N_VPP} funciones del ABI en C y nada mas (via ${INTENTOS})")
