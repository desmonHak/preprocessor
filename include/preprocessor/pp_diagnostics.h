/**
 * @file pp_diagnostics.h
 * @brief Sistema de diagnosticos (errores, advertencias, notas) del preprocesador.
 */
#pragma once

#include "pp_token.h"
#include <string>
#include <vector>
#include <functional>
#include <ostream>

namespace vpp {

/**
 * @brief Nivel de severidad de un diagnostico.
 */
enum class DiagLevel : uint8_t {
    NOTE    = 0,    ///< Nota informativa
    WARNING = 1,    ///< Advertencia (no detiene el proceso)
    ERR     = 2,    ///< Error (puede continuar para reportar mas errores)
    FATAL   = 3     ///< Error fatal (detiene el procesamiento inmediatamente)
};

/**
 * @brief Devuelve el nombre del nivel de diagnostico.
 * @param lv Nivel de diagnostico.
 * @return Cadena con el nombre del nivel.
 */
const char* diag_level_name(DiagLevel lv);

/**
 * @brief Un mensaje de diagnostico con ubicacion, nivel y texto.
 */
struct Diagnostic {
    DiagLevel      level;   ///< Severidad del diagnostico
    SourceLocation loc;     ///< Ubicacion en el fuente
    std::string    message; ///< Texto del mensaje

    /**
     * @brief Constructor.
     * @param lv Nivel de severidad.
     * @param l  Ubicacion en fuente.
     * @param msg Texto del mensaje.
     */
    Diagnostic(DiagLevel lv, SourceLocation l, std::string msg)
        : level(lv), loc(std::move(l)), message(std::move(msg)) {}

    /**
     * @brief Formatea el diagnostico como "archivo:linea:col: nivel: mensaje".
     * @return Cadena formateada.
     */
    std::string format() const;
};

/**
 * @brief Tipo de callback invocado cuando se emite un diagnostico.
 * @param d Diagnostico emitido.
 */
using DiagCallback = std::function<void(const Diagnostic& d)>;

/**
 * @brief Motor de diagnosticos: acumula y reporta mensajes del preprocesador.
 *
 * Permite registrar un callback personalizado para redirigir los mensajes
 * a cualquier destino (consola, archivo, coleccion, etc.).
 */
class DiagnosticEngine {
public:
    /**
     * @brief Constructor.
     * @param cb Callback opcional para cada diagnostico emitido.
     *           Si es nullptr se usan los defaults (stderr para errores).
     */
    explicit DiagnosticEngine(DiagCallback cb = nullptr);

    /**
     * @brief Emite un diagnostico de nivel NOTE.
     * @param loc Ubicacion en el fuente.
     * @param msg Mensaje de la nota.
     */
    void note(const SourceLocation& loc, std::string msg);

    /**
     * @brief Emite un diagnostico de nivel WARNING.
     * @param loc Ubicacion en el fuente.
     * @param msg Mensaje de la advertencia.
     */
    void warning(const SourceLocation& loc, std::string msg);

    /**
     * @brief Emite un diagnostico de nivel ERROR.
     * @param loc Ubicacion en el fuente.
     * @param msg Mensaje del error.
     */
    void error(const SourceLocation& loc, std::string msg);

    /**
     * @brief Emite un diagnostico de nivel FATAL y lanza std::runtime_error.
     * @param loc Ubicacion en el fuente.
     * @param msg Mensaje del error fatal.
     */
    [[noreturn]] void fatal(const SourceLocation& loc, std::string msg);

    /**
     * @brief Indica si se emitio al menos un error (no fatal).
     * @return true si hay uno o mas errores registrados.
     */
    bool has_errors() const noexcept { return m_error_count > 0; }

    /**
     * @brief Numero total de errores emitidos.
     * @return Conteo de errores.
     */
    uint32_t error_count()   const noexcept { return m_error_count; }

    /**
     * @brief Numero total de advertencias emitidas.
     * @return Conteo de advertencias.
     */
    uint32_t warning_count() const noexcept { return m_warning_count; }

    /**
     * @brief Todos los diagnosticos acumulados.
     * @return Referencia constante al vector de diagnosticos.
     */
    const std::vector<Diagnostic>& diagnostics() const noexcept {
        return m_diags;
    }

    /**
     * @brief Imprime todos los diagnosticos en el flujo dado.
     * @param out Flujo de salida destino.
     */
    void print_all(std::ostream& out) const;

    /**
     * @brief Limpia todos los diagnosticos acumulados y reinicia contadores.
     */
    void clear();

private:
    std::vector<Diagnostic> m_diags;         ///< Diagnosticos acumulados
    DiagCallback            m_callback;       ///< Callback personalizado
    uint32_t                m_error_count;    ///< Conteo de errores
    uint32_t                m_warning_count;  ///< Conteo de advertencias

    /**
     * @brief Emite un diagnostico y lo registra.
     * @param d Diagnostico a emitir.
     */
    void emit(Diagnostic d);
};

} // namespace vpp
