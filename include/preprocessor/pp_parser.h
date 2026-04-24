/**
 * @file pp_parser.h
 * @brief Parser del preprocesador: construye el AST a partir de los tokens.
 */
#pragma once

#include "pp_token.h"
#include "pp_ast.h"
#include "pp_diagnostics.h"
#include <vector>
#include <memory>

namespace vpp {

/**
 * @brief Parser del preprocesador.
 *
 * Recorre la secuencia de tokens producida por PPLexer y construye el AST
 * del preprocesador. Reconoce:
 *   - Directivas: #define, #undef, #include
 *   - Condicionales: #if, #ifdef, #ifndef, #elif, #else, #endif
 *   - Mensajes: #error, #warning
 *   - Informacion: #pragma, #line
 *   - Metaprogramacion: #foreach, #endforeach, #repeat, #endrepeat, #array, #exec, #set
 *   - Libreria de macros: #import
 *   - Texto plano (no directiva): emitido como TextNode
 *
 * El parser no evalua macros ni condiciones; eso es responsabilidad del
 * evaluador. Se limita a construir la estructura del AST.
 */
class PPParser {
public:
    /**
     * @brief Constructor.
     * @param tokens Secuencia de tokens del lexer (incluyendo PP_EOF al final).
     * @param diag   Motor de diagnosticos para errores de sintaxis.
     */
    PPParser(std::vector<PPToken> tokens, DiagnosticEngine& diag);

    /**
     * @brief Construye el AST completo del archivo fuente.
     * @return NodePtr a un BlockNode raiz con todos los nodos del archivo.
     */
    NodePtr parse();

private:
    std::vector<PPToken> m_toks; ///< Tokens de entrada
    size_t               m_pos;  ///< Posicion actual en m_toks
    DiagnosticEngine&    m_diag; ///< Motor de diagnosticos

    // --- acceso al flujo de tokens ---

    /**
     * @brief Devuelve el token en la posicion actual sin consumirlo.
     * @return Referencia constante al token actual.
     */
    const PPToken& cur() const;

    /**
     * @brief Devuelve el token en la posicion actual + offset sin consumirlo.
     * @param offset Desplazamiento desde la posicion actual.
     * @return Referencia constante al token en esa posicion.
     */
    const PPToken& peek(int offset = 1) const;

    /**
     * @brief Avanza al siguiente token y devuelve el consumido.
     * @return Token consumido.
     */
    PPToken consume();

    /**
     * @brief Consume el token actual si es del tipo dado; de lo contrario error.
     * @param t   Tipo esperado.
     * @param msg Mensaje de error si no coincide.
     * @return Token consumido.
     */
    PPToken expect(PPTokenType t, const char* msg);

    /**
     * @brief Indica si el token actual es del tipo indicado.
     * @param t Tipo a verificar.
     * @return true si coincide.
     */
    bool check(PPTokenType t) const;

    /**
     * @brief Salta todos los tokens NEWLINE y WHITESPACE consecutivos.
     */
    void skip_blanks();

    /**
     * @brief Consume tokens hasta el proximo NEWLINE o PP_EOF.
     * @return Vector de tokens consumidos (sin incluir el NEWLINE).
     */
    std::vector<PPToken> consume_rest_of_line();

    // --- construccion del AST ---

    /**
     * @brief Parsea un bloque de nodos hasta encontrar una directiva de cierre.
     *
     * Un bloque termina cuando se encuentra un token de cierre:
     * #endif, #else, #elif, #endforeach, #endrepeat o PP_EOF.
     *
     * @param l Ubicacion del inicio del bloque.
     * @return NodePtr a un BlockNode con los nodos del bloque.
     */
    NodePtr parse_block();

    /**
     * @brief Parsea el siguiente nodo del bloque actual.
     * @return NodePtr al nodo parseado, o nullptr si no hay mas nodos.
     */
    NodePtr parse_node();

    /**
     * @brief Parsea una linea de texto plano (no directiva).
     * @return NodePtr a un TextNode con los tokens de la linea.
     */
    NodePtr parse_text_line();

    /**
     * @brief Parsea una linea de directiva comenzando en el token HASH.
     * @return NodePtr al nodo de la directiva, o nullptr si es directiva de cierre.
     */
    NodePtr parse_directive();

    // --- parsers de directivas especificas ---

    /**
     * @brief Parsea #define NAME [(...)] [cuerpo].
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a DefineNode.
     */
    NodePtr parse_define(SourceLocation hash_loc);

    /**
     * @brief Parsea #undef NAME.
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a UndefNode.
     */
    NodePtr parse_undef(SourceLocation hash_loc);

    /**
     * @brief Parsea #include "path" o #include <path>.
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a IncludeNode.
     */
    NodePtr parse_include(SourceLocation hash_loc);

    /**
     * @brief Parsea #import <path> (inclusion de libreria de macros vpp).
     *
     * Equivalente a #include <path> pero con semantica auto-once y busqueda
     * en import_paths. El nombre de modulo no requiere extension; se prueba
     * con .vph, .vel y sin extension.
     *
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a IncludeNode con is_import=true.
     */
    NodePtr parse_import_macro(SourceLocation hash_loc);

    /**
     * @brief Parsea #if EXPR, #ifdef NAME o #ifndef NAME con su bloque.
     * @param hash_loc Ubicacion del token '#'.
     * @param variant  Tipo de directiva IF/IFDEF/IFNDEF.
     * @return NodePtr a IfBlockNode.
     */
    NodePtr parse_if_block(SourceLocation hash_loc, IfBlockNode::Kind variant);

    /**
     * @brief Parsea #error "mensaje".
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a ErrorDirNode.
     */
    NodePtr parse_error(SourceLocation hash_loc);

    /**
     * @brief Parsea #warning "mensaje".
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a WarningDirNode.
     */
    NodePtr parse_warning(SourceLocation hash_loc);

    /**
     * @brief Parsea #pragma [argumentos].
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a PragmaNode.
     */
    NodePtr parse_pragma(SourceLocation hash_loc);

    /**
     * @brief Parsea #line N ["archivo"].
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a LineDirNode.
     */
    NodePtr parse_line(SourceLocation hash_loc);

    /**
     * @brief Parsea #foreach VAR in (lista) ... #endforeach.
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a ForeachNode.
     */
    NodePtr parse_foreach(SourceLocation hash_loc);

    /**
     * @brief Parsea #repeat EXPR ... #endrepeat.
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a RepeatNode.
     */
    NodePtr parse_repeat(SourceLocation hash_loc);

    /**
     * @brief Parsea #array NAME (item1, item2, ...).
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a ArrayDefNode.
     */
    NodePtr parse_array(SourceLocation hash_loc);

    /**
     * @brief Parsea #exec VARNAME comando....
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a ExecDirNode.
     */
    NodePtr parse_exec(SourceLocation hash_loc);

    /**
     * @brief Parsea #set VAR op expr (variables del preprocesador).
     *
     * Soporta: = += -= *= /= %= &= |= ^= <<= >>= ++ --
     *
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a SetDirNode.
     */
    NodePtr parse_set(SourceLocation hash_loc);

    /**
     * @brief Parsea #assert expr ["mensaje"].
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a AssertNode.
     */
    NodePtr parse_assert(SourceLocation hash_loc);

    /**
     * @brief Parsea #macro NAME(params) ... #endmacro.
     * @param hash_loc Ubicacion del token '#'.
     * @return NodePtr a MacroBlockNode.
     */
    NodePtr parse_macro(SourceLocation hash_loc);
};

} // namespace vpp
