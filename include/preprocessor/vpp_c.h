/**
 * @file vpp_c.h
 * @brief ABI en C puro del preprocesador vpp -- interfaz estable para la DLL/.so.
 *
 * La API C++ (preprocessor/preprocessor.h) usa std::string, std::vector y
 * std::function en su firma.  Eso vale para enlazar el .a con EL MISMO
 * compilador, pero no cruza la frontera de una biblioteca compartida: el
 * layout de std::string difiere entre MSVC, MinGW y versiones distintas de
 * libstdc++, y las excepciones no se propagan de forma fiable entre modulos.
 *
 * Este header es la superficie que exporta la biblioteca compartida: handles
 * opacos, const char*, codigos de error y cero excepciones.  Es consumible
 * desde C, y por tanto desde Python/ctypes, Rust, C#, Go, Zig o cualquier
 * lenguaje con FFI a C.
 *
 * Reglas de propiedad de memoria (importantes):
 *   - Lo que devuelve vpp_process / vpp_process_file en out_result es
 *     PROPIEDAD DEL LLAMANTE y se libera con vpp_string_free.
 *   - Todo const char* devuelto por los demas getters es PRESTADO: apunta a
 *     memoria del handle y deja de ser valido tras la siguiente llamada a
 *     vpp_process* o tras vpp_destroy.  Copialo si lo necesitas despues.
 *
 * Un handle NO es thread-safe.  Usa un handle por hilo.
 */
#ifndef VPP_C_H
#define VPP_C_H

#include <stddef.h>

/* --- decoracion de simbolos exportados ------------------------------------ */

/*
 * VPP_STATIC        lo define el target estatico (PUBLIC): sin decoracion.
 * VPP_BUILD_SHARED  lo define el target compartido al COMPILARSE (PRIVATE):
 *                   exporta.  Un consumidor de la DLL no lo define, con lo
 *                   que cae en la rama de importacion.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(VPP_STATIC)
#    define VPP_API
#  elif defined(VPP_BUILD_SHARED)
#    define VPP_API __declspec(dllexport)
#  else
#    define VPP_API __declspec(dllimport)
#  endif
#else
#  if defined(VPP_BUILD_SHARED) && defined(__GNUC__)
#    define VPP_API __attribute__((visibility("default")))
#  else
#    define VPP_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- tipos ----------------------------------------------------------------- */

/**
 * @brief Handle opaco de una instancia del preprocesador.
 *
 * Su layout no forma parte del ABI: solo se manipula por puntero a traves de
 * las funciones de este header.
 */
typedef struct vpp_preprocessor_s vpp_preprocessor;

/**
 * @brief Codigo de resultado de las operaciones de la API.
 */
typedef enum vpp_status_e {
    VPP_OK              = 0, /**< Operacion completada sin errores. */
    VPP_ERR_INVALID_ARG = 1, /**< Argumento nulo o fuera de rango. */
    VPP_ERR_IO          = 2, /**< No se pudo leer el fichero de entrada. */
    VPP_ERR_DIAGNOSTIC  = 3, /**< Se proceso, pero hubo errores de preprocesado. */
    VPP_ERR_INTERNAL    = 4, /**< Excepcion C++ atrapada en la frontera. */
    VPP_ERR_OOM         = 5  /**< Sin memoria. */
} vpp_status;

/**
 * @brief Severidad de un diagnostico.  Espeja vpp::DiagLevel.
 */
typedef enum vpp_diag_level_e {
    VPP_DIAG_NOTE    = 0, /**< Nota informativa. */
    VPP_DIAG_WARNING = 1, /**< Advertencia; no detiene el proceso. */
    VPP_DIAG_ERROR   = 2, /**< Error; el proceso continua para reportar mas. */
    VPP_DIAG_FATAL   = 3  /**< Error fatal; detiene el procesamiento. */
} vpp_diag_level;

/**
 * @brief Un diagnostico emitido durante el preprocesado.
 *
 * Los punteros file y message son PRESTADOS (ver reglas de propiedad en la
 * cabecera de este fichero).
 */
typedef struct vpp_diagnostic_s {
    vpp_diag_level level;   /**< Severidad. */
    const char*    file;    /**< Fichero fuente; nunca NULL (puede ser ""). */
    unsigned int   line;    /**< Linea, base 1. */
    unsigned int   col;     /**< Columna, base 1. */
    const char*    message; /**< Texto del mensaje; nunca NULL. */
} vpp_diagnostic;

/**
 * @brief Resolvedor de inclusiones definido por el usuario.
 *
 * Permite servir los #include desde donde sea (memoria, un zip, la red) en
 * lugar del sistema de ficheros.
 *
 * @param user_data      Puntero opaco pasado en vpp_set_include_resolver.
 * @param from_file      Fichero que realiza la inclusion.
 * @param requested_path Ruta tal como aparece en la directiva.
 * @param is_system      1 para #include <...>, 0 para #include "...".
 * @return Contenido del fichero, o NULL si no se encuentra.  El puntero se
 *         copia ANTES de que la llamada retorne al preprocesador, asi que
 *         puede reutilizarse o liberarse en la siguiente invocacion.
 */
typedef const char* (*vpp_include_resolver_fn)(void* user_data,
                                               const char* from_file,
                                               const char* requested_path,
                                               int is_system);

/* --- version --------------------------------------------------------------- */

/**
 * @brief Version de la biblioteca en componentes numericos.
 * @param major Recibe el major, o NULL si no interesa.
 * @param minor Recibe el minor, o NULL si no interesa.
 * @param patch Recibe el patch, o NULL si no interesa.
 */
VPP_API void vpp_version(int* major, int* minor, int* patch);

/**
 * @brief Version de la biblioteca como cadena "MAJOR.MINOR.PATCH".
 * @return Literal estatico; no se libera.
 */
VPP_API const char* vpp_version_string(void);

/**
 * @brief Nombre legible de un codigo de estado.
 * @param st Codigo de estado.
 * @return Literal estatico; no se libera.
 */
VPP_API const char* vpp_status_string(vpp_status st);

/* --- ciclo de vida --------------------------------------------------------- */

/**
 * @brief Crea una instancia del preprocesador con las macros de plataforma ya
 *        registradas.
 * @return Handle nuevo, o NULL si no hay memoria.  Liberalo con vpp_destroy.
 */
VPP_API vpp_preprocessor* vpp_create(void);

/**
 * @brief Destruye una instancia y libera todos sus recursos.
 * @param pp Handle a destruir.  NULL es un no-op valido.
 */
VPP_API void vpp_destroy(vpp_preprocessor* pp);

/* --- configuracion (antes de procesar) ------------------------------------- */

/**
 * @brief Define una macro, equivalente a -DNAME o -DNAME=valor.
 * @param pp  Handle.
 * @param def Cadena "NAME" o "NAME=valor".
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_add_define(vpp_preprocessor* pp, const char* def);

/**
 * @brief Agrega una ruta de busqueda para #include <...> (equivale a -I).
 * @param pp   Handle.
 * @param path Ruta del directorio.
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_add_include_path(vpp_preprocessor* pp, const char* path);

/**
 * @brief Agrega una ruta de busqueda para #import <...> (equivale a -M).
 * @param pp   Handle.
 * @param path Ruta del directorio de la libreria de macros.
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_add_import_path(vpp_preprocessor* pp, const char* path);

/**
 * @brief Activa o desactiva la expansion de macros en el texto plano.
 * @param pp     Handle.
 * @param enable 1 para expandir (por defecto), 0 para dejar el texto intacto.
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_set_expand_macros(vpp_preprocessor* pp, int enable);

/**
 * @brief Activa o desactiva la emision de marcadores #line tras cada include.
 * @param pp     Handle.
 * @param enable 1 para emitirlos, 0 para omitirlos (por defecto).
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_set_line_markers(vpp_preprocessor* pp, int enable);

/**
 * @brief Activa o desactiva la deteccion de inclusion circular.
 * @param pp     Handle.
 * @param enable 1 para detectarla (por defecto), 0 para desactivarla.
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_set_track_includes(vpp_preprocessor* pp, int enable);

/**
 * @brief Instala un resolvedor de inclusiones definido por el usuario.
 * @param pp        Handle.
 * @param fn        Funcion resolutora, o NULL para volver al sistema de ficheros.
 * @param user_data Puntero opaco que se pasa tal cual a cada invocacion.
 * @return VPP_OK o VPP_ERR_INVALID_ARG.
 */
VPP_API vpp_status vpp_set_include_resolver(vpp_preprocessor* pp,
                                            vpp_include_resolver_fn fn,
                                            void* user_data);

/* --- procesado ------------------------------------------------------------- */

/**
 * @brief Preprocesa un fuente en memoria.
 *
 * Un handle acumula diagnosticos y macros entre llamadas: para unidades de
 * traduccion independientes, usa un handle por unidad.
 *
 * @param pp         Handle.
 * @param source     Texto fuente terminado en NUL.
 * @param filename   Nombre para mensajes y __FILE__; NULL usa "<stdin>".
 * @param out_result Recibe el texto preprocesado, propiedad del llamante, a
 *                   liberar con vpp_string_free.  Se rellena tambien cuando
 *                   se devuelve VPP_ERR_DIAGNOSTIC.
 * @return VPP_OK si no hubo errores; VPP_ERR_DIAGNOSTIC si los hubo (consulta
 *         vpp_diagnostic_at); VPP_ERR_INVALID_ARG / VPP_ERR_INTERNAL /
 *         VPP_ERR_OOM segun el caso.
 */
VPP_API vpp_status vpp_process(vpp_preprocessor* pp,
                               const char* source,
                               const char* filename,
                               char** out_result);

/**
 * @brief Preprocesa un fichero leyendolo del disco.
 * @param pp         Handle.
 * @param filepath   Ruta del fichero de entrada.
 * @param out_result Igual que en vpp_process.
 * @return VPP_ERR_IO si el fichero no se puede abrir; resto igual que
 *         vpp_process.
 */
VPP_API vpp_status vpp_process_file(vpp_preprocessor* pp,
                                    const char* filepath,
                                    char** out_result);

/**
 * @brief Libera una cadena devuelta por vpp_process / vpp_process_file.
 *
 * Debe usarse SIEMPRE esta funcion en lugar de free: la biblioteca puede
 * estar enlazada a un heap distinto al del programa que la consume.
 *
 * @param s Cadena a liberar.  NULL es un no-op valido.
 */
VPP_API void vpp_string_free(char* s);

/* --- diagnosticos ---------------------------------------------------------- */

/**
 * @brief Numero de errores emitidos.
 * @param pp Handle.
 * @return Conteo de errores, o 0 si pp es NULL.
 */
VPP_API unsigned int vpp_error_count(const vpp_preprocessor* pp);

/**
 * @brief Numero de advertencias emitidas.
 * @param pp Handle.
 * @return Conteo de advertencias, o 0 si pp es NULL.
 */
VPP_API unsigned int vpp_warning_count(const vpp_preprocessor* pp);

/**
 * @brief Numero total de diagnosticos acumulados, de cualquier severidad.
 * @param pp Handle.
 * @return Conteo, o 0 si pp es NULL.
 */
VPP_API size_t vpp_diagnostic_count(const vpp_preprocessor* pp);

/**
 * @brief Lee el diagnostico en la posicion dada.
 * @param pp    Handle.
 * @param index Indice en [0, vpp_diagnostic_count).
 * @param out   Estructura a rellenar; sus punteros son prestados.
 * @return VPP_OK, o VPP_ERR_INVALID_ARG si el indice esta fuera de rango.
 */
VPP_API vpp_status vpp_diagnostic_at(const vpp_preprocessor* pp,
                                     size_t index,
                                     vpp_diagnostic* out);

/* --- dependencias (para caches de compilacion) ----------------------------- */

/**
 * @brief Numero de ficheros que el fuente incluyo, sin repetir.
 *
 * Quien cachea el resultado necesita saber de que depende: si un fichero
 * incluido cambia, el artefacto cacheado deja de valer.
 *
 * @param pp Handle.
 * @return Conteo, o 0 si pp es NULL.
 */
VPP_API size_t vpp_included_file_count(const vpp_preprocessor* pp);

/**
 * @brief Ruta del fichero incluido en la posicion dada.
 * @param pp    Handle.
 * @param index Indice en [0, vpp_included_file_count).
 * @return Ruta prestada, o NULL si el indice esta fuera de rango.
 */
VPP_API const char* vpp_included_file_at(const vpp_preprocessor* pp,
                                         size_t index);

/* --- inspeccion de macros -------------------------------------------------- */

/**
 * @brief Indica si una macro esta definida.
 * @param pp   Handle.
 * @param name Nombre de la macro.
 * @return 1 si esta definida, 0 en caso contrario.
 */
VPP_API int vpp_is_defined(const vpp_preprocessor* pp, const char* name);

/**
 * @brief Numero de macros en la tabla, incluidas las predefinidas.
 * @param pp Handle.
 * @return Conteo, o 0 si pp es NULL.
 */
VPP_API size_t vpp_macro_count(const vpp_preprocessor* pp);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VPP_C_H */
