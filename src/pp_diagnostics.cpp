/**
 * @file pp_diagnostics.cpp
 * @brief Implementacion del sistema de diagnosticos del preprocesador vpp.
 */

#include "preprocessor/pp_diagnostics.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace vpp {

/* --- SourceLocation ------------------------------------------------------- */

std::string SourceLocation::to_string() const {
    std::ostringstream oss;
    if (!file().empty()) {
        oss << file() << ':';
    }
    oss << line << ':' << col;
    return oss.str();
}

/* --- Diagnostic ----------------------------------------------------------- */

const char* diag_level_name(DiagLevel lv) {
    switch (lv) {
        case DiagLevel::NOTE:    return "nota";
        case DiagLevel::WARNING: return "advertencia";
        case DiagLevel::ERR:   return "error";
        case DiagLevel::FATAL:   return "fatal";
    }
    return "desconocido";
}

std::string Diagnostic::format() const {
    std::ostringstream oss;
    oss << loc.to_string() << ": "
        << diag_level_name(level) << ": "
        << message;
    return oss.str();
}

/* --- DiagnosticEngine ----------------------------------------------------- */

DiagnosticEngine::DiagnosticEngine(DiagCallback cb)
    : m_callback(std::move(cb))
    , m_error_count(0)
    , m_warning_count(0)
{}

void DiagnosticEngine::note(const SourceLocation& loc, std::string msg) {
    emit(Diagnostic(DiagLevel::NOTE, loc, std::move(msg)));
}

void DiagnosticEngine::warning(const SourceLocation& loc, std::string msg) {
    ++m_warning_count;
    emit(Diagnostic(DiagLevel::WARNING, loc, std::move(msg)));
}

void DiagnosticEngine::error(const SourceLocation& loc, std::string msg) {
    ++m_error_count;
    emit(Diagnostic(DiagLevel::ERR, loc, std::move(msg)));
}

void DiagnosticEngine::fatal(const SourceLocation& loc, std::string msg) {
    ++m_error_count;
    Diagnostic d(DiagLevel::FATAL, loc, msg);
    emit(d);
    // lanzar excepcion para detener el procesamiento inmediatamente
    throw std::runtime_error(d.format());
}

void DiagnosticEngine::print_all(std::ostream& out) const {
    for (const auto& d : m_diags) {
        out << d.format() << '\n';
    }
}

void DiagnosticEngine::clear() {
    m_diags.clear();
    m_error_count   = 0;
    m_warning_count = 0;
}

void DiagnosticEngine::emit(Diagnostic d) {
    if (m_callback) {
        // usar el callback personalizado del usuario
        m_callback(d);
    } else {
        // comportamiento por defecto: imprimir en stderr
        std::cerr << d.format() << '\n';
    }
    m_diags.push_back(std::move(d));
}

} // namespace vpp
