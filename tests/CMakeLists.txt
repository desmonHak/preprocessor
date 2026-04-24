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
    COMMENT "Ejecutando todos los tests del preprocesador vpp"
)
