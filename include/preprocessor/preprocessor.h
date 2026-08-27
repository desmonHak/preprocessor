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
#include "pp_capabilities.h"
#include "pp_include.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
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

    /**
     * @brief Con que preguntar por las capacidades del compilador de destino.
     *
     * Los operadores `__has_builtin`, `__has_attribute` y companeros preguntan
     * por lo que sabe hacer un COMPILADOR, no por una macro, asi que la unica
     * respuesta honesta es preguntarselo a el.  Aqui va la orden con la que
     * invocarlo; vpp le anade la ruta de un fichero de consulta.
     *
     *     "gcc -E -P -x c"        "clang -E -P -x c++"      "cl /EP"
     *
     * Se apunta al binario EXACTO por el mismo motivo que en predef_sources:
     * en una maquina conviven varios compiladores -- el de MinGW, el de MSVC,
     * el de WSL -- y cada uno responde distinto a la misma pregunta.
     *
     * Vacio significa que no hay a quien preguntar y esos operadores valen 0.
     */
    std::string capabilities_command;

    /**
     * @brief Donde se recuerda entre ejecuciones lo que contesta el compilador.
     *
     * Lo que se guarda son hechos sobre un COMPILADOR -- que tenga
     * `__builtin_expect`, que macros predefine -- no sobre un proyecto, asi que
     * por omision la memoria es del usuario y se comparte entre todos sus
     * proyectos en vez de calentarse una vez en cada uno.
     *
     * Vacio significa el sitio por omision (ver user_cache_dir); para
     * desactivarla del todo esta use_cache.
     */
    std::string cache_dir;

    /**
     * @brief Si se recuerda algo entre ejecuciones.
     *
     * A false, cada ejecucion vuelve a preguntarle todo al compilador.  Sirve
     * para medir el coste real de las consultas y para descartar la memoria
     * cuando se sospecha de ella.
     */
    bool                 use_cache;
    bool                 emit_line_markers; ///< Emite marcadores #line tras cada #include

    /** @brief Constructor con valores por defecto. */
    PPOptions()
        : expand_macros(true)
        , track_includes(true)
        , use_cache(true)
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

    /**
     * @brief Un fichero de inclusion en curso.
     *
     * Guarda la ruta RESUELTA y no la que se escribio en la directiva.  Son
     * cosas distintas y confundirlas rompe dos casos: un `#include "vecino.h"`
     * dentro de una cabecera que aparecio por una ruta de busqueda tiene que
     * buscar al lado de ELLA, y un `#include_next` necesita saber en que
     * directorio de la lista aparecio la actual para reanudar en el siguiente.
     */
    struct IncludeFrame {
        std::string path;             ///< Ruta con la que se abrio
        int         search_index = -1; ///< Directorio de la lista donde aparecio,
                                       ///< o -1 si fue relativo al que incluye
    };

    std::vector<IncludeFrame>       m_include_stack;      ///< Ficheros en curso
    IncludeSearch                   m_search;             ///< Busqueda de ficheros
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

    /**
     * @brief Remapeo de posicion instalado por `#line`.
     *
     * `#line N "f"` dice que la linea SIGUIENTE es la N del fichero f.  No se
     * guarda el desplazamiento sino el par (linea fisica de origen, linea
     * reportada de origen), y el resto se calcula por diferencia: asi varios
     * `#line` seguidos componen bien y no hay que arrastrar un acumulado.
     */
    bool        m_line_remap_active = false; ///< Hay un #line en vigor
    uint32_t    m_line_base_phys    = 0;     ///< Linea fisica donde empieza
    uint32_t    m_line_base_rep     = 0;     ///< Linea reportada equivalente
    /**
     * @brief Fichero reportado por `#line`, ya compartido; nulo si no se dio.
     *
     * Es un puntero al pozo y no una cadena porque mapped_position se llama por
     * linea: internar alli tomaria el cerrojo constantemente.  Se interna una
     * vez, al procesar la directiva.
     */
    const std::string* m_line_remap_file = nullptr;

    /**
     * @brief Traduce una posicion real a la que debe reportarse.
     * @param real Posicion tal y como esta en el fichero.
     * @return Posicion despues de aplicar el `#line` en vigor, si lo hay.
     */
    SourceLocation mapped_position(const SourceLocation& real) const;

    /**
     * @brief Quien contesta por el compilador de destino.
     *
     * Es un componente aparte, con su propio estado y su propia memoria; el
     * preprocesador solo le pasa la pregunta.
     */
    CapabilityOracle m_capabilities;

    // Contadores para macros dinamicas
    uint32_t m_counter; ///< Valor actual de __COUNTER__

    /**
     * @brief Responde a un operador de prueba de caracteristicas.
     *
     * Reparte segun quien sepa la respuesta: `__has_include` lo contesta vpp
     * con sus propias rutas de busqueda, y el resto va al CapabilityOracle,
     * que es quien habla con el compilador y recuerda lo que dice.
     *
     * @param op  Operador, p.ej. "__has_builtin".
     * @param arg Texto entre parentesis.
     * @return 1 o 0; 0 tambien cuando no hay compilador al que preguntar.
     */
    int64_t resolve_capability(const std::string& op, const std::string& arg);

    /**
     * @brief Dice si vpp puede contestar a un operador del compilador.
     *
     * No es un detalle.  Las bibliotecas estandar se protegen con
     *
     *     #ifndef __has_builtin
     *     #  define __has_builtin(x) 0
     *     #endif
     *
     * de modo que un preprocesador que no se declare capaz acaba con una macro
     * que responde 0 a TODO, y con ella las cabeceras se van por ramas que su
     * compilador no usa.  Asi paso: libc++ moria en un `#error` porque el shim
     * habia anulado todas las consultas.
     *
     * La respuesta no se deduce de la forma del nombre: se PREGUNTA.
     * `__has_include` lo contesta vpp, porque pregunta por el sistema de
     * ficheros.  El resto necesita un compilador que ademas LO TENGA -- el
     * juego de operadores cambia con el lenguaje y con la version -- y sin
     * compilador la respuesta correcta es que no, porque deja que el shim
     * instale su 0 y el codigo tome su rama de reserva.
     *
     * @param name Identificador tal y como aparece en la directiva.
     * @return true si vpp puede contestar a ese operador.
     */
    bool can_answer_capability(const std::string& name);

    /**
     * @brief Directorio efectivo de la memoria entre ejecuciones.
     *
     * Resuelve las dos formas de no decir nada -- desactivada del todo, o
     * activada sin decir donde -- en un solo sitio, para que los dos que la
     * usan no puedan interpretarlas distinto.
     *
     * @return La ruta, o vacia si no hay memoria en disco.
     */
    std::string effective_cache_dir() const;

    /**
     * @brief Dice si un nombre esta definido a efectos de `#ifdef` y `defined`.
     *
     * Une los dos motivos por los que puede estarlo: ser una macro, o ser un
     * operador de prueba de caracteristicas que vpp sabe contestar.  Existe
     * para que las dos condiciones se decidan en un solo sitio y no puedan
     * separarse por descuido en alguno de los usos.
     *
     * @param name Identificador tal y como aparece en la directiva.
     * @return true si esta definido.
     */
    bool name_is_defined(const std::string& name);

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
    ResolvedInclude resolve_include(const IncludeNode& node);

};

} // namespace vpp
