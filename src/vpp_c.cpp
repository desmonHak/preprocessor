/**
 * @file vpp_c.cpp
 * @brief Implementacion del ABI en C del preprocesador vpp.
 *
 * Cada funcion exportada es una frontera: traduce entre los tipos C++ del
 * nucleo y los tipos C del header, y atrapa TODA excepcion.  Una excepcion que
 * escape de aqui cruzaria el limite de un modulo compartido -- comportamiento
 * indefinido en la practica cuando el consumidor no es C++ o usa otro runtime.
 * Por eso ninguna de estas funciones puede lanzar: lo que seria un throw se
 * convierte en un vpp_status.
 */

#include "preprocessor/vpp_c.h"
#include "preprocessor/preprocessor.h"

#include <cstring>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

/* --- estructura interna del handle ---------------------------------------- */

/**
 * @brief Estado real detras del handle opaco vpp_preprocessor.
 *
 * Guarda ademas las cadenas prestadas que la API devuelve como const char*:
 * los std::string del nucleo son estables mientras viva el handle, asi que se
 * devuelve su c_str() directamente y no hace falta duplicarlos.  Lo unico que
 * se materializa aqui es el user_data del resolvedor.
 */
struct vpp_preprocessor_s {
    vpp::Preprocessor       pp;            ///< Nucleo C++ del preprocesador
    vpp_include_resolver_fn resolver_fn;   ///< Resolvedor C instalado, o nullptr
    void*                   resolver_user; ///< Puntero opaco del usuario

    /** @brief Constructor: sin resolvedor instalado por defecto. */
    vpp_preprocessor_s()
        : resolver_fn(nullptr), resolver_user(nullptr) {}
};

namespace {

/**
 * @brief Duplica un std::string a un buffer C que el llamante debe liberar.
 *
 * Se usa malloc y no new[] porque vpp_string_free hace free: mantener el par
 * malloc/free en el MISMO modulo es lo que permite que el consumidor libere
 * sin conocer el heap de la biblioteca.
 *
 * @param s Cadena a duplicar.
 * @return Buffer terminado en NUL, o nullptr si no hay memoria.
 */
char* dup_to_c(const std::string& s) {
    char* buf = static_cast<char*>(std::malloc(s.size() + 1));
    if (!buf) return nullptr;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    return buf;
}

/**
 * @brief Traduce el nivel de diagnostico C++ al enum del ABI C.
 * @param lv Nivel interno.
 * @return Nivel equivalente del ABI.
 */
vpp_diag_level to_c_level(vpp::DiagLevel lv) {
    switch (lv) {
        case vpp::DiagLevel::NOTE:    return VPP_DIAG_NOTE;
        case vpp::DiagLevel::WARNING: return VPP_DIAG_WARNING;
        case vpp::DiagLevel::ERR:     return VPP_DIAG_ERROR;
        case vpp::DiagLevel::FATAL:   return VPP_DIAG_FATAL;
    }
    return VPP_DIAG_ERROR;
}

} // namespace

/* --- version --------------------------------------------------------------- */

extern "C" VPP_API void vpp_version(int* major, int* minor, int* patch) {
    if (major) *major = VPP_VERSION_MAJOR;
    if (minor) *minor = VPP_VERSION_MINOR;
    if (patch) *patch = VPP_VERSION_PATCH;
}

extern "C" VPP_API const char* vpp_version_string(void) {
    return VPP_VERSION_STRING;
}

extern "C" VPP_API const char* vpp_status_string(vpp_status st) {
    switch (st) {
        case VPP_OK:              return "ok";
        case VPP_ERR_INVALID_ARG: return "argumento invalido";
        case VPP_ERR_IO:          return "error de entrada/salida";
        case VPP_ERR_DIAGNOSTIC:  return "errores de preprocesado";
        case VPP_ERR_INTERNAL:    return "error interno";
        case VPP_ERR_OOM:         return "sin memoria";
    }
    return "estado desconocido";
}

/* --- ciclo de vida --------------------------------------------------------- */

extern "C" VPP_API vpp_preprocessor* vpp_create(void) {
    try {
        return new vpp_preprocessor_s();
    } catch (...) {
        // ni siquiera un bad_alloc puede escapar de la frontera
        return nullptr;
    }
}

extern "C" VPP_API void vpp_destroy(vpp_preprocessor* pp) {
    // delete sobre nullptr ya es un no-op, pero el guard documenta el contrato
    if (!pp) return;
    delete pp;
}

/* --- configuracion --------------------------------------------------------- */

extern "C" VPP_API vpp_status vpp_add_define(vpp_preprocessor* pp,
                                             const char* def) {
    if (!pp || !def) return VPP_ERR_INVALID_ARG;
    try {
        // se acumula en las opciones: process() las aplica al arrancar, que es
        // donde el nucleo espera encontrarlas
        pp->pp.options().predefines.push_back(def);
        return VPP_OK;
    } catch (...) {
        return VPP_ERR_OOM;
    }
}

extern "C" VPP_API vpp_status vpp_add_include_path(vpp_preprocessor* pp,
                                                   const char* path) {
    if (!pp || !path) return VPP_ERR_INVALID_ARG;
    try {
        pp->pp.options().include_paths.push_back(path);
        return VPP_OK;
    } catch (...) {
        return VPP_ERR_OOM;
    }
}

extern "C" VPP_API vpp_status vpp_add_import_path(vpp_preprocessor* pp,
                                                  const char* path) {
    if (!pp || !path) return VPP_ERR_INVALID_ARG;
    try {
        pp->pp.options().import_paths.push_back(path);
        return VPP_OK;
    } catch (...) {
        return VPP_ERR_OOM;
    }
}

extern "C" VPP_API vpp_status vpp_set_expand_macros(vpp_preprocessor* pp,
                                                    int enable) {
    if (!pp) return VPP_ERR_INVALID_ARG;
    pp->pp.options().expand_macros = (enable != 0);
    return VPP_OK;
}

extern "C" VPP_API vpp_status vpp_set_line_markers(vpp_preprocessor* pp,
                                                   int enable) {
    if (!pp) return VPP_ERR_INVALID_ARG;
    pp->pp.options().emit_line_markers = (enable != 0);
    return VPP_OK;
}

extern "C" VPP_API vpp_status vpp_set_track_includes(vpp_preprocessor* pp,
                                                     int enable) {
    if (!pp) return VPP_ERR_INVALID_ARG;
    pp->pp.options().track_includes = (enable != 0);
    return VPP_OK;
}

extern "C" VPP_API vpp_status vpp_set_include_resolver(
        vpp_preprocessor* pp,
        vpp_include_resolver_fn fn,
        void* user_data) {
    if (!pp) return VPP_ERR_INVALID_ARG;
    try {
        pp->resolver_fn   = fn;
        pp->resolver_user = user_data;

        if (!fn) {
            // sin resolvedor: se vuelve al sistema de ficheros del nucleo
            pp->pp.set_include_resolver(nullptr);
            return VPP_OK;
        }

        // el lambda captura el handle, no el par (fn, user_data), para que un
        // cambio posterior de resolvedor surta efecto sin reinstalar nada
        vpp_preprocessor_s* self = pp;
        pp->pp.set_include_resolver(
            [self](const std::string& from_file,
                   const std::string& requested,
                   bool is_system) -> std::string {
                if (!self->resolver_fn) return std::string();
                const char* r = self->resolver_fn(self->resolver_user,
                                                  from_file.c_str(),
                                                  requested.c_str(),
                                                  is_system ? 1 : 0);
                // NULL = no encontrado; el nucleo interpreta la cadena vacia
                // como fallo, que es exactamente el contrato documentado
                if (!r) return std::string();
                return std::string(r);
            });
        return VPP_OK;
    } catch (...) {
        return VPP_ERR_OOM;
    }
}

/* --- procesado ------------------------------------------------------------- */

extern "C" VPP_API vpp_status vpp_process(vpp_preprocessor* pp,
                                          const char* source,
                                          const char* filename,
                                          char** out_result) {
    if (!pp || !source || !out_result) return VPP_ERR_INVALID_ARG;
    *out_result = nullptr;

    try {
        std::string out = pp->pp.process(source,
                                         filename ? filename : "<stdin>");
        char* buf = dup_to_c(out);
        if (!buf) return VPP_ERR_OOM;
        *out_result = buf;

        // el texto se devuelve aunque haya errores: sirve para diagnosticar,
        // y el codigo de estado ya avisa de que no es utilizable tal cual
        return pp->pp.diagnostics().has_errors() ? VPP_ERR_DIAGNOSTIC : VPP_OK;
    } catch (const std::bad_alloc&) {
        return VPP_ERR_OOM;
    } catch (...) {
        // DiagnosticEngine::fatal lanza std::runtime_error; aterriza aqui
        return VPP_ERR_INTERNAL;
    }
}

extern "C" VPP_API vpp_status vpp_process_file(vpp_preprocessor* pp,
                                               const char* filepath,
                                               char** out_result) {
    if (!pp || !filepath || !out_result) return VPP_ERR_INVALID_ARG;
    *out_result = nullptr;

    try {
        // se anota el conteo previo para distinguir "no se pudo abrir" de
        // "se abrio pero el contenido tiene errores": ambos dejan el texto
        // vacio, pero solo el primero es un fallo de E/S
        const bool had_errors_before = pp->pp.diagnostics().has_errors();

        std::string out = pp->pp.process_file(filepath);

        if (out.empty() && !had_errors_before &&
            pp->pp.diagnostics().has_errors()) {
            // process_file emite un error propio cuando el ifstream falla;
            // se distingue comprobando si el fichero es legible
            std::FILE* f = std::fopen(filepath, "rb");
            if (!f) return VPP_ERR_IO;
            std::fclose(f);
        }

        char* buf = dup_to_c(out);
        if (!buf) return VPP_ERR_OOM;
        *out_result = buf;

        return pp->pp.diagnostics().has_errors() ? VPP_ERR_DIAGNOSTIC : VPP_OK;
    } catch (const std::bad_alloc&) {
        return VPP_ERR_OOM;
    } catch (...) {
        return VPP_ERR_INTERNAL;
    }
}

extern "C" VPP_API void vpp_string_free(char* s) {
    // par exacto del malloc de dup_to_c, en el mismo modulo
    std::free(s);
}

/* --- diagnosticos ---------------------------------------------------------- */

extern "C" VPP_API unsigned int vpp_error_count(const vpp_preprocessor* pp) {
    if (!pp) return 0u;
    return pp->pp.diagnostics().error_count();
}

extern "C" VPP_API unsigned int vpp_warning_count(const vpp_preprocessor* pp) {
    if (!pp) return 0u;
    return pp->pp.diagnostics().warning_count();
}

extern "C" VPP_API size_t vpp_diagnostic_count(const vpp_preprocessor* pp) {
    if (!pp) return 0u;
    return pp->pp.diagnostics().diagnostics().size();
}

extern "C" VPP_API vpp_status vpp_diagnostic_at(const vpp_preprocessor* pp,
                                                size_t index,
                                                vpp_diagnostic* out) {
    if (!pp || !out) return VPP_ERR_INVALID_ARG;

    const std::vector<vpp::Diagnostic>& all = pp->pp.diagnostics().diagnostics();
    if (index >= all.size()) return VPP_ERR_INVALID_ARG;

    const vpp::Diagnostic& d = all[index];
    out->level   = to_c_level(d.level);
    // punteros prestados al almacenamiento de los std::string del motor: viven
    // mientras el handle no procese otra unidad ni se destruya
    out->file    = d.loc.file.c_str();
    out->line    = d.loc.line;
    out->col     = d.loc.col;
    out->message = d.message.c_str();
    return VPP_OK;
}

/* --- dependencias ---------------------------------------------------------- */

extern "C" VPP_API size_t vpp_included_file_count(const vpp_preprocessor* pp) {
    if (!pp) return 0u;
    return pp->pp.included_files().size();
}

extern "C" VPP_API const char* vpp_included_file_at(const vpp_preprocessor* pp,
                                                    size_t index) {
    if (!pp) return nullptr;
    const std::vector<std::string>& files = pp->pp.included_files();
    if (index >= files.size()) return nullptr;
    return files[index].c_str();
}

/* --- inspeccion de macros -------------------------------------------------- */

extern "C" VPP_API int vpp_is_defined(const vpp_preprocessor* pp,
                                      const char* name) {
    if (!pp || !name) return 0;
    try {
        return pp->pp.macro_table().is_defined(name) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" VPP_API size_t vpp_macro_count(const vpp_preprocessor* pp) {
    if (!pp) return 0u;
    return pp->pp.macro_table().all().size();
}
