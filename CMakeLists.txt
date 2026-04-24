cmake_minimum_required(VERSION 3.16)
project(vpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- opciones de compilacion -----------------------------------------------

option(VPP_BUILD_TESTS "Compilar los tests del preprocesador" ON)
option(VPP_BUILD_EXE   "Compilar el ejecutable vpp"           ON)

# --- biblioteca estatica (core del preprocesador) --------------------------

set(VPP_SOURCES
    src/pp_diagnostics.cpp
    src/pp_lexer.cpp
    src/pp_parser.cpp
    src/pp_macro.cpp
    src/pp_evaluator.cpp
    src/preprocessor.cpp
)

add_library(vpp_lib STATIC ${VPP_SOURCES})

target_include_directories(vpp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(vpp_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    >
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
    >
)

# en Windows: flags de plataforma para MinGW
if(WIN32)
    target_compile_definitions(vpp_lib PRIVATE
        _WIN32_WINNT=0x0601   # Windows 7 minimo
    )
endif()

# --- ejecutable principal --------------------------------------------------

if(VPP_BUILD_EXE)
    add_executable(vpp src/main.cpp)
    target_link_libraries(vpp PRIVATE vpp_lib)

    # en MinGW necesitamos stdc++fs para <filesystem>
    if(MINGW OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_NAME STREQUAL "Windows"))
        target_link_libraries(vpp PRIVATE stdc++fs)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
        target_link_libraries(vpp PRIVATE stdc++fs)
    endif()

    # copiar include_lib junto al ejecutable en tiempo de build
    # (permite usar #import <vesta/...> sin instalar ni configurar rutas)
    add_custom_command(TARGET vpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/include_lib"
            "$<TARGET_FILE_DIR:vpp>/include_lib"
        COMMENT "Copiando stdlib vpp (include_lib) al directorio de salida"
    )

    install(TARGETS vpp RUNTIME DESTINATION bin)
    # instalar include_lib junto al ejecutable en el layout instalado
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include_lib"
            DESTINATION bin)
endif()

# --- tests -----------------------------------------------------------------

if(VPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- resumen de configuracion ----------------------------------------------

message(STATUS "vpp ${PROJECT_VERSION} configurado:")
message(STATUS "  Compilador    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Estandar C++  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests         : ${VPP_BUILD_TESTS}")
message(STATUS "  Ejecutable    : ${VPP_BUILD_EXE}")
