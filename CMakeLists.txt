cmake_minimum_required(VERSION 3.16)
project(vpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Deteccion de "somos el proyecto raiz".
# ---------------------------------------------------------------------------
# Cuando vpp se compila como submodulo (el caso del repo VM, que hace
# add_subdirectory(preprocessor)) no queremos construir el ejecutable, ni la
# biblioteca compartida, ni instalar nada: el padre solo enlaza el .a.  En
# cambio, compilado por si solo, se quiere el paquete completo.
# PROJECT_IS_TOP_LEVEL es de CMake 3.21; aqui se calcula a mano por el minimo
# de 3.16 declarado arriba.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VPP_IS_TOP_LEVEL ON)
else()
    set(VPP_IS_TOP_LEVEL OFF)
endif()

# --- opciones de compilacion -----------------------------------------------

option(VPP_BUILD_TESTS  "Compilar los tests del preprocesador" ON)
option(VPP_BUILD_EXE    "Compilar el ejecutable vpp"           ON)
option(VPP_BUILD_STATIC "Compilar la biblioteca estatica (.a/.lib)" ON)
option(VPP_BUILD_SHARED "Compilar la biblioteca compartida (.dll/.so)"
       ${VPP_IS_TOP_LEVEL})
option(VPP_INSTALL      "Generar las reglas de instalacion y el paquete"
       ${VPP_IS_TOP_LEVEL})
option(VPP_INSTALL_COMMON
       "Instalar los ficheros COMUNES a todas las ABI (cabeceras, exe, docs)"
       ON)

# ---------------------------------------------------------------------------
# Identidad de ABI
# ---------------------------------------------------------------------------
# Una biblioteca estatica NO es intercambiable entre MSVC y MinGW: distinto
# mangling de C++, distinto modelo de excepciones y distinta STL.  Tampoco
# entre 32 y 64 bits.  Es decir, `libvpp.a` de GCC es ilegible para link.exe y
# no hay conversion posible: la unica salida es compilar una vez por ABI y
# guardar cada resultado en su propio sitio en vez de pisarse.
#
# En Windows, por tanto, las bibliotecas se instalan en lib/<arch>-<compilador>/
# (x64-mingw, x86-msvc, ...).  En Unix NO se hace: gcc y clang comparten ABI y
# las distribuciones esperan el lib/ plano de toda la vida.
if(MSVC)
    set(_vpp_abi_cc "msvc")
elseif(WIN32)
    set(_vpp_abi_cc "mingw")
else()
    set(_vpp_abi_cc "")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_vpp_abi_arch "x64")
else()
    set(_vpp_abi_arch "x86")
endif()

if(WIN32)
    set(VPP_ABI_TAG "${_vpp_abi_arch}-${_vpp_abi_cc}")
    # Subdirectorio propio por ABI + componente propio, para que el instalador
    # deje marcar "librerias MinGW x64", "librerias MSVC x86", etc.
    set(VPP_LIBDIR "${CMAKE_INSTALL_LIBDIR}/${VPP_ABI_TAG}")
    string(REPLACE "-" "_" _vpp_abi_sfx "${VPP_ABI_TAG}")
    set(VPP_LIBS_COMPONENT "libs_${_vpp_abi_sfx}")
else()
    set(VPP_ABI_TAG        "${_vpp_abi_arch}")
    set(VPP_LIBDIR         "${CMAKE_INSTALL_LIBDIR}")
    set(VPP_LIBS_COMPONENT "development")
endif()

if(NOT VPP_BUILD_STATIC AND NOT VPP_BUILD_SHARED)
    message(FATAL_ERROR
        "vpp: hay que construir al menos una biblioteca "
        "(VPP_BUILD_STATIC o VPP_BUILD_SHARED)")
endif()

if(VPP_BUILD_EXE AND NOT VPP_BUILD_STATIC)
    message(FATAL_ERROR
        "vpp: el ejecutable usa la API C++ y requiere VPP_BUILD_STATIC=ON")
endif()

if(VPP_BUILD_TESTS AND NOT VPP_BUILD_STATIC)
    message(FATAL_ERROR
        "vpp: los tests usan la API C++ y requieren VPP_BUILD_STATIC=ON")
endif()

# --- fuentes ---------------------------------------------------------------

# Nucleo del preprocesador: la API C++.
set(VPP_SOURCES
    src/pp_diagnostics.cpp
    src/pp_lexer.cpp
    src/pp_parser.cpp
    src/pp_macro.cpp
    src/pp_evaluator.cpp
    src/pp_capabilities.cpp
    src/pp_include.cpp
    src/preprocessor.cpp
)

# Capa del ABI en C.  Se compila en AMBAS bibliotecas: en la compartida es lo
# unico que se exporta, y en la estatica se ofrece ademas de la API C++ para
# que quien enlace el .a pueda elegir cualquiera de las dos.
set(VPP_C_SOURCES
    src/vpp_c.cpp
)

# --- helper comun para configurar un target de biblioteca ------------------

# @brief Aplica a un target de biblioteca las opciones compartidas por la
#        version estatica y la compartida.
# @param tgt Nombre del target.
function(vpp_configure_lib tgt)
    target_include_directories(${tgt} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    # La version se inyecta desde el proyecto para que vpp_version() no pueda
    # desincronizarse del project(VERSION ...) de arriba.
    target_compile_definitions(${tgt} PRIVATE
        VPP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        VPP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        VPP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        VPP_VERSION_STRING="${PROJECT_VERSION}"
    )

    target_compile_options(${tgt} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter
        >
        $<$<CXX_COMPILER_ID:MSVC>:
            /W4
            /wd4100   # equivalente de -Wno-unused-parameter
        >
    )

    # MSVC marca como "inseguras" fopen/localtime y propone sus _s, que no son
    # portables.  El codigo es C estandar a proposito -- compila igual con GCC,
    # Clang y MSVC -- asi que se silencia el aviso en vez de partir el codigo
    # en dos ramas por compilador.
    if(MSVC)
        target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    # PIC siempre, tambien en la estatica: sin esto el .a no puede embeberse
    # dentro de un .so en Linux, que es justo uno de los casos de uso.
    set_target_properties(${tgt} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # en Windows: flags de plataforma para MinGW
    if(WIN32)
        target_compile_definitions(${tgt} PRIVATE
            _WIN32_WINNT=0x0601   # Windows 7 minimo
        )
    endif()
endfunction()

# --- biblioteca estatica ---------------------------------------------------

if(VPP_BUILD_STATIC)
    # El nombre del target sigue siendo vpp_lib: el proyecto VM lo enlaza por
    # ese nombre en una veintena de sitios.  Lo que cambia es el nombre del
    # FICHERO, que pasa a ser el canonico libvpp.a en lugar del redundante
    # libvpp_lib.a.
    add_library(vpp_lib STATIC ${VPP_SOURCES} ${VPP_C_SOURCES})
    add_library(vpp::vpp_static ALIAS vpp_lib)
    vpp_configure_lib(vpp_lib)

    # VPP_STATIC es PUBLIC a proposito: tanto al compilar la biblioteca como al
    # consumirla, VPP_API queda vacio.  Sin esto, un consumidor del .a en
    # Windows veria __declspec(dllimport) y el enlace fallaria.
    target_compile_definitions(vpp_lib PUBLIC VPP_STATIC)

    # El target se llama vpp_lib por compatibilidad con el proyecto VM, pero
    # los alias ALIAS no se exportan: sin EXPORT_NAME, el paquete instalado
    # ofreceria "vpp::vpp_lib" mientras la documentacion y el propio
    # vppConfig.cmake hablan de "vpp::vpp_static", y quien siguiera las
    # instrucciones se encontraria con un target inexistente.
    set_target_properties(vpp_lib PROPERTIES EXPORT_NAME vpp_static)

    # El .a esta hecho de C++, aunque se ofrezca a consumidores en C a traves de
    # vpp_c.h.  CMake anota en el paquete exportado
    # IMPORTED_LINK_INTERFACE_LANGUAGES=CXX, y con eso elige el enlazador de
    # C++ -- pero solo si el proyecto consumidor tiene ese lenguaje habilitado.
    # Un `project(x LANGUAGES C)` enlaza con gcc y falla con "undefined
    # reference to operator new".  La solucion es del lado del consumidor,
    # `project(x LANGUAGES C CXX)`, y esta documentada en el README.
    #
    # NO se anade aqui `INTERFACE stdc++`: probado, en MinGW eso arrastra a la
    # vez el libstdc++.a estatico y el import de la DLL, y el enlace muere con
    # cientos de "multiple definition".  La biblioteca COMPARTIDA no tiene este
    # problema -- lleva la runtime dentro -- y es otra razon para preferirla.

    if(MSVC)
        # En MSVC la import library de la DLL tambien se llama vpp.lib: hay que
        # separar los nombres o una sobrescribe a la otra.
        set_target_properties(vpp_lib PROPERTIES OUTPUT_NAME vpp_static)
    else()
        set_target_properties(vpp_lib PROPERTIES OUTPUT_NAME vpp)
    endif()
endif()

# --- biblioteca compartida (DLL / .so) -------------------------------------

if(VPP_BUILD_SHARED)
    add_library(vpp_shared SHARED ${VPP_SOURCES} ${VPP_C_SOURCES})
    add_library(vpp::vpp_shared ALIAS vpp_shared)
    vpp_configure_lib(vpp_shared)

    # PRIVATE: solo al COMPILAR la biblioteca los simbolos son de exportacion.
    # El consumidor no define esta macro y obtiene dllimport / declaracion
    # normal, que es lo correcto.
    target_compile_definitions(vpp_shared PRIVATE VPP_BUILD_SHARED)

    set_target_properties(vpp_shared PROPERTIES
        OUTPUT_NAME    vpp
        VERSION        ${PROJECT_VERSION}
        SOVERSION      ${PROJECT_VERSION_MAJOR}
        # Todo oculto por defecto: la .so/.dll exporta UNICAMENTE lo marcado
        # con VPP_API, es decir el ABI en C.  Los simbolos C++ mangled se
        # quedan dentro, que es lo que hace la biblioteca consumible desde
        # otros compiladores y otros lenguajes.
        CXX_VISIBILITY_PRESET     hidden
        VISIBILITY_INLINES_HIDDEN ON
    )

    # Sin el prefijo "lib" en Windows: la convencion alli es vpp.dll, que es el
    # nombre que un consumidor pasa a LoadLibrary o a ctypes.CDLL.  En Unix se
    # mantiene el prefijo, que ahi si es la convencion (libvpp.so).
    # PREFIX no admite generator expressions, de ahi el if.
    if(WIN32)
        set_target_properties(vpp_shared PROPERTIES PREFIX "")
    endif()

    # MinGW puede escribir el .def de la DLL como subproducto del enlace.  Sirve
    # para que un consumidor MSVC se fabrique su import library con
    #     lib /def:vpp.def /machine:x64 /out:vpp.lib
    # ya que el libvpp.dll.a de MinGW no es el COFF que espera link.exe.  Con
    # MSVC no hace falta: cl/link ya generan el .lib nativo.
    if(MINGW)
        set(VPP_DEF_FILE "${CMAKE_CURRENT_BINARY_DIR}/vpp.def")
        target_link_options(vpp_shared PRIVATE
            "-Wl,--output-def,${VPP_DEF_FILE}")
    endif()

    # MinGW: enlazar la runtime de C++ de forma estatica dentro de la DLL para
    # que el consumidor no necesite libstdc++-6.dll ni libgcc_s_seh-1.dll al
    # lado.  Es lo que convierte la DLL en un artefacto realmente autonomo.
    if(MINGW)
        target_link_options(vpp_shared PRIVATE
            -static-libgcc -static-libstdc++)
    endif()

    if(NOT WIN32)
        # <filesystem> en GCC anteriores a 9 vive en una biblioteca aparte
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
           CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
            target_link_libraries(vpp_shared PRIVATE stdc++fs)
        endif()

        # Recorte de la tabla de simbolos dinamicos.  CXX_VISIBILITY_PRESET no
        # basta: las plantillas de libstdc++ llegan desde sus cabeceras con
        # visibilidad "default" y se cuelan en la biblioteca (medido: 9 simbolos
        # C++ junto a los 23 del ABI en C).  Al ser simbolos debiles pueden
        # interponerse con los del programa anfitrion.  El detalle, en los
        # ficheros cmake/vpp.map y cmake/vpp.symbols.
        if(APPLE)
            set(_vpp_symfile "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vpp.symbols")
            target_link_options(vpp_shared PRIVATE
                "-Wl,-exported_symbols_list,${_vpp_symfile}")
        else()
            set(_vpp_symfile "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vpp.map")
            target_link_options(vpp_shared PRIVATE
                "-Wl,--version-script,${_vpp_symfile}")
        endif()
        # Sin esto, tocar el fichero no re-enlazaria la biblioteca.
        set_target_properties(vpp_shared PROPERTIES
            LINK_DEPENDS "${_vpp_symfile}")

        # Runtime de C++ dentro de la biblioteca.  Sin esto, la .so arrastra una
        # dependencia de libstdc++6 con la version del compilador que la
        # construyo (medido: >= 13.1 por el simbolo ios_base_library_init), y
        # un .deb con esa dependencia NO INSTALA en una distribucion mas
        # antigua aunque el codigo funcionase perfectamente ahi.
        #
        # Meter libstdc++ dentro de una biblioteca compartida solo es seguro si
        # NINGUN simbolo C++ sale de ella -- si salieran, podrian interponerse
        # con los de la libstdc++ del programa anfitrion.  Aqui lo garantiza el
        # version script de arriba: la .so exporta 23 simbolos y todos son C.
        # En macOS NO: el clang de Apple no acepta -static-libgcc, y ademas la
        # libc++ viene con el sistema, asi que la dependencia no es un problema
        # de distribucion como si lo es la libstdc++ en Linux.
        if(NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(vpp_shared PRIVATE
                -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()

# --- ejecutable principal --------------------------------------------------

if(VPP_BUILD_EXE)
    add_executable(vpp src/main.cpp)
    target_link_libraries(vpp PRIVATE vpp_lib)

    # Mismo motivo que en la biblioteca: el ejecutable viaja en el paquete base
    # y no debe exigir una libstdc++6 concreta del sistema donde se instale.
    if(NOT WIN32 AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(vpp PRIVATE -static-libgcc -static-libstdc++)
    endif()

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
endif()

# --- tests -----------------------------------------------------------------

if(VPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Instalacion y paquete redistribuible
# ---------------------------------------------------------------------------
if(VPP_INSTALL)
    set(VPP_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/vpp")
    set(VPP_ABI_CMAKEDIR     "${VPP_LIBDIR}/cmake/vpp")
    set(VPP_INSTALL_DATADIR  "${CMAKE_INSTALL_DATADIR}/vpp")

    # --- targets de biblioteca + export set --------------------------------
    set(VPP_EXPORT_TARGETS "")
    if(VPP_BUILD_STATIC)
        list(APPEND VPP_EXPORT_TARGETS vpp_lib)
    endif()
    if(VPP_BUILD_SHARED)
        list(APPEND VPP_EXPORT_TARGETS vpp_shared)
    endif()

    # Todo lo de una ABI junto en su directorio, la DLL incluida: en Windows la
    # DLL de MinGW y la de MSVC no pueden compartir carpeta (mismo nombre,
    # binarios distintos).  El .exe no depende de ninguna -- enlaza la estatica
    # y es autonomo -- asi que sacar la DLL de bin/ no rompe nada.
    install(TARGETS ${VPP_EXPORT_TARGETS}
        EXPORT vppTargets
        ARCHIVE  DESTINATION ${VPP_LIBDIR} COMPONENT ${VPP_LIBS_COMPONENT}
        # LIBRARY es el .so de Unix (en Windows el .dll va por RUNTIME, y esta
        # linea no se usa).  Va a "runtime" y no al componente de bibliotecas
        # porque la convencion de las distribuciones es que libvpp.so.1.0.0 y
        # su SONAME viajen en el paquete base: quien solo EJECUTA un programa
        # enlazado a vpp necesita el .so, pero no las cabeceras.  Solo el
        # enlace de desarrollo libvpp.so (el NAMELINK) va al paquete -dev.
        LIBRARY  DESTINATION ${VPP_LIBDIR} COMPONENT runtime
                 NAMELINK_COMPONENT ${VPP_LIBS_COMPONENT}
        RUNTIME  DESTINATION ${VPP_LIBDIR} COMPONENT ${VPP_LIBS_COMPONENT}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # El .def acompana a la DLL de MinGW a la que describe.
    if(VPP_BUILD_SHARED AND MINGW AND VPP_DEF_FILE)
        install(FILES "${VPP_DEF_FILE}"
                DESTINATION ${VPP_LIBDIR} COMPONENT ${VPP_LIBS_COMPONENT})
    endif()

    # --- cabeceras ----------------------------------------------------------
    # Se instala el arbol entero: vpp_c.h es la superficie de la DLL, pero
    # preprocessor.h y sus dependencias hacen falta para quien enlaza el .a y
    # quiere la API C++.
    #
    # Las cabeceras son IDENTICAS para todas las ABI, asi que se instalan una
    # sola vez: los builds secundarios (los que solo aportan su libreria) van
    # con VPP_INSTALL_COMMON=OFF para no duplicarlas ni pisarse entre si.
    if(VPP_INSTALL_COMMON)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/preprocessor"
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                COMPONENT headers
                FILES_MATCHING PATTERN "*.h")
    endif()

    # --- ejecutable ---------------------------------------------------------
    if(VPP_BUILD_EXE AND VPP_INSTALL_COMMON)
        install(TARGETS vpp
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
        # junto al ejecutable, porque main.cpp busca include_lib ahi
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include_lib"
                DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    endif()

    # --- stdlib de macros junto a la biblioteca -----------------------------
    # Quien embebe el preprocesador no pasa por bin/: necesita la misma
    # include_lib en un datadir estable, que es lo que exponen vppConfig.cmake
    # (vpp_INCLUDE_LIB_DIR) y el .pc (vpp_include_lib).  Son 21 KB.
    if(VPP_INSTALL_COMMON)
        install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include_lib"
                DESTINATION ${VPP_INSTALL_DATADIR} COMPONENT runtime)
    endif()

    # --- paquete CMake: find_package(vpp) -----------------------------------
    # Un fichero de targets POR ABI: cada uno apunta a los binarios de su
    # directorio.  El vppConfig.cmake comun (abajo) elige cual cargar segun con
    # que compile el consumidor.
    install(EXPORT vppTargets
        FILE        vppTargets.cmake
        NAMESPACE   vpp::
        DESTINATION ${VPP_ABI_CMAKEDIR}
        COMPONENT   ${VPP_LIBS_COMPONENT}
    )

    # Se pasa a la plantilla para que el layout plano (Unix) pueda rechazar a un
    # consumidor de otra bitness, ya que alli no hay subdirectorio por ABI que
    # distinga los binarios.
    set(VPP_BUILD_SIZEOF_VOID_P "${CMAKE_SIZEOF_VOID_P}")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vppConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vppConfig.cmake"
        INSTALL_DESTINATION ${VPP_INSTALL_CMAKEDIR}
        PATH_VARS VPP_INSTALL_DATADIR
    )

    # ARCH_INDEPENDENT no es cosmetico: sin el, este fichero graba la bitness
    # del build que lo genero y RECHAZA a cualquier consumidor de otra.  Como
    # en el layout multi-ABI es COMUN (lo instala el build primario, de 64
    # bits), un consumidor de 32 bits se topaba con
    #     "no compatible ... version: 1.0.0 (64bit)"
    # aunque las bibliotecas de x86 estuvieran instaladas al lado.  Es decir,
    # las ABI de 32 bits eran inalcanzables por find_package.
    #
    # Aqui la comprobacion sobra ademas de estorbar: quien decide la ABI es el
    # vppConfig.cmake, que mira la arquitectura del consumidor y carga el
    # juego de targets que le toca (y en el layout plano de Unix comprueba la
    # bitness explicitamente, ver la plantilla).
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/vppConfigVersion.cmake"
        VERSION          ${PROJECT_VERSION}
        COMPATIBILITY    SameMajorVersion
        ARCH_INDEPENDENT
    )

    if(VPP_INSTALL_COMMON)
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/vppConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/vppConfigVersion.cmake"
            DESTINATION ${VPP_INSTALL_CMAKEDIR}
            COMPONENT   headers
        )
    endif()

    # --- pkg-config: consumidores que no usan CMake -------------------------
    # Se apunta a la compartida cuando existe; si no, a la estatica, que
    # ademas arrastra la runtime de C++ como dependencia privada.
    if(VPP_BUILD_SHARED)
        set(VPP_PC_LIBNAME       "vpp")
        set(VPP_PC_LIBS_PRIVATE  "")
    elseif(MSVC)
        set(VPP_PC_LIBNAME       "vpp_static")
        set(VPP_PC_LIBS_PRIVATE  "")
    else()
        set(VPP_PC_LIBNAME       "vpp")
        set(VPP_PC_LIBS_PRIVATE  "-lstdc++")
    endif()

    # Rutas absolutas para el .pc: GNUInstallDirs puede devolverlas relativas.
    # OJO: el libdir del .pc es el de ESTA ABI (lib/x64-msvc, ...), no el lib/
    # generico -- si no, un consumidor por pkg-config enlazaria la biblioteca
    # de otro compilador y el fallo saldria en el enlazador, no aqui.
    if(IS_ABSOLUTE "${VPP_LIBDIR}")
        set(PKGCONFIG_LIBDIR "${VPP_LIBDIR}")
    else()
        set(PKGCONFIG_LIBDIR "\${prefix}/${VPP_LIBDIR}")
    endif()
    if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
        set(PKGCONFIG_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
    else()
        set(PKGCONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
    endif()

    # Prefijo relocalizable: se sube desde el directorio del propio .pc tantos
    # niveles como tenga su ruta de instalacion.  Son dos en Unix
    # (lib/pkgconfig) y tres en el layout multi-ABI de Windows
    # (lib/x64-mingw/pkgconfig), de ahi que se calcule en vez de fijarlo.
    set(_vpp_pcdir "${VPP_LIBDIR}/pkgconfig")
    if(IS_ABSOLUTE "${_vpp_pcdir}")
        # Con un libdir absoluto no hay prefijo relativo posible; se incrusta.
        set(PKGCONFIG_PREFIX "${CMAKE_INSTALL_PREFIX}")
    else()
        string(REGEX REPLACE "[^/]+" ".." _vpp_pcup "${_vpp_pcdir}")
        set(PKGCONFIG_PREFIX "\${pcfiledir}/${_vpp_pcup}")
    endif()

    if(IS_ABSOLUTE "${VPP_INSTALL_DATADIR}")
        set(PKGCONFIG_DATADIR "${VPP_INSTALL_DATADIR}")
    else()
        set(PKGCONFIG_DATADIR "\${prefix}/${VPP_INSTALL_DATADIR}")
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vpp.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vpp.pc"
        @ONLY
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vpp.pc"
            DESTINATION "${VPP_LIBDIR}/pkgconfig"
            COMPONENT   ${VPP_LIBS_COMPONENT})

    # --- documentacion y licencia -------------------------------------------
    if(VPP_INSTALL_COMMON)
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            DESTINATION ${VPP_INSTALL_DATADIR}
            COMPONENT   docs
        )
    endif()

    # --- empaquetado: instalador NSIS + zip/tgz portable --------------------
    # La configuracion de CPack vive en su propio modulo para no mezclar "que
    # se instala" (aqui) con "como se presenta al usuario" (alli).
    if(VPP_IS_TOP_LEVEL)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VppPackaging.cmake")
    endif()
endif()

# --- resumen de configuracion ----------------------------------------------

message(STATUS "vpp ${PROJECT_VERSION} configurado:")
message(STATUS "  Compilador    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Estandar C++  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Proyecto raiz : ${VPP_IS_TOP_LEVEL}")
message(STATUS "  ABI           : ${VPP_ABI_TAG}")
message(STATUS "  Estatica (.a) : ${VPP_BUILD_STATIC}")
message(STATUS "  Compartida    : ${VPP_BUILD_SHARED}")
message(STATUS "  Ejecutable    : ${VPP_BUILD_EXE}")
message(STATUS "  Tests         : ${VPP_BUILD_TESTS}")
message(STATUS "  Instalacion   : ${VPP_INSTALL}")
