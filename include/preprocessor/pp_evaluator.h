/**
 * @file pp_evaluator.h
 * @brief Evaluador de expresiones constantes para directivas #if/#elif.
 */
#pragma once

#include "pp_token.h"
#include "pp_macro.h"
#include "pp_diagnostics.h"
#include <cstdint>
#include <vector>

namespace vpp {

/**
 * @brief Resuelve un operador de prueba de caracteristicas.
 *
 * Son los `__has_builtin`, `__has_attribute`, `__has_include` y companeros que
 * aparecen en las condiciones de las cabeceras modernas.  No son macros: son
 * operadores que preguntan por lo que sabe hacer el COMPILADOR DE DESTINO, asi
 * que vpp no puede contestarlos por su cuenta y delega en quien si puede.
 *
 * @param op  Nombre del operador, con sus dos guiones bajos.
 * @param arg Texto entre parentesis, tal cual.
 * @return Valor entero del operador; 0 si no se sabe.
 */
using CapabilityResolver =
    std::function<int64_t(const std::string& op, const std::string& arg)>;

/**
 * @brief Valor de una expresion del preprocesador, con su caracter de signo.
 *
 * El estandar de C evalua las condiciones de `#if` en el entero mas ancho
 * disponible y aplica las conversiones aritmeticas habituales: si CUALQUIERA de
 * los operandos es sin signo, la operacion entera pasa a ser sin signo.  Eso
 * cambia resultados de forma poco intuitiva -- `-1 > 0u` es VERDADERO, porque
 * el -1 se convierte a un valor enorme -- y sin arrastrar el signo junto al
 * valor no hay manera de reproducirlo.
 */
struct PPValue {
    int64_t v = 0;                  ///< Bits del valor.
    bool    is_unsigned = false;    ///< true si debe interpretarse sin signo.

    /** @brief Los mismos bits leidos sin signo. @return Valor sin signo. */
    uint64_t u() const noexcept { return static_cast<uint64_t>(v); }
};

/**
 * @brief Evaluador de expresiones de preprocesador.
 *
 * Evalua expresiones de las directivas #if y #elif. Soporta:
 *   - Literales enteros (decimales, hexadecimales 0x, octales 0, binarios 0b)
 *   - Operadores aritmeticos: + - * / % ~ ^ & | << >>
 *   - Operadores logicos: && || !
 *   - Operadores de comparacion: == != < > <= >=
 *   - Operador ternario: cond ? val_true : val_false
 *   - Operador defined(MACRO) y defined MACRO
 *   - Macros predefinidas como __ARCH_32__, __WINDOWS__, etc.
 *
 * Los valores se manejan como enteros de 64 bits que ademas arrastran si son
 * con o sin signo, para poder aplicar las conversiones aritmeticas de C.
 * Las comparaciones y logicas devuelven 0 (falso) o 1 (verdadero).
 */
class PPEvaluator {
public:
    /**
     * @brief Constructor.
     * @param macros Tabla de macros para resolver identificadores y defined().
     * @param diag   Motor de diagnosticos para errores de expresion.
     */
    /**
     * @brief Constructor.
     * @param macros Tabla de macros.
     * @param diag   Motor de diagnosticos.
     * @param caps   Resolutor de operadores de prueba de caracteristicas.  Nulo
     *               significa que no hay a quien preguntar y valen 0.
     */
    PPEvaluator(MacroTable& macros, DiagnosticEngine& diag,
                CapabilityResolver caps = nullptr);

    /**
     * @brief Evalua una secuencia de tokens como expresion entera constante.
     * @param tokens  Tokens de la expresion (sin el HASH ni el nombre de directiva).
     * @param loc     Ubicacion de la directiva (para mensajes de error).
     * @return Valor entero de 64 bits resultado de la evaluacion.
     *         Devuelve 0 si hay error de evaluacion.
     */
    int64_t evaluate(const std::vector<PPToken>& tokens,
                     const SourceLocation& loc);

private:
    /**
     * @brief Sustituye cada `defined X` / `defined(X)` por 1 o 0.
     *
     * Se aplica ANTES de expandir macros, porque el operando de `defined` es lo
     * unico de la expresion que no debe expandirse.  Lo que quede malformado se
     * deja intacto para que el parser lo diagnostique con su ubicacion real.
     *
     * @param tokens Tokens de la condicion, sin expandir.
     * @return Los mismos tokens con los operadores `defined` ya resueltos.
     */
    std::vector<PPToken> resolve_defined(
        const std::vector<PPToken>& tokens) const;

    MacroTable&       m_macros; ///< Referencia a la tabla de macros
    DiagnosticEngine& m_diag;   ///< Motor de diagnosticos
    CapabilityResolver m_caps;  ///< Resolutor de capacidades; puede ser nulo

    // Estado del parser de expresion
    std::vector<PPToken> m_toks; ///< Tokens de la expresion a evaluar
    size_t               m_pos;  ///< Posicion actual en m_toks
    SourceLocation       m_loc;  ///< Ubicacion base para errores

    // --- acceso al flujo de tokens de expresion ---

    /**
     * @brief Devuelve el token actual sin consumirlo.
     * @return Token actual o PP_EOF si se llego al final.
     */
    const PPToken& cur() const;

    /**
     * @brief Avanza al siguiente token y devuelve el consumido.
     * @return Token consumido.
     */
    PPToken consume();

    /**
     * @brief Indica si el token actual es del tipo indicado.
     * @param t Tipo a verificar.
     * @return true si el token actual es de tipo t.
     */
    bool check(PPTokenType t) const;

    /**
     * @brief Consume el token actual si es del tipo indicado.
     * @param t Tipo esperado.
     * @return true si se consumio, false si no coincidio.
     */
    bool match(PPTokenType t);

    // --- parser descendente recursivo para expresiones ---

    /**
     * @brief Parsea una expresion ternaria (nivel mas bajo de precedencia).
     * @return Valor de la expresion.
     */
    PPValue parse_ternary();

    /**
     * @brief Parsea una expresion OR logico (||).
     * @return Valor de la expresion.
     */
    PPValue parse_or();

    /**
     * @brief Parsea una expresion AND logico (&&).
     * @return Valor de la expresion.
     */
    PPValue parse_and();

    /**
     * @brief Parsea una expresion OR de bits (|).
     * @return Valor de la expresion.
     */
    PPValue parse_bitor();

    /**
     * @brief Parsea una expresion XOR de bits (^).
     * @return Valor de la expresion.
     */
    PPValue parse_xor();

    /**
     * @brief Parsea una expresion AND de bits (&).
     * @return Valor de la expresion.
     */
    PPValue parse_bitand();

    /**
     * @brief Parsea una comparacion de igualdad (==, !=).
     * @return Valor de la expresion.
     */
    PPValue parse_equality();

    /**
     * @brief Parsea una comparacion relacional (<, >, <=, >=).
     * @return Valor de la expresion.
     */
    PPValue parse_relational();

    /**
     * @brief Parsea un desplazamiento de bits (<< o >>).
     * @return Valor de la expresion.
     */
    PPValue parse_shift();

    /**
     * @brief Parsea una suma o resta (+ o -).
     * @return Valor de la expresion.
     */
    PPValue parse_additive();

    /**
     * @brief Parsea una multiplicacion, division o modulo (*, /, %).
     * @return Valor de la expresion.
     */
    PPValue parse_multiplicative();

    /**
     * @brief Parsea un operador unario (!, ~, -, +) o un primario.
     * @return Valor de la expresion.
     */
    PPValue parse_unary();

    /**
     * @brief Parsea un primario: literal, identificador, defined(), o parentesis.
     * @return Valor del primario.
     */
    PPValue parse_primary();

    /**
     * @brief Convierte el valor textual de un token NUMBER a int64_t.
     * @param tok Token NUMBER a convertir.
     * @return Valor entero del literal.
     */
    PPValue parse_number_literal(const PPToken& tok);
};

} // namespace vpp
