cmake_minimum_required(VERSION 3.16)

# --- helper para crear un test executable ----------------------------------

function(add_vpp_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE vpp_lib)

    # vinculacion de stdc++fs en compiladores antiguos / MinGW
    if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_NAME STREQUAL "Windows"))
        target_link_libraries(${name} PRIVATE stdc++fs)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
        target_link_libraries(${name} PRIVATE stdc++fs)
    endif()

    target_compile_definitions(${name} PRIVATE VPP_TESTING)

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

# --- targets de test -------------------------------------------------------

add_vpp_test(vpp_test_lexer        test_lexer.cpp)
add_vpp_test(vpp_test_macros       test_macros.cpp)
add_vpp_test(vpp_test_conditionals test_conditionals.cpp)
add_vpp_test(vpp_test_includes     test_includes.cpp)
add_vpp_test(vpp_test_metaprog     test_metaprog.cpp)
add_vpp_test(vpp_test_new_features test_new_features.cpp)
add_vpp_test(vpp_test_variables    test_variables.cpp)
add_vpp_test(vpp_test_float_conv   test_float_conv.cpp)
add_vpp_test(vpp_test_import       test_import.cpp)
add_vpp_test(vpp_test_c_api        test_c_api.cpp)
add_vpp_test(vpp_test_include_search test_include_search.cpp)
add_vpp_test(vpp_test_compiler_id  test_compiler_id.cpp)
add_vpp_test(vpp_test_facts_cache  test_facts_cache.cpp)
add_vpp_test(vpp_test_dialect      test_dialect.cpp)
add_vpp_test(vpp_test_deps         test_deps.cpp)
add_vpp_test(vpp_test_diag_render  test_diag_render.cpp)

# --- suite de conformidad con el preprocesador de C -------------------------
#
# Diffea la salida de vpp contra la de gcc/clang sobre un corpus de casos.  Los
# tests de arriba comprueban lo que vpp construyo; este comprueba lo que el
# estandar de C exige, que es lo que sostiene el objetivo de ser un
# preprocesador usable desde cualquier lenguaje.
#
# Necesita un compilador de referencia.  Si no hay ninguno, el test no se
# registra en vez de fallar: es preferible a un rojo que no dice nada del
# codigo.
find_program(VPP_REF_CC NAMES gcc clang cc)
if(VPP_REF_CC AND TARGET vpp)
    add_test(NAME vpp_test_conformance
        COMMAND ${CMAKE_COMMAND}
            -DVPP=$<TARGET_FILE:vpp>
            -DCC=${VPP_REF_CC}
            -DCASES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/conformance/cases
            -DXFAIL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/conformance/xfail.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_conformance.cmake)
    set_tests_properties(vpp_test_conformance PROPERTIES TIMEOUT 180)

    # Prueba de integracion: el corpus mide casos aislados, esto mide que vpp
    # sirva para codigo real.  Preprocesa un fuente con cabeceras del sistema,
    # compila la salida y ejecuta el binario.  Es lo que justifica --predef-from:
    # sin las macros predefinidas del compilador, esas cabeceras toman ramas
    # equivocadas y el resultado no compila aunque el preprocesado no de errores.
    add_test(NAME vpp_test_system_headers
        COMMAND ${CMAKE_COMMAND}
            -DVPP=$<TARGET_FILE:vpp>
            -DCC=${VPP_REF_CC}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/sys_headers
            -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_system_headers.cmake)
    set_tests_properties(vpp_test_system_headers PROPERTIES TIMEOUT 180)

    # La misma prueba en C++.  No es redundante: las cabeceras de libstdc++
    # exigen cosas que las de C no tocan -- los operadores de prueba de
    # caracteristicas y #include_next para superponerse a las de C -- asi que es
    # el unico sitio donde esos dos caminos se ejercitan de verdad.
    find_program(VPP_REF_CXX NAMES g++ clang++ c++)
    if(VPP_REF_CXX)
        add_test(NAME vpp_test_system_headers_cxx
            COMMAND ${CMAKE_COMMAND}
                -DVPP=$<TARGET_FILE:vpp>
                -DCC=${VPP_REF_CXX}
                -DLANG=c++
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/sys_headers_cxx
                -P ${CMAKE_CURRENT_SOURCE_DIR}/conformance/run_system_headers.cmake)
        set_tests_properties(vpp_test_system_headers_cxx PROPERTIES TIMEOUT 300)
        message(STATUS "  Cabeceras C++ : ${VPP_REF_CXX}")
    else()
        message(STATUS "  Cabeceras C++ : sin compilador de C++; test omitido")
    endif()
    message(STATUS "  Conformidad   : ${VPP_REF_CC}")
else()
    message(STATUS "  Conformidad   : sin compilador de referencia; test omitido")
endif()

# --- la biblioteca compartida solo exporta el ABI en C ---------------------
#
# Es la invariante que sostiene todo el diseno: en cuanto se escapa un simbolo
# de C++, la biblioteca deja de ser consumible desde otro compilador.  Ya paso
# una vez en ELF -- se colaron 9 plantillas de libstdc++ pese a la visibilidad
# oculta -- asi que se comprueba en cada build en lugar de a mano.
if(TARGET vpp_shared)
    add_test(NAME vpp_test_exports
        COMMAND ${CMAKE_COMMAND}
            -DLIB=$<TARGET_FILE:vpp_shared>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
    set_tests_properties(vpp_test_exports PROPERTIES TIMEOUT 60)
endif()

# --- target de conveniencia para correr todos los tests --------------------

add_custom_target(vpp_run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -V
    DEPENDS
        vpp_test_lexer
        vpp_test_macros
        vpp_test_conditionals
        vpp_test_includes
        vpp_test_metaprog
        vpp_test_new_features
        vpp_test_variables
        vpp_test_float_conv
        vpp_test_import
        vpp_test_c_api
        vpp_test_include_search
        vpp_test_compiler_id
        vpp_test_facts_cache
        vpp_test_dialect
        vpp_test_deps
        vpp_test_diag_render
    COMMENT "Ejecutando todos los tests del preprocesador vpp"
)
