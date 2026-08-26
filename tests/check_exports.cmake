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

# Cada plataforma trae su herramienta; se prueba en orden y se usa la primera.
find_program(NM_TOOL      nm)
find_program(OBJDUMP_TOOL objdump)
find_program(DUMPBIN_TOOL dumpbin)

set(SIMBOLOS "")

if(WIN32 AND OBJDUMP_TOOL)
    # objdump -p lista la tabla de exportacion del PE
    execute_process(COMMAND "${OBJDUMP_TOOL}" -p "${LIB}"
                    OUTPUT_VARIABLE salida RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "objdump fallo sobre ${LIB}")
    endif()
    string(REPLACE "\n" ";" _lineas "${salida}")
    foreach(_l IN LISTS _lineas)
        # las entradas van como "  [  N] nombre"
        if(_l MATCHES "^[ \t]*\\[[ \t]*[0-9]+\\][ \t]+([A-Za-z_][A-Za-z0-9_@?$]*)")
            list(APPEND SIMBOLOS "${CMAKE_MATCH_1}")
        endif()
    endforeach()

elseif(WIN32 AND DUMPBIN_TOOL)
    execute_process(COMMAND "${DUMPBIN_TOOL}" /exports "${LIB}"
                    OUTPUT_VARIABLE salida RESULT_VARIABLE rc)
    string(REPLACE "\n" ";" _lineas "${salida}")
    foreach(_l IN LISTS _lineas)
        # "  ordinal  hint  RVA  nombre"
        if(_l MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([A-Za-z_?][A-Za-z0-9_@?$]*)")
            list(APPEND SIMBOLOS "${CMAKE_MATCH_1}")
        endif()
    endforeach()

elseif(NM_TOOL)
    # -D: tabla dinamica; --defined-only: lo que la biblioteca APORTA, no lo
    # que necesita de fuera
    execute_process(COMMAND "${NM_TOOL}" -D --defined-only "${LIB}"
                    OUTPUT_VARIABLE salida RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "nm fallo sobre ${LIB}")
    endif()
    string(REPLACE "\n" ";" _lineas "${salida}")
    foreach(_l IN LISTS _lineas)
        # "direccion TIPO nombre"; solo interesan los de codigo y datos
        if(_l MATCHES "^[0-9A-Fa-f]+[ \t]+[TtDdBbWwRr][ \t]+([A-Za-z_][A-Za-z0-9_.$]*)")
            list(APPEND SIMBOLOS "${CMAKE_MATCH_1}")
        endif()
    endforeach()

else()
    message(STATUS "sin nm, objdump ni dumpbin: no se puede comprobar; se omite")
    return()
endif()

if(NOT SIMBOLOS)
    message(FATAL_ERROR
        "no se extrajo ningun simbolo de ${LIB}; la comprobacion no vale de "
        "nada si no lee la tabla, asi que se falla en vez de dar un falso ok")
endif()

# --- veredicto ---------------------------------------------------------------

set(AJENOS "")
set(N_VPP 0)

foreach(sym IN LISTS SIMBOLOS)
    if(sym MATCHES "^_?vpp_")
        math(EXPR N_VPP "${N_VPP} + 1")
    # Los auxiliares que el propio enlazador mete en un PE no son codigo
    # nuestro y no cuentan.
    elseif(sym MATCHES "^(_head_|__imp_|.*_dll_iname$|_DllMain)")
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

message(STATUS "exporta ${N_VPP} funciones del ABI en C y nada mas")
