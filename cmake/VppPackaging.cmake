# VppPackaging.cmake
#
# Configuracion de CPack para vpp: instalador grafico en Windows (NSIS .exe) y
# paquetes portables (ZIP / TGZ) en cualquier plataforma.
#
# Las reglas install() NO viven aqui sino en el CMakeLists principal: son la
# unica fuente de verdad de "que ficheros forman una instalacion", y el
# instalador empaqueta exactamente eso.  Este fichero solo decide COMO se
# presenta ese contenido.
#
# Generar con un solo comando (NSIS se AUTO-DESCARGA si falta -- no hay que
# instalar nada a mano):
#
#   cmake --build <build> --target installer       # -> vpp-<ver>-win64.exe
#   cmake --build <build> --target installer-zip   # -> vpp-<ver>-*.zip / .tar.gz
#
# Alternativa manual, si ya tienes la herramienta:
#   cpack --config <build>/CPackConfig.cmake -G NSIS
#   cpack --config <build>/CPackConfig.cmake -G ZIP

# --- metadatos del paquete --------------------------------------------------

set(CPACK_PACKAGE_NAME           "vpp")
set(CPACK_PACKAGE_VENDOR         "David Lopez T. (DesmonHak)")
set(CPACK_PACKAGE_VERSION        "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR  "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR  "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH  "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Preprocesador C/C++ compatible de Vesta, embebible via ABI en C")
set(CPACK_PACKAGE_HOMEPAGE_URL   "https://github.com/vesta-lang")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "vpp")

# Nombre del artefacto: win64 en Windows, <sistema>-<arch> en el resto.
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "vpp-${PROJECT_VERSION}-win64")
else()
    set(CPACK_PACKAGE_FILE_NAME
        "vpp-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()

# El asistente de NSIS espera un .txt para la licencia; el repo mantiene un
# LICENSE.md.  Se genera una copia en el build dir (no versionada).
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
               "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README  "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# --- componentes ------------------------------------------------------------

# Instalacion por componentes: el asistente deja marcar/desmarcar cada uno.
#
# Los comunes (runtime, headers, docs) los aporta ESTE build.  Las bibliotecas
# van en un componente POR ABI (libs_x64_mingw, libs_x86_msvc, ...) porque una
# estatica de MSVC y una de MinGW no son intercambiables: el usuario marca la
# que corresponda a su compilador.
set(CPACK_COMPONENTS_ALL runtime headers docs)

set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Preprocesador (vpp)")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
    "El ejecutable vpp y la stdlib de macros.  Obligatorio.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)

set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "Cabeceras y paquete CMake")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION
    "Cabeceras (ABI en C y API C++) y el vppConfig.cmake que elige la ABI segun tu compilador.  Comunes a todas las ABI; necesarias para compilar contra vpp.")

set(CPACK_COMPONENT_DOCS_DISPLAY_NAME "Documentacion")
set(CPACK_COMPONENT_DOCS_DESCRIPTION "README y licencia.")

# ---------------------------------------------------------------------------
# Bibliotecas por ABI
# ---------------------------------------------------------------------------
# La ABI de ESTE build siempre entra.  Las demas se enganchan pasando el
# directorio donde ya se instalaron, con el formato
#
#   -DVPP_EXTRA_ABI_STAGES="x86-mingw=C:/s/x86-mingw;x64-msvc=C:/s/x64-msvc"
#
# cmake/build_all_abis.bat automatiza compilarlas e instalarlas todas.

# @brief Declara el componente que agrupa las bibliotecas de una ABI.
# @param tag  Etiqueta de la ABI (x64-mingw en Windows, x64 a secas en Unix).
# @param comp Nombre REAL del componente con el que se etiquetaron las reglas
#             install().  Se pasa en vez de derivarlo del tag porque en Unix es
#             "development" y no "libs_x64": si los dos nombres no coinciden,
#             CPack no encuentra los ficheros y el paquete sale sin biblioteca
#             -- sin dar ningun error.
function(vpp_declare_abi_component tag comp)
    string(TOUPPER "${comp}" _COMP)

    # Nombre legible.  El tag lleva compilador solo en Windows, donde conviven
    # varias ABI y hay que distinguirlas; en Unix hay una sola.
    if("${tag}" MATCHES "-")
        string(REPLACE "-" ";" _parts "${tag}")
        list(GET _parts 0 _arch)
        list(GET _parts 1 _cc)
        if(_cc STREQUAL "msvc")
            set(_ccname "MSVC")
        else()
            set(_ccname "MinGW")
        endif()
        set(_disp "Librerias ${_ccname} ${_arch}")
        set(_desc "Biblioteca estatica, dinamica e import library para ${_disp}, mas su pkg-config.  Marca la que corresponda a tu compilador.")
    else()
        set(_disp "Ficheros de desarrollo")
        set(_desc "Biblioteca estatica y dinamica, pkg-config y ficheros de CMake.  Necesario para enlazar vpp desde tu propio programa.")
    endif()

    set(CPACK_COMPONENT_${_COMP}_DISPLAY_NAME "${_disp}" PARENT_SCOPE)
    set(CPACK_COMPONENT_${_COMP}_DESCRIPTION  "${_desc}" PARENT_SCOPE)
    set(CPACK_COMPONENT_${_COMP}_DEPENDS      headers    PARENT_SCOPE)
    set(_vpp_new_component "${comp}" PARENT_SCOPE)
endfunction()

# ABI de este build: sus reglas install() ya estan declaradas en el CMakeLists.
vpp_declare_abi_component("${VPP_ABI_TAG}" "${VPP_LIBS_COMPONENT}")
list(APPEND CPACK_COMPONENTS_ALL "${_vpp_new_component}")

# ABIs adicionales, cada una YA INSTALADA en su propio directorio de staging.
#
# Se toma el arbol instalado y no el directorio de build a proposito.  La via
# "natural" (CPACK_INSTALL_CMAKE_PROJECTS apuntando a los otros builds) obliga
# a CPack a lanzar `cmake --build --target preinstall` en cada uno, y eso exige
# tener a mano su herramienta de construccion: para los arboles de MSVC, nmake
# y el entorno de vcvars, que no estan en el shell desde el que se empaqueta.
# Peor aun, forzarlo corriendo cpack dentro de un vcvars concreto arriesga que
# el arbol de x86 se recompile con el compilador de x64 si algo quedase por
# reconstruir -- un binario incorrecto, y ademas en silencio.
#
# Copiar el arbol ya instalado no necesita ninguna herramienta y no puede
# recompilar nada.  Cada ABI instala en su propio lib/<tag>/, de modo que los
# arboles se superponen sin pisarse.
foreach(_entry IN LISTS VPP_EXTRA_ABI_STAGES)
    if(NOT _entry MATCHES "^([^=]+)=(.+)$")
        message(FATAL_ERROR
            "VPP_EXTRA_ABI_STAGES: entrada invalida '${_entry}'.  "
            "Formato esperado: <tag>=<directorio de staging>")
    endif()
    set(_tag "${CMAKE_MATCH_1}")
    # A barras normales: estas rutas acaban escritas en ficheros que CMake
    # vuelve a parsear, y un "C:\Users\..." seria el escape invalido \U.
    file(TO_CMAKE_PATH "${CMAKE_MATCH_2}" _sdir)
    if(NOT IS_DIRECTORY "${_sdir}")
        message(FATAL_ERROR
            "VPP_EXTRA_ABI_STAGES: '${_sdir}' no existe o no es un directorio.")
    endif()

    string(REPLACE "-" "_" _tag_sfx "${_tag}")
    vpp_declare_abi_component("${_tag}" "libs_${_tag_sfx}")
    list(APPEND CPACK_COMPONENTS_ALL "${_vpp_new_component}")
    # La barra final del origen significa "el contenido de", para que el arbol
    # caiga en la raiz del paquete y no dentro de otro nivel.
    install(DIRECTORY "${_sdir}/"
            DESTINATION "."
            COMPONENT   "${_vpp_new_component}")
    message(STATUS "  ABI adicional : ${_tag} <- ${_sdir}")
endforeach()

# Un solo artefacto con todos los componentes dentro, no uno por componente.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

# Carpeta contenedora: buena para el ZIP, mala para el NSIS.  El override
# por-generador vive en el CPACK_PROJECT_CONFIG_FILE de abajo.
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 1)
set(CPACK_PROJECT_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VppCPackProjectConfig.cmake")

# --- NSIS (Windows) ---------------------------------------------------------

set(CPACK_NSIS_DISPLAY_NAME  "vpp ${PROJECT_VERSION}")
set(CPACK_NSIS_PACKAGE_NAME  "vpp ${PROJECT_VERSION}")
set(CPACK_NSIS_INSTALL_ROOT  "$PROGRAMFILES64")
set(CPACK_NSIS_BRANDING_TEXT "vpp ${PROJECT_VERSION} -- preprocesador de Vesta")
# Anade <prefix>\bin al PATH (con pagina de eleccion) -> `vpp` desde cualquier
# shell.  Es lo unico que hace falta para una herramienta de linea de comandos.
set(CPACK_NSIS_MODIFY_PATH   ON)
# Cuadruple barra a proposito: CPack ESCRIBE este valor a CPackConfig.cmake y
# CMake lo vuelve a parsear.  Con cuatro barras aqui, el fichero generado lleva
# dos, que al releerse dan la barra unica que NSIS espera.  Con dos barras aqui,
# la segunda pasada ve \v -> "Invalid character escape" y cpack aborta.
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\vpp.exe")
# NO auto-desinstalar la version previa: el .onInit de CPack ejecuta el
# Uninstall.exe registrado, y si el usuario borro la carpeta a mano ese .exe ya
# no existe -> "Uninstall failed" + Abort, con la instalacion bloqueada.
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)

# Icono, si el repo lo trae.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icono.ico")
    set(CPACK_NSIS_MUI_ICON    "${CMAKE_CURRENT_SOURCE_DIR}/icono.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/icono.ico")
endif()

# --- paquetes nativos de Linux (DEB / RPM) ----------------------------------

if(UNIX AND NOT APPLE)
    # Las distribuciones parten una biblioteca en dos paquetes: el binario que
    # necesita quien solo EJECUTA, y las cabeceras y los .a/.so-enlazables que
    # necesita quien COMPILA contra ella.  Se agrupan los componentes para
    # reproducir esa convencion en lugar de soltar un .deb por componente.
    set(CPACK_COMPONENT_RUNTIME_GROUP     runtime)
    set(CPACK_COMPONENT_DOCS_GROUP        runtime)
    set(CPACK_COMPONENT_HEADERS_GROUP     dev)
    set(CPACK_COMPONENT_DEVELOPMENT_GROUP dev)

    set(CPACK_COMPONENT_GROUP_RUNTIME_DISPLAY_NAME "vpp")
    set(CPACK_COMPONENT_GROUP_DEV_DISPLAY_NAME     "vpp-dev")

    # Sin esto, los generadores DEB y RPM IGNORAN los componentes por completo
    # y sueltan un unico paquete monolitico, por muchos grupos que se declaren.
    set(CPACK_DEB_COMPONENT_INSTALL ON)
    set(CPACK_RPM_COMPONENT_INSTALL ON)

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
        "David Lopez T. (DesmonHak) <anonimus.hak1.1@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION  "devel")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # DEB-DEFAULT da el nombre canonico: vpp_1.0.0_amd64.deb.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    # Nombres de paquete por grupo.
    set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "vpp")
    set(CPACK_DEBIAN_DEV_PACKAGE_NAME     "vpp-dev")
    # El paquete de desarrollo no sirve de nada sin la biblioteca en tiempo de
    # ejecucion, y ademas debe ser de la MISMA version: si se mezclaran, las
    # cabeceras describirian una biblioteca distinta de la instalada.
    set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "vpp (= ${PROJECT_VERSION})")

    # dpkg-shlibdeps deduce las dependencias reales (libc, libstdc++) leyendo
    # los binarios, en vez de fiarlo a una lista escrita a mano que envejece.
    find_program(VPP_DPKG_SHLIBDEPS dpkg-shlibdeps)
    if(VPP_DPKG_SHLIBDEPS)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()

    # RPM-DEFAULT deja que rpmbuild aplique su nomenclatura canonica
    # (vpp-1.0.0-1.x86_64.rpm).  Sin esto CPack usa CPACK_PACKAGE_FILE_NAME con
    # el grupo pegado detras y sale un "vpp-1.0.0-Linux-x86_64-dev.rpm" que no
    # respeta el formato <nombre>-<version>-<release>.<arquitectura>.rpm.
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP   "Development/Tools")
    set(CPACK_RPM_RUNTIME_PACKAGE_NAME "vpp")
    set(CPACK_RPM_DEV_PACKAGE_NAME     "vpp-devel")
    set(CPACK_RPM_DEV_PACKAGE_REQUIRES "vpp = ${PROJECT_VERSION}")
endif()

# --- generador por defecto --------------------------------------------------

if(NOT CPACK_GENERATOR)
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
endif()

include(CPack)

# --- targets de conveniencia -------------------------------------------------

if(WIN32)
    # .exe con asistente.  El script auto-descarga NSIS si makensis no esta.
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeInstallerNSIS.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
        COMMENT "Generando instalador .exe (NSIS se descarga solo si no esta)")

    add_custom_target(installer-zip
        COMMAND ${CMAKE_CPACK_COMMAND} -G ZIP
                --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
        COMMENT "Generando paquete .zip portable")
else()
    add_custom_target(installer-zip
        COMMAND ${CMAKE_CPACK_COMMAND} -G TGZ
                --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
        COMMENT "Generando paquete .tar.gz portable")

    # Paquetes nativos de la distribucion: vpp y vpp-dev.
    add_custom_target(installer-deb
        COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
                --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
        COMMENT "Generando paquetes .deb (vpp + vpp-dev)")

    add_custom_target(installer-rpm
        COMMAND ${CMAKE_CPACK_COMMAND} -G RPM
                --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
        COMMENT "Generando paquetes .rpm (requiere rpmbuild)")
endif()

# Los paquetes deben empaquetar binarios frescos, no los de la build anterior.
set(_vpp_pkg_targets "")
foreach(_t vpp_lib vpp_shared vpp)
    if(TARGET ${_t})
        list(APPEND _vpp_pkg_targets ${_t})
    endif()
endforeach()
if(_vpp_pkg_targets)
    if(TARGET installer)
        add_dependencies(installer ${_vpp_pkg_targets})
    endif()
    add_dependencies(installer-zip ${_vpp_pkg_targets})
    foreach(_t installer-deb installer-rpm)
        if(TARGET ${_t})
            add_dependencies(${_t} ${_vpp_pkg_targets})
        endif()
    endforeach()
    if(TARGET package)
        add_dependencies(package ${_vpp_pkg_targets})
    endif()
endif()
