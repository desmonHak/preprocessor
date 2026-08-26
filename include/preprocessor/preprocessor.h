/**
 * @file preprocessor.h
 * @brief Clase principal del preprocesador vpp.
 */
#pragma once

#include "pp_diagnostics.h"
#include "pp_macro.h"
#include "pp_evaluator.h"
#include "pp_ast.h"
#include "pp_lexer.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

namespace vpp {

/**
 * @brief Tipo de funcion para resolver rutas de inclusion.
 *
 * Dado el archivo que realiza el include y la ruta solicitada,
 * devuelve el contenido del archivo incluido, o un string vacio
 * si no se puede encontrar.
 */
using IncludeResolver = std::function<
    std::string(const std::string& from_file,
                const std::string& requested_path,
                bool is_system)>;

/**
 * @brief De donde sale un conjunto de macros a precargar.
 */
enum class PredefKind : uint8_t {
    Text,    ///< El propio texto de las directivas.
    File,    ///< Ruta de un fichero que las contiene.
    Command  ///< Comando cuya salida estandar las contiene.
};

/**
 * @brief Un conjunto de macros a precargar antes de procesar el fuente.
 *
 * NO es una lista de pares nombre=valor: es TEXTO con directivas, que se
 * procesa con el pipeline normal y del que solo se conservan las macros.  Esa
 * decision es lo que hace el mecanismo util de verdad -- un volcado real de
 * macros predefinidas trae macros funcion (`#define __glibcxx_assert(cond)`) y
 * valores de varios tokens (`#define __SIZE_TYPE__ long unsigned int`), que no
 * caben en un nombre=valor -- y ademas lo mantiene independiente del lenguaje:
 * el fichero es simplemente un fuente de directivas, venga de donde venga.
 */
struct PredefSource {
    PredefKind  kind;   ///< Como interpretar `value`.
    std::string value;  ///< Texto, ruta o comando, segun `kind`.
};

/**
 * @brief Opciones de configuracion del preprocesador.
 */
struct PPOptions {
    LexerOptions         lexer;             ///< Opciones del lexer
    bool                 expand_macros;     ///< true para expandir macros en texto plano
    bool                 track_includes;    ///< true para detectar inclusion circular
    std::vector<std::string> include_paths; ///< Rutas de busqueda para #include <...>
    std::vector<std::string> import_paths;  ///< Rutas de busqueda para #import (libreria de macros vpp)
    std::vector<std::string> predefines;    ///< Macros a predefinir (formato "NAME" o "NAME=val")
    /**
     * @brief Conjuntos de macros a precargar, en orden.
     *
     * Sirve para traerse las macros que un compilador concreto predefine.  Se
     * apunta al BINARIO exacto (`gcc-12 -dM -E -`, `clang-15 -dM -E -x c++ -`)
     * en lugar de a un nombre de compilador conocido: asi conviven varios
     * compiladores y varias versiones en la misma maquina sin que vpp tenga que
     * saber nada de ninguno, y el mecanismo vale igual para un lenguaje que no
     * sea C.
     */
    std::vector<PredefSource> predef_sources;
    bool                 emit_line_markers; ///< Emite marcadores #line tras cada #include

    /** @brief Constructor con valores por defecto. */
    PPOptions()
        : expand_macros(true)
        , track_includes(true)
        , emit_line_markers(false) {}
};

/**
 * @brief Preprocesador vpp: clase principal que une todos los componentes.
 *
 * Uso tipico:
 * @code
 *   vpp::Preprocessor pp;
 *   pp.options().predefines.push_back("MY_FLAG");
 *   std::string result = pp.process(source_text, "mi_archivo.vel");
 *   if (pp.diagnostics().has_errors()) { ... }
 * @endcode
 */
class Preprocessor {
public:
    /**
     * @brief Constructor por defecto.
     * Registra las macros predefinidas de plataforma y arquitectura.
     */
    Preprocessor();

    /**
     * @brief Constructor con callback de diagnosticos personalizado.
     * @param cb Callback invocado por cada diagnostico.
     */
    explicit Preprocessor(DiagCallback cb);

    /**
     * @brief Preprocesa un texto fuente completo.
     *
     * Tokeniza, parsea el AST y evalua todos los nodos produciendo
     * el texto de salida con macros expandidas y condicionales resueltos.
     *
     * @param source   Contenido del archivo fuente.
     * @param filename Nombre del archivo (para mensajes de error y __FILE__).
     * @return Texto preprocesado listo para ser compilado.
     */
    std::string process(const std::string& source,
                        const std::string& filename = "<stdin>");

    /**
     * @brief Preprocesa un archivo dado su ruta.
     * @param filepath Ruta completa al archivo a preprocesar.
     * @return Texto preprocesado, o cadena vacia si no se puede leer el archivo.
     */
    std::string process_file(const std::string& filepath);

    /**
     * @brief Los ficheros que este fuente incluyo, en orden y sin repetir.
     *
     * Quien cachea el resultado de compilar necesita saber DE QUE depende, y
     * un `#include` es una dependencia igual que un `import`: si cambia, lo
     * compilado deja de valer.  Sin esto, un cache serviria el artefacto viejo
     * -- que no da error, da un resultado que ya no corresponde al codigo.
     *
     * @return Rutas tal como se resolvieron.
     */
    const std::vector<std::string>& included_files() const noexcept {
        return m_included_files;
    }

    /**
     * @brief Acceso a las opciones del preprocesador (modificables antes de process).
     * @return Referencia a las opciones.
     */
    PPOptions& options() noexcept { return m_opts; }

    /**
     * @brief Acceso de solo lectura a las opciones.
     * @return Referencia constante a las opciones.
     */
    const PPOptions& options() const noexcept { return m_opts; }

    /**
     * @brief Acceso al motor de diagnosticos para consultar errores tras process().
     * @return Referencia constante al motor de diagnosticos.
     */
    const DiagnosticEngine& diagnostics() const noexcept { return m_diag; }

    /**
     * @brief Acceso a la tabla de macros (para inspeccionar macros definidas).
     * @return Referencia constante a la tabla de macros.
     */
    const MacroTable& macro_table() const noexcept { return m_macros; }

    /**
     * @brief Registra un resolvedor de includes personalizado.
     *
     * Si no se registra ninguno, el preprocesador intentara leer los archivos
     * directamente del sistema de archivos usando las include_paths configuradas.
     *
     * @param resolver Funcion resolutora.
     */
    void set_include_resolver(IncludeResolver resolver);

    /**
     * @brief Define una macro externamente (equivalente a -DNAME o -DNAME=val).
     * @param def Cadena en formato "NAME" o "NAME=value".
     */
    void add_define(const std::string& def);

    /**
     * @brief Registra un conjunto de macros a precargar.
     * @param kind  Si `value` es texto, ruta de fichero o comando.
     * @param value Texto de las directivas, ruta o comando.
     */
    void add_predef_source(PredefKind kind, const std::string& value);

private:
    PPOptions          m_opts;          ///< Opciones del preprocesador
    DiagnosticEngine   m_diag;          ///< Motor de diagnosticos
    MacroTable         m_macros;        ///< Tabla de macros activa
    IncludeResolver    m_resolver;      ///< Resolvedor de includes
    std::unordered_set<std::string> m_include_guard_once; ///< Archivos con #pragma once
    std::vector<std::string>        m_include_stack;      ///< Pila de archivos en proceso
    /**
     * @brief TODOS los ficheros incluidos, no solo los que estan en curso.
     *
     * La pila de arriba sirve para `#pragma once` y se vacia al salir de cada
     * uno.  Esto es otra cosa: la lista de lo que este fuente LEYO, que es lo
     * que un cache necesita para saber cuando deja de valer.  Sin ella, tocar
     * un fichero incluido no invalida nada y se sirve un artefacto viejo -- un
     * fallo que no da error, da un resultado que ya no corresponde al codigo.
     */
    std::vector<std::string>        m_included_files;

    // Contadores para macros dinamicas
    uint32_t m_counter; ///< Valor actual de __COUNTER__

    /**
     * @brief Precarga los conjuntos de macros de `predef_sources`.
     *
     * Cada uno se procesa como un fuente normal y su salida se descarta: lo que
     * interesa es el efecto lateral sobre la tabla de macros.  Pasar por el
     * pipeline completo, en vez de trocear nombre=valor, es lo que permite que
     * un volcado real -- con macros funcion y valores de varios tokens -- entre
     * intacto.
     */
    void load_predefines();

    /**
     * @brief Evalua el AST de un bloque y produce texto de salida.
     * @param block   Bloque a evaluar.
     * @param output  Flujo de salida donde se escribe el resultado.
     */
    void eval_block(const BlockNode& block, std::string& output);

    /**
     * @brief Evalua un nodo individual del AST.
     * @param node   Nodo a evaluar.
     * @param output Flujo de salida.
     */
    void eval_node(const ASTNode& node, std::string& output);

    /**
     * @brief Evalua un nodo de texto plano expandiendo las macros definidas.
     * @param node   TextNode a evaluar.
     * @param output Flujo de salida.
     */
    void eval_text(const TextNode& node, std::string& output);

    /**
     * @brief Evalua una directiva #define y registra la macro.
     * @param node DefineNode a procesar.
     */
    void eval_define(const DefineNode& node);

    /**
     * @brief Evalua una directiva #include e incrusta el contenido del archivo.
     * @param node   IncludeNode a procesar.
     * @param output Flujo de salida donde se inserta el archivo incluido.
     */
    void eval_include(const IncludeNode& node, std::string& output);

    /**
     * @brief Evalua un bloque condicional #if / #ifdef / #ifndef.
     * @param node   IfBlockNode a procesar.
     * @param output Flujo de salida.
     */
    void eval_if_block(const IfBlockNode& node, std::string& output);

    /**
     * @brief Evalua un bloque #foreach ... #endforeach.
     * @param node   ForeachNode a procesar.
     * @param output Flujo de salida.
     */
    void eval_foreach(const ForeachNode& node, std::string& output);

    /**
     * @brief Evalua un bloque #repeat ... #endrepeat.
     * @param node   RepeatNode a procesar.
     * @param output Flujo de salida.
     */
    void eval_repeat(const RepeatNode& node, std::string& output);

    /**
     * @brief Evalua una directiva #array y registra el array en la tabla.
     * @param node ArrayDefNode con nombre e items.
     */
    void eval_array_def(const ArrayDefNode& node);

    /**
     * @brief Evalua una directiva #exec, ejecuta el comando y define la macro.
     * @param node ExecDirNode con nombre de variable y comando.
     */
    void eval_exec(const ExecDirNode& node);

    /**
     * @brief Evalua una directiva #assert: emite error si la condicion es falsa.
     * @param node AssertNode con la condicion y el mensaje opcional.
     */
    void eval_assert(const AssertNode& node);

    /**
     * @brief Evalua un bloque #macro ... #endmacro y registra la macro funcion.
     * @param node MacroBlockNode con nombre, parametros y cuerpo multilinea.
     */
    void eval_macro(const MacroBlockNode& node);

    /**
     * @brief Evalua una directiva #set: asigna o modifica una variable.
     *
     * Evalua la expresion RHS con PPEvaluator (para enteros) o la trata como
     * cadena si consiste en un unico token STRING. Aplica el operador compuesto
     * sobre el valor actual de la variable si ya existe.
     *
     * @param node SetDirNode con nombre, operador y expresion.
     */
    void eval_set(const SetDirNode& node);

    /**
     * @brief Evalua la condicion de una directiva #if o #elif.
     * @param tokens Tokens de la condicion.
     * @param loc    Ubicacion de la directiva para errores.
     * @return true si la condicion es verdadera (distinta de 0).
     */
    bool eval_condition(const std::vector<PPToken>& tokens,
                        const SourceLocation& loc);

    /**
     * @brief Resuelve e intenta leer el archivo de un #include.
     * @param node IncludeNode con la ruta a resolver.
     * @return Contenido del archivo, o cadena vacia si no se encontro.
     */
    std::string resolve_include(const IncludeNode& node);

    /**
     * @brief Actualiza las macros dinamicas __FILE__, __LINE__, __COUNTER__.
     * @param file Nombre de archivo actual.
     * @param line Numero de linea actual.
     */
    void update_dynamic_macros(const std::string& file, uint32_t line);
};

} // namespace vpp
