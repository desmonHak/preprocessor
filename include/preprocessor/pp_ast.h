/**
 * @file pp_ast.h
 * @brief Arbol de sintaxis abstracta (AST) del preprocesador vpp.
 *
 * Define los nodos del AST que representan directivas de preprocesador
 * y bloques de texto plano. El AST se construye por el parser y luego
 * el evaluador lo recorre para producir la salida preprocesada.
 */
#pragma once

#include "pp_token.h"
#include <memory>
#include <vector>
#include <string>

namespace vpp {

/* --- tipos de nodo -------------------------------------------------------- */

/**
 * @brief Tipos de nodo del AST del preprocesador.
 */
enum class NodeKind : uint8_t {
    BLOCK,          ///< Secuencia de nodos hijos
    TEXT,           ///< Linea de texto plano (con tokens para expansion)
    DEFINE,         ///< Directiva #define
    UNDEF,          ///< Directiva #undef
    INCLUDE,        ///< Directiva #include
    IF_BLOCK,       ///< Bloque #if / #ifdef / #ifndef ... #endif
    ERROR_DIR,      ///< Directiva #error
    WARNING_DIR,    ///< Directiva #warning
    PRAGMA,         ///< Directiva #pragma
    LINE_DIR,       ///< Directiva #line
    FOREACH_BLOCK,  ///< Bloque #foreach ... #endforeach (metaprogramacion)
    REPEAT_BLOCK,   ///< Bloque #repeat ... #endrepeat (metaprogramacion)
    ARRAY_DEF,      ///< Directiva #array: define un array de cadenas
    EXEC_DIR,       ///< Directiva #exec: ejecuta un comando del sistema
    SET_DIR,        ///< Directiva #set: asigna o modifica una variable
    ASSERT_DIR,     ///< Directiva #assert: comprueba una condicion en preprocesado
    MACRO_BLOCK     ///< Bloque #macro ... #endmacro: macro funcion multilínea
};

/* --- clase base ----------------------------------------------------------- */

/**
 * @brief Clase base abstracta de todos los nodos del AST.
 */
struct ASTNode {
    NodeKind       kind;    ///< Tipo de nodo
    SourceLocation loc;     ///< Ubicacion del nodo en el fuente

    /**
     * @brief Constructor.
     * @param k Tipo de nodo.
     * @param l Ubicacion en fuente.
     */
    ASTNode(NodeKind k, SourceLocation l) : kind(k), loc(std::move(l)) {}
    virtual ~ASTNode() = default;

    ASTNode(const ASTNode&)            = delete;
    ASTNode& operator=(const ASTNode&) = delete;
};

/** @brief Puntero unico a nodo AST. */
using NodePtr  = std::unique_ptr<ASTNode>;
/** @brief Coleccion de punteros a nodos AST. */
using NodeList = std::vector<NodePtr>;

/* --- nodos concretos ------------------------------------------------------ */

/**
 * @brief Nodo bloque: contiene una secuencia ordenada de nodos hijos.
 *
 * Representa el cuerpo de un archivo, de una rama condicional,
 * o del cuerpo de #foreach / #repeat.
 */
struct BlockNode : ASTNode {
    NodeList children; ///< Nodos hijos en orden de aparicion

    /**
     * @brief Constructor.
     * @param l Ubicacion del bloque.
     */
    explicit BlockNode(SourceLocation l)
        : ASTNode(NodeKind::BLOCK, std::move(l)) {}
};

/**
 * @brief Nodo de texto plano: una o mas lineas de contenido no directivo.
 *
 * Los tokens de la linea se almacenan para permitir la expansion de macros
 * durante la fase de evaluacion.
 */
struct TextNode : ASTNode {
    std::vector<PPToken> tokens; ///< Tokens que componen el texto (TEXT, IDENT, etc.)

    /**
     * @brief Constructor.
     * @param l      Ubicacion del nodo.
     * @param toks   Tokens del texto.
     */
    TextNode(SourceLocation l, std::vector<PPToken> toks)
        : ASTNode(NodeKind::TEXT, std::move(l))
        , tokens(std::move(toks)) {}
};

/**
 * @brief Nodo #define: definicion de macro objeto o funcion.
 */
struct DefineNode : ASTNode {
    std::string              name;          ///< Nombre de la macro
    std::vector<std::string> params;        ///< Parametros (vacio si es objeto)
    bool                     is_function;   ///< true si es macro funcion
    bool                     is_variadic;   ///< true si el ultimo param es ...
    std::vector<PPToken>     body;          ///< Tokens del cuerpo de la macro

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param n    Nombre de la macro.
     * @param ps   Lista de parametros.
     * @param fn   true si es macro funcion.
     * @param va   true si es variadic.
     * @param b    Tokens del cuerpo.
     */
    DefineNode(SourceLocation l, std::string n,
               std::vector<std::string> ps, bool fn, bool va,
               std::vector<PPToken> b)
        : ASTNode(NodeKind::DEFINE, std::move(l))
        , name(std::move(n)), params(std::move(ps))
        , is_function(fn), is_variadic(va), body(std::move(b)) {}
};

/**
 * @brief Nodo #undef: eliminacion de una macro.
 */
struct UndefNode : ASTNode {
    std::string name; ///< Nombre de la macro a eliminar

    /**
     * @brief Constructor.
     * @param l Ubicacion de la directiva.
     * @param n Nombre de la macro.
     */
    UndefNode(SourceLocation l, std::string n)
        : ASTNode(NodeKind::UNDEF, std::move(l)), name(std::move(n)) {}
};

/**
 * @brief Nodo #include / #import: inclusion de otro archivo fuente o libreria de macros.
 *
 * - #include "path"  -> is_system=false, is_import=false
 * - #include <path>  -> is_system=true,  is_import=false
 * - #import <path>   -> is_system=true,  is_import=true  (auto-once, busca en import_paths)
 */
struct IncludeNode : ASTNode {
    std::string path;        ///< Ruta del archivo a incluir
    bool        is_system;   ///< true si es <path>, false si es "path"
    bool        is_import;   ///< true si es #import (semantica auto-once + import_paths)

    /**
     * @brief Tokens de la ruta cuando hay que expandir macros para conocerla.
     *
     * `#include CABECERA` es valido en C: los tokens que siguen se expanden y
     * el resultado debe formar "..." o <...>.  El parser no puede resolverlo
     * porque no ve la tabla de macros, asi que los guarda aqui y la decision se
     * aplaza a la evaluacion.  Vacio cuando la ruta venia escrita literalmente.
     */
    std::vector<PPToken> path_tokens;

    /**
     * @brief Constructor.
     * @param l      Ubicacion de la directiva.
     * @param p      Ruta del archivo.
     * @param sys    true si es inclusion de sistema (<...>).
     * @param import true si es directiva #import.
     */
    IncludeNode(SourceLocation l, std::string p, bool sys, bool import = false)
        : ASTNode(NodeKind::INCLUDE, std::move(l))
        , path(std::move(p)), is_system(sys), is_import(import) {}
};

/**
 * @brief Rama de una directiva #elif o #else dentro de un bloque condicional.
 */
struct IfBranch {
    std::vector<PPToken> condition; ///< Tokens de la condicion (vacio para #else)
    bool                 is_else;   ///< true si es rama #else (sin condicion)
    NodePtr              body;      ///< Cuerpo de la rama como BlockNode

    /**
     * @brief Constructor.
     * @param cond  Tokens de la condicion.
     * @param else_ true si es #else.
     * @param b     Cuerpo de la rama.
     */
    IfBranch(std::vector<PPToken> cond, bool else_, NodePtr b)
        : condition(std::move(cond)), is_else(else_), body(std::move(b)) {}
};

/**
 * @brief Nodo bloque condicional: #if/#ifdef/#ifndef ... (#elif)* (#else)? #endif
 */
struct IfBlockNode : ASTNode {
    /**
     * @brief Variante del inicio del condicional.
     */
    enum class Kind { IF, IFDEF, IFNDEF };

    Kind                     variant;    ///< Tipo de directiva de apertura
    std::vector<PPToken>     condition;  ///< Tokens de la condicion principal
    NodePtr                  then_block; ///< Cuerpo del bloque verdadero
    std::vector<IfBranch>    elif_chain; ///< Cadena de #elif
    NodePtr                  else_block; ///< Cuerpo del bloque #else (puede ser nullptr)

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param v    Variante (IF/IFDEF/IFNDEF).
     * @param cond Tokens de la condicion.
     * @param tb   Bloque principal.
     * @param ec   Cadena de elif.
     * @param eb   Bloque else (puede ser nullptr).
     */
    IfBlockNode(SourceLocation l, Kind v,
                std::vector<PPToken> cond,
                NodePtr tb,
                std::vector<IfBranch> ec,
                NodePtr eb)
        : ASTNode(NodeKind::IF_BLOCK, std::move(l))
        , variant(v), condition(std::move(cond))
        , then_block(std::move(tb))
        , elif_chain(std::move(ec))
        , else_block(std::move(eb)) {}
};

/**
 * @brief Nodo #error: emite un error de preprocesador.
 */
struct ErrorDirNode : ASTNode {
    std::string message; ///< Texto del mensaje de error

    /**
     * @brief Constructor.
     * @param l   Ubicacion de la directiva.
     * @param msg Mensaje de error.
     */
    ErrorDirNode(SourceLocation l, std::string msg)
        : ASTNode(NodeKind::ERROR_DIR, std::move(l))
        , message(std::move(msg)) {}
};

/**
 * @brief Nodo #warning: emite una advertencia de preprocesador.
 */
struct WarningDirNode : ASTNode {
    std::string message; ///< Texto del mensaje de advertencia

    /**
     * @brief Constructor.
     * @param l   Ubicacion de la directiva.
     * @param msg Mensaje de advertencia.
     */
    WarningDirNode(SourceLocation l, std::string msg)
        : ASTNode(NodeKind::WARNING_DIR, std::move(l))
        , message(std::move(msg)) {}
};

/**
 * @brief Nodo #pragma: directiva de comportamiento del preprocesador.
 */
struct PragmaNode : ASTNode {
    std::vector<PPToken> args; ///< Tokens de los argumentos del pragma

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param args Tokens de los argumentos.
     */
    PragmaNode(SourceLocation l, std::vector<PPToken> args)
        : ASTNode(NodeKind::PRAGMA, std::move(l))
        , args(std::move(args)) {}
};

/**
 * @brief Nodo #line: redefine el numero de linea y nombre de archivo.
 */
struct LineDirNode : ASTNode {
    uint32_t    line_num;   ///< Nuevo numero de linea
    std::string filename;   ///< Nuevo nombre de archivo (puede ser vacio)

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param num  Nuevo numero de linea.
     * @param file Nuevo nombre de archivo (vacio = sin cambio).
     */
    LineDirNode(SourceLocation l, uint32_t num, std::string file)
        : ASTNode(NodeKind::LINE_DIR, std::move(l))
        , line_num(num), filename(std::move(file)) {}
};

/**
 * @brief Nodo #foreach: iteracion sobre una lista o array (metaprogramacion).
 *
 * Sintaxis inline: #foreach VAR in (item1, item2, ...)
 * Sintaxis array:  #foreach VAR in NOMBRE_ARRAY
 *
 * Si array_name no esta vacio, se itera sobre el array definido con #array.
 * Si array_name esta vacio, se usan los items inline de la lista.
 */
struct ForeachNode : ASTNode {
    std::string              var_name;    ///< Variable de iteracion
    std::string              index_var;  ///< Variable de indice opcional (#foreach V, I in ...)
    std::string              array_name;  ///< Nombre del array (vacio = usa items inline)
    std::vector<std::string> items;       ///< Lista inline de valores (si array_name vacio)
    NodePtr                  body;        ///< Cuerpo a repetir por cada item

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param var  Nombre de la variable de iteracion.
     * @param idx  Nombre de la variable de indice (vacio = sin indice).
     * @param arr  Nombre del array a iterar (vacio para lista inline).
     * @param list Lista inline de valores (usada cuando arr esta vacio).
     * @param b    Cuerpo del bucle.
     */
    ForeachNode(SourceLocation l, std::string var, std::string idx,
                std::string arr, std::vector<std::string> list, NodePtr b)
        : ASTNode(NodeKind::FOREACH_BLOCK, std::move(l))
        , var_name(std::move(var)), index_var(std::move(idx))
        , array_name(std::move(arr))
        , items(std::move(list)), body(std::move(b)) {}
};

/**
 * @brief Nodo #array: define un array de cadenas con nombre.
 *
 * Sintaxis: #array NAME (item1, item2, ...)
 * El array puede ser iterado por #foreach VAR in NAME.
 */
struct ArrayDefNode : ASTNode {
    std::string              name;   ///< Nombre del array
    std::vector<std::string> items;  ///< Elementos del array

    /**
     * @brief Constructor.
     * @param l     Ubicacion de la directiva.
     * @param n     Nombre del array.
     * @param list  Elementos del array.
     */
    ArrayDefNode(SourceLocation l, std::string n, std::vector<std::string> list)
        : ASTNode(NodeKind::ARRAY_DEF, std::move(l))
        , name(std::move(n)), items(std::move(list)) {}
};

/**
 * @brief Nodo #exec: ejecuta un comando del sistema y guarda la salida en una macro.
 *
 * Sintaxis: #exec VARNAME comando argumentos...
 * La salida estandar del comando se almacena como el valor de la macro VARNAME.
 */
struct ExecDirNode : ASTNode {
    std::string var_name;  ///< Macro donde se guarda la salida del comando
    std::string command;   ///< Comando a ejecutar (expandido antes de ejecutar)

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param var  Nombre de la macro destino.
     * @param cmd  Linea de comando.
     */
    ExecDirNode(SourceLocation l, std::string var, std::string cmd)
        : ASTNode(NodeKind::EXEC_DIR, std::move(l))
        , var_name(std::move(var)), command(std::move(cmd)) {}
};

/**
 * @brief Nodo #repeat: repeticion de un bloque N veces (metaprogramacion).
 *
 * Sintaxis: #repeat N
 * Dentro del cuerpo, __REPEAT_INDEX__ se expande al indice actual (base 0).
 */
struct RepeatNode : ASTNode {
    std::vector<PPToken> count_expr; ///< Expresion que determina el numero de repeticiones
    NodePtr              body;       ///< Cuerpo a repetir

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param expr Tokens de la expresion de conteo.
     * @param b    Cuerpo del bloque de repeticion.
     */
    RepeatNode(SourceLocation l, std::vector<PPToken> expr, NodePtr b)
        : ASTNode(NodeKind::REPEAT_BLOCK, std::move(l))
        , count_expr(std::move(expr)), body(std::move(b)) {}
};

/**
 * @brief Nodo #set: asigna o modifica el valor de una variable del preprocesador.
 *
 * Sintaxis:
 *   #set VAR = expr
 *   #set VAR += expr   (y -=, *=, /=, %=, &=, |=, ^=, <<=, >>=)
 *   #set VAR++
 *   #set VAR--
 *
 * Las variables creadas con #set son macros objeto marcadas como is_variable,
 * lo que suprime las advertencias de redefinicion en asignaciones posteriores.
 */
struct SetDirNode : ASTNode {
    std::string          name;  ///< Nombre de la variable
    std::string          op;    ///< Operador: "=", "+=", "-=", "*=", "/=", "%=",
                                ///<   "&=", "|=", "^=", "<<=", ">>=", "++", "--"
    std::vector<PPToken> expr;  ///< Expresion RHS (vacia para ++ y --)

    /**
     * @brief Constructor.
     * @param l  Ubicacion de la directiva.
     * @param n  Nombre de la variable.
     * @param o  Operador de asignacion.
     * @param e  Tokens de la expresion RHS.
     */
    SetDirNode(SourceLocation l, std::string n,
               std::string o, std::vector<PPToken> e)
        : ASTNode(NodeKind::SET_DIR, std::move(l))
        , name(std::move(n)), op(std::move(o)), expr(std::move(e)) {}
};

/**
 * @brief Nodo #assert: comprueba una condicion en tiempo de preprocesado.
 *
 * Sintaxis: #assert expr ["mensaje de error"]
 *
 * Si la expresion evalua a 0 (falso) el preprocesador emite un error y
 * detiene el procesado.  El mensaje es opcional; si se omite se usa un
 * mensaje generico.
 */
struct AssertNode : ASTNode {
    std::vector<PPToken> condition;  ///< Expresion a evaluar
    std::string          message;    ///< Mensaje de error (puede estar vacio)

    /**
     * @brief Constructor.
     * @param l    Ubicacion de la directiva.
     * @param cond Tokens de la expresion condicional.
     * @param msg  Mensaje de error opcional.
     */
    AssertNode(SourceLocation l, std::vector<PPToken> cond, std::string msg)
        : ASTNode(NodeKind::ASSERT_DIR, std::move(l))
        , condition(std::move(cond)), message(std::move(msg)) {}
};

/**
 * @brief Nodo #macro ... #endmacro: macro funcion multilinea.
 *
 * Sintaxis:
 *   #macro NAME(param1, param2, ...)
 *       ... cuerpo multilinea con sustitusion de parametros ...
 *   #endmacro
 *
 * A diferencia de #define, el cuerpo puede abarcar varias lineas.
 * Los parametros se sustituyen como en cualquier macro funcion.
 */
struct MacroBlockNode : ASTNode {
    std::string              name;         ///< Nombre de la macro
    std::vector<std::string> params;       ///< Parametros formales
    bool                     is_variadic;  ///< true si el ultimo param es ...
    std::vector<PPToken>     body;         ///< Tokens del cuerpo (incluye NEWLINEs)

    /**
     * @brief Constructor.
     * @param l       Ubicacion de la directiva.
     * @param n       Nombre de la macro.
     * @param ps      Lista de parametros.
     * @param va      true si es variadic.
     * @param b       Tokens del cuerpo multilinea.
     */
    MacroBlockNode(SourceLocation l, std::string n, std::vector<std::string> ps,
                   bool va, std::vector<PPToken> b)
        : ASTNode(NodeKind::MACRO_BLOCK, std::move(l))
        , name(std::move(n)), params(std::move(ps))
        , is_variadic(va), body(std::move(b)) {}
};

/* --- helpers -------------------------------------------------------------- */

/**
 * @brief Crea un BlockNode en el heap.
 * @param l Ubicacion del bloque.
 * @return NodePtr apuntando al nuevo BlockNode.
 */
inline NodePtr make_block(SourceLocation l) {
    return std::make_unique<BlockNode>(std::move(l));
}

} // namespace vpp
