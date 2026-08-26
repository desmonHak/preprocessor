# VppCPackProjectConfig.cmake
#
# CPACK_PROJECT_CONFIG_FILE: CPack ejecuta este fichero UNA VEZ POR GENERADOR,
# con ${CPACK_GENERATOR} ya fijado.  Es el modo soportado de dar valores
# distintos segun el formato de salida (un -D en la linea de cpack lo pisaria
# el CPackConfig.cmake generado).

# La carpeta contenedora (vpp-<ver>-<plataforma>/) tiene sentidos opuestos
# segun el formato:
#
#   - ZIP / TGZ: SI se quiere.  Al extraer, todo cae dentro de su carpeta en
#     lugar de volcarse suelto en el directorio actual.
#   - NSIS: NO se quiere.  El instalador ya pregunta el directorio destino; con
#     la carpeta contenedora activada instalaria en
#     `...\vpp\vpp-1.0.0-win64\bin\vpp.exe` en vez de `...\vpp\bin\vpp.exe`,
#     y el PATH que anade el instalador apuntaria al sitio equivocado.
if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 0)
endif()

# DEB y RPM: un paquete por GRUPO, no uno por componente ni todo en uno.
# Es la convencion de las distribuciones -- vpp y vpp-dev -- y sin esto CPack
# generaria un solo paquete con todo dentro, que no permite instalar la
# biblioteca sin arrastrar las cabeceras.
if(CPACK_GENERATOR STREQUAL "DEB" OR CPACK_GENERATOR STREQUAL "RPM")
    set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
    # Aqui la carpeta contenedora no aplica: los paquetes nativos instalan en
    # rutas absolutas del sistema.
    set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 0)
endif()
