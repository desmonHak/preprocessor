/**
 * @file pp_macro.h
 * @brief Definicion y tabla de macros del preprocesador vpp.
 */
#pragma once

#include "pp_token.h"
#include "pp_diagnostics.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>

namespace vpp {

// declaracion adelantada necesaria para BuiltinFnHandler
class MacroTable;

/**
 * @brief Tipo de manejador para macros funcion predefinidas del sistema.
 *
 * Recibe la tabla de macros (para acceso a arrays), los argumentos ya
 * expandidos como cadenas planas (sin comillas), y la ubicacion de la
 * llamada para diagnosticos.
 * Devuelve un PPToken con el resultado (NUMBER, STRING o IDENT).
 */
using BuiltinFnHandler = std::function<PPToken(
    MacroTable&,
    const std::vector<std::string>&,
    const SourceLocation&)>;

/**
 * @brief Definicion de una macro (objeto o funcion).
 */
struct MacroDef {
    std::string              name;         ///< Nombre de la macro
    std::vector<std::string> params;       ///< Nombres de parametros formales
    std::vector<PPToken>     body;         ///< Tokens del cuerpo de expansion
    bool                     is_function;  ///< true si es macro funcion (tiene parentesis)
    bool                     is_variadic;  ///< true si acepta ... como ultimo parametro
    bool                     is_builtin;   ///< true si es una macro predefinida del sistema
    bool                     is_variable;  ///< true si fue creada con #set (suprime warning de redefinicion)
    SourceLocation           def_loc;      ///< Ubicacion donde fue definida

    /**
     * @brief Constructor para macro objeto.
     * @param n    Nombre.
     * @param b    Tokens del cuerpo.
     * @param l    Ubicacion de la definicion.
     * @param blt  true si es predefinida.
     */
    MacroDef(std::string n, std::vector<PPToken> b,
             SourceLocation l, bool blt = false)
        : name(std::move(n)), body(std::move(b))
        , is_function(false), is_variadic(false)
        , is_builtin(blt), is_variable(false), def_loc(std::move(l)) {}

    /**
     * @brief Constructor para macro funcion.
     * @param n    Nombre.
     * @param ps   Parametros formales.
     * @param va   true si es variadic.
     * @param b    Tokens del cuerpo.
     * @param l    Ubicacion de la definicion.
     */
    MacroDef(std::string n, std::vector<std::string> ps, bool va,
             std::vector<PPToken> b, SourceLocation l)
        : name(std::move(n)), params(std::move(ps)), body(std::move(b))
        , is_function(true), is_variadic(va)
        , is_builtin(false), is_variable(false), def_loc(std::move(l)) {}
};

/**
 * @brief Tabla de macros: almacena y gestiona todas las definiciones activas.
 *
 * Proporciona operaciones para definir, eliminar y consultar macros,
 * asi como para expandir tokens aplicando las macros definidas.
 */
class MacroTable {
public:
    /**
     * @brief Constructor. Registra las macros predefinidas del sistema.
     * @param diag Motor de diagnosticos para advertencias de redefinicion.
     */
    explicit MacroTable(DiagnosticEngine& diag);

    /**
     * @brief Define o redefine una macro.
     *
     * Si la macro ya esta definida y la nueva definicion difiere,
     * emite una advertencia de redefinicion.
     *
     * @param def Definicion de la macro.
     */
    void define(MacroDef def);

    /**
     * @brief Elimina la definicion de una macro.
     * @param name Nombre de la macro a eliminar.
     */
    void undef(const std::string& name);

    /**
     * @brief Indica si una macro esta definida.
     * @param name Nombre de la macro.
     * @return true si la macro existe en la tabla.
     */
    bool is_defined(const std::string& name) const;

    /**
     * @brief Obtiene la definicion de una macro.
     * @param name Nombre de la macro.
     * @return Puntero constante a MacroDef, o nullptr si no existe.
     */
    const MacroDef* get(const std::string& name) const;

    /**
     * @brief Expande una secuencia de tokens aplicando las macros definidas.
     *
     * Realiza expansion recursiva con deteccion de ciclos para evitar
     * recursion infinita.
     *
     * @param tokens  Tokens de entrada a expandir.
     * @param call_loc Ubicacion del punto de expansion (para diagnosticos).
     * @return Tokens resultantes despues de la expansion.
     */
    std::vector<PPToken> expand(const std::vector<PPToken>& tokens,
                                const SourceLocation& call_loc);

    /**
     * @brief Define las macros predefinidas de plataforma y arquitectura.
     *
     * Detecta el sistema operativo y la arquitectura en tiempo de compilacion
     * y define las macros correspondientes (__WINDOWS__, __LINUX__, etc.).
     */
    void register_platform_macros();

    /**
     * @brief Define una macro simple de valor constante.
     * @param name  Nombre de la macro.
     * @param value Valor como cadena.
     */
    void define_simple(const std::string& name, const std::string& value);

    /**
     * @brief Define una macro sin valor (flag).
     * @param name Nombre de la macro flag.
     */
    void define_flag(const std::string& name);

    /**
     * @brief Devuelve el mapa completo de macros definidas.
     * @return Referencia constante al mapa interno.
     */
    const std::unordered_map<std::string, MacroDef>& all() const noexcept {
        return m_table;
    }

    // --- gestion de arrays --------------------------------------------------

    /**
     * @brief Define o redefine un array de cadenas con el nombre dado.
     * @param name  Nombre del array.
     * @param items Elementos del array.
     */
    void define_array(const std::string& name, std::vector<std::string> items);

    /**
     * @brief Indica si existe un array con el nombre dado.
     * @param name Nombre del array.
     * @return true si el array esta definido.
     */
    bool has_array(const std::string& name) const;

    /**
     * @brief Obtiene los elementos de un array por nombre.
     * @param name Nombre del array.
     * @return Puntero constante al vector de items, o nullptr si no existe.
     */
    const std::vector<std::string>* get_array(const std::string& name) const;

    /**
     * @brief Elimina un array por nombre.
     * @param name Nombre del array a eliminar.
     */
    void undef_array(const std::string& name);

    /**
     * @brief Devuelve el mapa completo de arrays definidos.
     * @return Referencia constante al mapa de arrays.
     */
    const std::unordered_map<std::string, std::vector<std::string>>& all_arrays()
        const noexcept {
        return m_arrays;
    }

    // --- macros funcion predefinidas del sistema ----------------------------

    /**
     * @brief Busca el manejador de una macro funcion predefinida.
     * @param name Nombre de la macro.
     * @return Puntero constante al manejador, o nullptr si no es predefinida.
     */
    const BuiltinFnHandler* get_builtin_fn(const std::string& name) const;

    // metodos de expansion usados internamente y por las funciones helper estaticas

    /**
     * @brief Expande un unico token identificador con sus argumentos si aplica.
     * @param tokens Secuencia de tokens de entrada.
     * @param pos    Posicion del identificador en tokens (se actualiza).
     * @param guard  Conjunto de macros en expansion (para evitar ciclos).
     * @return Tokens resultantes de la expansion.
     */
    std::vector<PPToken> expand_one(
        const std::vector<PPToken>& tokens,
        size_t& pos,
        std::unordered_map<std::string,bool>& guard);

    /**
     * @brief Recoge los argumentos de una llamada a macro funcion.
     * @param tokens   Secuencia de tokens.
     * @param pos      Posicion actual (debe apuntar al '(' de apertura).
     * @param mac      Definicion de la macro funcion.
     * @param call_loc Ubicacion de la llamada (para errores).
     * @return Vector de vectores: cada elemento es un argumento tokenizado.
     */
    std::vector<std::vector<PPToken>> collect_args(
        const std::vector<PPToken>& tokens,
        size_t& pos,
        const MacroDef& mac,
        const SourceLocation& call_loc);

    /**
     * @brief Sustituye los parametros formales por los argumentos en el cuerpo.
     * @param body   Tokens del cuerpo de la macro.
     * @param params Nombres de los parametros formales.
     * @param args   Argumentos ya expandidos.
     * @param raw_args      Argumentos SIN expandir.  Son los que corresponden a
     *                      un parametro pegado con `##`.
     * @param expanded_args Argumentos ya expandidos.  Son los que corresponden
     *                      a un parametro en cualquier otra posicion, tal y
     *                      como exige el estandar; sin ellos el idioma
     *                      XSTR(x)/STR(x) no funciona.
     * @param loc           Ubicacion de la llamada.
     * @return Tokens del cuerpo con los parametros sustituidos.
     */
    std::vector<PPToken> substitute(
        const std::vector<PPToken>& body,
        const std::vector<std::string>& params,
        const std::vector<std::vector<PPToken>>& raw_args,
        const std::vector<std::vector<PPToken>>& expanded_args,
        const SourceLocation& loc);

    /**
     * @brief Aplica el operador ## (pegado de tokens) en el cuerpo sustituido.
     * @param tokens Tokens con posibles operadores HASHHASH.
     * @return Tokens con los pegados aplicados.
     */
    std::vector<PPToken> apply_token_paste(const std::vector<PPToken>& tokens);

    /**
     * @brief Aplica el operador # (stringify) en el cuerpo sustituido.
     * @param tokens Tokens del cuerpo.
     * @param params Parametros formales.
     * @param args   Argumentos reales (sin expandir, para stringify exacto).
     * @return Tokens con stringify aplicado.
     */
    std::vector<PPToken> apply_stringify(
        const std::vector<PPToken>& tokens,
        const std::vector<std::string>& params,
        const std::vector<std::vector<PPToken>>& args);

private:
    std::unordered_map<std::string, MacroDef>              m_table;        ///< Macros definidas
    std::unordered_map<std::string, std::vector<std::string>> m_arrays;    ///< Arrays definidos
    std::unordered_map<std::string, BuiltinFnHandler>      m_builtin_fns;  ///< Macros funcion del sistema
    DiagnosticEngine&                                      m_diag;         ///< Motor de diagnosticos

    /**
     * @brief Registra todas las macros funcion predefinidas del sistema.
     * Se llama una vez desde el constructor.
     */
    void register_builtin_fns();
};

} // namespace vpp
