/**
 * @file preprocessor.cpp
 * @brief Implementacion del preprocesador principal vpp.
 */

#include "preprocessor/preprocessor.h"
#include "preprocessor/pp_lexer.h"
#include "preprocessor/pp_parser.h"
#include "preprocessor/pp_dialect.h"
#include "preprocessor/pp_system.h"
#include "preprocessor/pp_compiler_id.h"
#include "preprocessor/pp_command_cache.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <iostream>
#ifdef _WIN32
    // Dispositivo nulo con el que se alimenta la entrada de los comandos, para
    // que uno que lea de stdin termine en vez de quedarse colgado.
    static const char* const kNullDevice = "NUL";
    #define VPP_POPEN  _popen
    #define VPP_PCLOSE _pclose
#else
    static const char* const kNullDevice = "/dev/null";
    #define VPP_POPEN  popen
    #define VPP_PCLOSE pclose
#endif

namespace vpp {

/* --- constructores -------------------------------------------------------- */

Preprocessor::Preprocessor()
    : m_diag()
    , m_macros(m_diag)
    , m_counter(0)
{
    // aplicar predefiniciones de opciones por defecto
}

Preprocessor::Preprocessor(DiagCallback cb)
    : m_diag(std::move(cb))
    , m_macros(m_diag)
    , m_counter(0)
{}

/* --- API publica ---------------------------------------------------------- */

void Preprocessor::set_include_resolver(IncludeResolver resolver) {
    m_resolver = std::move(resolver);
}

void Preprocessor::add_define(const std::string& def) {
    size_t eq = def.find('=');
    SourceLocation cli_loc("<cli>", 0, 0);
    if (eq == std::string::npos) {
        // solo flag: NAME
        m_macros.define_flag(def);
    } else {
        // NAME=value
        std::string name  = def.substr(0, eq);
        std::string value = def.substr(eq + 1);
        // crear un token con el valor
        PPToken val_tok(PPTokenType::IDENT, value, cli_loc);
        MacroDef md(name, {val_tok}, cli_loc);
        m_macros.define(std::move(md));
    }
}

void Preprocessor::add_predef_source(PredefKind kind,
                                     const std::string& value) {
    m_opts.predef_sources.push_back(PredefSource{kind, value});
}


bool Preprocessor::can_answer_capability(const std::string& name) {
    // `__has_include` no necesita compilador: se contesta con las rutas de
    // busqueda, que vpp ya tiene.
    if (name == "__has_include" || name == "__has_include_next" ||
        name == "__has_embed") {
        return true;
    }

    // Para el resto hace falta un compilador...
    if (m_opts.capabilities_command.empty()) return false;

    // ...y que ESE compilador lo tenga.  No vale con reconocer la forma del
    // nombre: el juego de operadores cambia con el lenguaje y con la version,
    // y dar por bueno uno que no existe lleva a preguntar algo que el
    // compilador rechaza y a tomar una rama que el nunca habria tomado.  Asi
    // fallaba `__has_cpp_attribute` al procesar cabeceras de C.
    return m_capabilities.is_known(name);
}

std::string Preprocessor::effective_cache_dir() const {
    if (!m_opts.use_cache) return {};
    if (!m_opts.cache_dir.empty()) return m_opts.cache_dir;
    return user_cache_dir();
}

std::vector<std::string> Preprocessor::user_included_files() const {
    std::vector<std::string> out;
    out.reserve(m_included_files.size());
    for (const auto& f : m_included_files) {
        if (!m_system_includes.count(f)) out.push_back(f);
    }
    return out;
}

namespace {

/**
 * @brief Indica si un token no aporta nada al texto de salida.
 *
 * Se mira el CONTENIDO y no solo el tipo: el lexer junta una racha de lineas en
 * blanco en un unico token de texto, asi que exigir que el tipo fuese
 * WHITESPACE o NEWLINE dejaba fuera justo el caso normal -- las lineas en blanco
 * que toda cabecera tiene entre el comentario de cabecera y su guarda.
 *
 * @param tok Token a examinar.
 * @return true si es solo espacio en blanco.
 */
bool is_blank_token(const PPToken& tok) {
    if (tok.type == PPTokenType::WHITESPACE ||
        tok.type == PPTokenType::NEWLINE) {
        return true;
    }
    if (tok.type != PPTokenType::TEXT) return false;
    for (const unsigned char c : tok.value) {
        if (!std::isspace(c)) return false;
    }
    return true;
}

} // namespace

void Preprocessor::record_include_guard(const std::string& path,
                                        const BlockNode& block) {
    // Ya anotado por una visita anterior.
    if (m_include_guards.count(path)) return;

    // Un unico condicional envolviendolo todo, y fuera de el solo blancos.
    const IfBlockNode* guard = nullptr;
    for (const auto& child : block.children) {
        if (child->kind == NodeKind::IF_BLOCK) {
            const auto* ib = static_cast<const IfBlockNode*>(child.get());
            if (!guard && ib->variant == IfBlockNode::Kind::IFNDEF) {
                guard = ib;
                continue;
            }
            return;   // un segundo condicional: no es una guarda
        }
        if (child->kind != NodeKind::TEXT) return;
        const auto* text = static_cast<const TextNode*>(child.get());
        for (const auto& tok : text->tokens) {
            if (!is_blank_token(tok)) return;  // texto de verdad fuera
        }
    }

    // La condicion tiene que ser un solo nombre; `#ifndef A && B` no vale.
    if (!guard || guard->condition.size() != 1 ||
        guard->condition[0].type != PPTokenType::IDENT) {
        return;
    }
    const std::string& name = guard->condition[0].value;

    // Y lo primero que hace el cuerpo, definir ESE nombre.  Si no, entrar al
    // fichero una segunda vez si tendria efectos.
    const auto* body = static_cast<const BlockNode*>(guard->then_block.get());
    if (!body) return;
    for (const auto& child : body->children) {
        if (child->kind == NodeKind::TEXT) {
            const auto* text = static_cast<const TextNode*>(child.get());
            bool blank = true;
            for (const auto& tok : text->tokens) {
                if (!is_blank_token(tok)) { blank = false; break; }
            }
            if (blank) continue;
            return;
        }
        if (child->kind == NodeKind::DEFINE) {
            const auto* def = static_cast<const DefineNode*>(child.get());
            if (def->name == name) m_include_guards.emplace(path, name);
        }
        return;
    }
}

bool Preprocessor::name_is_defined(const std::string& name) {
    return m_macros.is_defined(name) ||
           (is_capability_operator(name) && can_answer_capability(name));
}

int64_t Preprocessor::resolve_capability(const std::string& op,
                                         const std::string& arg) {
    // `__has_include` lo contesta vpp POR SI MISMO.  Pregunta por si una ruta
    // se resuelve, y eso se sabe con las MISMAS rutas de busqueda que se usan
    // para incluir; delegarlo en el compilador daria una respuesta sobre otro
    // juego de rutas, que no es la pregunta que se hizo.
    if (op == "__has_include" || op == "__has_include_next") {
        std::string ruta = arg;
        bool sistema = false;
        if (ruta.size() >= 2 && ruta.front() == '<' && ruta.back() == '>') {
            ruta = ruta.substr(1, ruta.size() - 2);
            sistema = true;
        } else if (ruta.size() >= 2 && ruta.front() == '"' &&
                                       ruta.back()  == '"') {
            ruta = ruta.substr(1, ruta.size() - 2);
        }
        if (ruta.empty()) return 0;

        // Se pregunta directamente al buscador, sin pasar por el preprocesador:
        // que el fichero no exista es la RESPUESTA que se esta pidiendo, no un
        // error, y por esta via no se emite ningun diagnostico.
        const std::string desde = m_include_stack.empty()
                                    ? std::string()
                                    : m_include_stack.back().path;
        return m_search.locate(ruta, sistema, desde).found ? 1 : 0;
    }

    /* `__has_embed` tampoco necesita compilador: pregunta por un recurso, y eso
     * se sabe con las mismas rutas.  Devuelve TRES valores, no dos -- 1 si esta
     * y tiene contenido, 2 si esta y esta vacio, 0 si no esta -- porque un
     * recurso vacio no se puede incrustar igual que uno con datos y quien
     * pregunta necesita distinguirlo. */
    if (op == "__has_embed") {
        std::string ruta = arg;
        bool sistema = false;
        if (ruta.size() >= 2 && ruta.front() == '<' && ruta.back() == '>') {
            ruta = ruta.substr(1, ruta.size() - 2);
            sistema = true;
        } else if (ruta.size() >= 2 && ruta.front() == '"' &&
                                       ruta.back()  == '"') {
            ruta = ruta.substr(1, ruta.size() - 2);
        }
        if (ruta.empty()) return 0;

        const std::string desde = m_include_stack.empty()
                                    ? std::string()
                                    : m_include_stack.back().path;
        const ResolvedInclude situado = m_search.locate(ruta, sistema, desde);
        if (!situado.found) return 0;   // __STDC_EMBED_NOT_FOUND__

        std::string datos;
        if (!read_file(situado.path, datos)) return 0;
        return datos.empty() ? 2 : 1;   // __STDC_EMBED_EMPTY__ / _FOUND__
    }

    // El resto pregunta por el compilador, y de eso se encarga el oraculo.  La
    // orden ya se le paso al arrancar el proceso.
    return m_capabilities.query(op, arg);
}

void Preprocessor::load_predefines() {
    for (const auto& src : m_opts.predef_sources) {
        std::string text;
        std::string label;

        switch (src.kind) {
            case PredefKind::Text:
                text  = src.value;
                label = "<predef>";
                break;

            case PredefKind::File: {
                label = src.value;
                std::ifstream ifs(src.value, std::ios::binary);
                if (!ifs) {
                    m_diag.error(SourceLocation(src.value, 0, 0),
                        "no se pudo leer el fichero de macros predefinidas: "
                        + src.value);
                    continue;
                }
                text.assign((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
                break;
            }

            case PredefKind::Command: {
                label = "<" + src.value + ">";

                // La memoria de la salida importa mas aqui que en ningun otro
                // sitio: este volcado no se pide unas cuantas veces sino UNA
                // POR INVOCACION de vpp, asi que compilar N ficheros son N
                // procesos lanzados para obtener exactamente lo mismo.
                const CommandOutputCache cache(
                    effective_cache_dir(), CompilerId::fingerprint(src.value));
                if (cache.load(text)) break;

                // La entrada estandar del hijo se ata al dispositivo nulo.
                //
                // El caso de uso principal es precisamente un comando que LEE
                // de stdin -- `gcc -dM -E -` es el ejemplo canonico -- y sin
                // esto hereda el stdin de vpp y se queda esperando un EOF que
                // no llega nunca: el proceso se cuelga indefinidamente en vez
                // de fallar.  Se envuelve en parentesis para que la
                // redireccion afecte al comando entero aunque traiga tuberias.
                std::string cmd = "(" + src.value + ") < " + kNullDevice;

                FILE* pipe = VPP_POPEN(cmd.c_str(), "r");
                if (!pipe) {
                    m_diag.error(SourceLocation(label, 0, 0),
                        "no se pudo ejecutar el comando de macros "
                        "predefinidas: " + src.value);
                    continue;
                }
                char buf[4096];
                while (std::fgets(buf, sizeof(buf), pipe)) text += buf;
                const int status = VPP_PCLOSE(pipe);

                // Solo se recuerda lo que salio BIEN.  Guardar la salida de una
                // invocacion fallida dejaria escrito un problema pasajero como
                // si fuera lo que ese compilador predefine, y el error
                // sobreviviria a su causa en todas las ejecuciones siguientes.
                if (status == 0) cache.store(text);
                break;
            }
        }

        if (text.empty()) continue;

        // Se procesa como un fuente cualquiera y la salida se tira: lo que
        // importa es lo que quede en la tabla de macros.  Asi entran intactas
        // las macros funcion y los valores de varios tokens, que es justo lo
        // que trae un volcado real y lo que un nombre=valor no puede
        // representar.
        PPLexer  lexer(text, label, m_diag, m_opts.lexer);
        PPParser parser(lexer.tokenize(), m_diag);
        auto ast = parser.parse();

        std::string descartado;
        eval_block(static_cast<const BlockNode&>(*ast), descartado);
    }
}

std::string Preprocessor::process(const std::string& source,
                                   const std::string& filename) {
    // Se le pasa al oraculo con QUIEN tiene que hablar antes de nada.  Hacerlo
    // aqui y no en la primera consulta importa: hay preguntas -- si un operador
    // existe siquiera -- que se hacen antes que ninguna consulta de valor, y
    // hasta entonces el oraculo diria que no hay compilador.
    // El buscador de inclusiones se monta UNA vez, con las rutas ya fijadas.
    // Rehacerlo en cada inclusion copiaba la lista entera de rutas cada vez.
    m_search = IncludeSearch(m_opts.include_paths, m_opts.system_include_paths,
                             m_opts.import_paths);

    m_capabilities.set_cache_dir(effective_cache_dir());
    m_capabilities.set_command(m_opts.capabilities_command);

    // Primero los conjuntos precargados y despues los -D sueltos, para que un
    // -D pueda pisar lo que traiga el volcado: es mas especifico.
    load_predefines();

    for (const auto& def : m_opts.predefines) {
        add_define(def);
    }

    // Y por ultimo lo que se quita: `-U` existe para cancelar lo que traiga el
    // volcado del compilador o el propio vpp, asi que va despues de todo lo que
    // define.
    for (const auto& name : m_opts.undefines) {
        m_macros.undef(name);
    }

    // definir macros de fecha y hora
    {
        SourceLocation bl("<builtin>", 0, 0);
        std::time_t now = std::time(nullptr);
        char date_buf[32], time_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%b %d %Y", std::localtime(&now));
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now));
        m_macros.define(MacroDef("__DATE__",
            {PPToken(PPTokenType::STRING, std::string("\"") + date_buf + "\"", bl)},
            bl, true));
        m_macros.define(MacroDef("__TIME__",
            {PPToken(PPTokenType::STRING, std::string("\"") + time_buf + "\"", bl)},
            bl, true));
    }

    //   tokenizar
    /* El dialecto se toma del propio fichero si lo declara, y es SUYO: no se
     * hereda ni se propaga a lo que incluya.  Sin eso, un fuente en Python no
     * podria incluir una cabecera de C. */
    LexerOptions lex_opts = m_opts.lexer;
    apply_dialect_line(source, lex_opts, filename, m_diag);

    m_sources.add(filename, source);
    PPLexer lexer(source, filename, m_diag, lex_opts);
    auto tokens = lexer.tokenize();

    //   parsear el AST
    PPParser parser(std::move(tokens), m_diag);
    auto ast = parser.parse();

    if (m_diag.has_errors()) return "";

    //   evaluar el AST produciendo el texto de salida
    std::string output;
    output.reserve(source.size());

    // El fichero de partida entra en la pila como cualquier otro.  No es un
    // adorno: sin su marco, todo lo que pregunta "donde estoy" -- el vecino de
    // un `#include "..."`, el de un `__has_include("...")`, la clave de un
    // `#pragma once` -- se queda sin respuesta justo en el nivel de arriba, que
    // es donde mas se nota.  Con el, la pila nunca esta vacia mientras se
    // evalua y el caso especial desaparece.
    m_include_stack.push_back(IncludeFrame{filename, -1});

    const auto* block = static_cast<const BlockNode*>(ast.get());
    eval_block(*block, output);
    m_include_stack.pop_back();

    return output;
}

std::string Preprocessor::process_file(const std::string& filepath) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        SourceLocation l(filepath, 0, 0);
        m_diag.error(l, "no se puede abrir el archivo: " + filepath);
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return process(content, filepath);
}

/* --- evaluacion del AST --------------------------------------------------- */

void Preprocessor::eval_block(const BlockNode& block, std::string& output) {
    for (const auto& child : block.children) {
        eval_node(*child, output);
        if (m_diag.has_errors() && m_diag.error_count() > 20) {
            // no continuar tras demasiados errores
            break;
        }
    }
}

void Preprocessor::eval_node(const ASTNode& node, std::string& output) {
    switch (node.kind) {
        case NodeKind::BLOCK:
            eval_block(static_cast<const BlockNode&>(node), output);
            break;

        case NodeKind::TEXT:
            eval_text(static_cast<const TextNode&>(node), output);
            break;

        case NodeKind::DEFINE:
            eval_define(static_cast<const DefineNode&>(node));
            break;

        case NodeKind::UNDEF: {
            const auto& n = static_cast<const UndefNode&>(node);
            m_macros.undef(n.name);
            break;
        }

        case NodeKind::INCLUDE:
            eval_include(static_cast<const IncludeNode&>(node), output);
            break;
        case NodeKind::EMBED:
            eval_embed(static_cast<const EmbedNode&>(node), output);
            break;

        case NodeKind::IF_BLOCK:
            eval_if_block(static_cast<const IfBlockNode&>(node), output);
            break;

        case NodeKind::ERROR_DIR: {
            const auto& n = static_cast<const ErrorDirNode&>(node);
            m_diag.error(n.loc, n.message);
            break;
        }

        case NodeKind::WARNING_DIR: {
            const auto& n = static_cast<const WarningDirNode&>(node);
            m_diag.warning(n.loc, n.message);
            break;
        }

        case NodeKind::PRAGMA: {
            const auto& n = static_cast<const PragmaNode&>(node);
            // procesar pragmas conocidos
            if (!n.args.empty() && n.args[0].value == "once") {
                // usar la pila de includes para identificar el archivo actual con certeza
                const std::string& guard_key = m_include_stack.empty()
                    ? n.loc.file()
                    : m_include_stack.back().path;
                m_include_guard_once.insert(guard_key);
            }
            // pragmas desconocidos se ignoran silenciosamente
            break;
        }

        case NodeKind::LINE_DIR: {
            const auto& n = static_cast<const LineDirNode&>(node);

            // Instalar el remapeo: la linea SIGUIENTE a la directiva pasa a ser
            // la que indica.  Antes esto solo re-emitia el marcador y no movia
            // __LINE__ ni __FILE__, con lo que la directiva no servia para lo
            // que existe: que el codigo generado apunte a su fuente original.
            m_line_remap_active = true;
            m_line_base_phys    = n.loc.line + 1;
            m_line_base_rep     = n.line_num;
            if (!n.filename.empty())
                m_line_remap_file = intern_file_name(n.filename);

            // emitir marcador de linea si esta habilitado
            if (m_opts.emit_line_markers) {
                output += "#line ";
                output += std::to_string(n.line_num);
                if (!n.filename.empty()) {
                    output += " \"";
                    output += n.filename;
                    output += "\"";
                }
                output += '\n';
            }
            break;
        }

        case NodeKind::FOREACH_BLOCK:
            eval_foreach(static_cast<const ForeachNode&>(node), output);
            break;

        case NodeKind::REPEAT_BLOCK:
            eval_repeat(static_cast<const RepeatNode&>(node), output);
            break;

        case NodeKind::ARRAY_DEF:
            eval_array_def(static_cast<const ArrayDefNode&>(node));
            break;

        case NodeKind::EXEC_DIR:
            eval_exec(static_cast<const ExecDirNode&>(node));
            break;

        case NodeKind::SET_DIR:
            eval_set(static_cast<const SetDirNode&>(node));
            break;

        case NodeKind::ASSERT_DIR:
            eval_assert(static_cast<const AssertNode&>(node));
            break;

        case NodeKind::MACRO_BLOCK:
            eval_macro(static_cast<const MacroBlockNode&>(node));
            break;
    }
}

SourceLocation Preprocessor::mapped_position(const SourceLocation& real) const {
    if (!m_line_remap_active) return real;

    SourceLocation out = real;
    if (m_line_remap_file) out.file_name = m_line_remap_file;
    // diferencia respecto al punto donde se instalo el remapeo
    out.line = m_line_base_rep + (real.line - m_line_base_phys);
    return out;
}

void Preprocessor::eval_text(const TextNode& node, std::string& output) {
    if (!m_opts.expand_macros) {
        // sin expansion: emitir el texto tal cual
        for (const auto& t : node.tokens) {
            output += t.value;
        }
        return;
    }
    // Posicion en curso, de donde salen __FILE__ y __LINE__.  Se fija por nodo
    // porque es la unidad que corresponde a una linea del fuente, y pasa por
    // el remapeo para que un `#line` en vigor se note.
    const SourceLocation pos = mapped_position(node.loc);
    m_macros.set_source_position(pos.file(), pos.line);

    // expansion de macros: los tokens IDENT son candidatos a expansion
    auto expanded = m_macros.expand(node.tokens, node.loc);
    for (const auto& t : expanded) {
        output += t.value;
    }
}

void Preprocessor::eval_define(const DefineNode& node) {
    if (node.is_function) {
        MacroDef def(node.name, node.params, node.is_variadic,
                     node.body, node.loc);
        m_macros.define(std::move(def));
    } else {
        MacroDef def(node.name, node.body, node.loc);
        m_macros.define(std::move(def));
    }
}


void Preprocessor::eval_embed(const EmbedNode& node, std::string& output) {
    // El recurso se busca con las MISMAS rutas que un #include: es lo que
    // espera quien ya tiene su proyecto configurado.
    const std::string desde = m_include_stack.empty()
                                ? node.loc.file()
                                : m_include_stack.back().path;
    const ResolvedInclude situado =
        m_search.locate(node.path, node.is_system, desde);

    if (!situado.found) {
        m_diag.error(node.loc, "recurso de #embed no encontrado: " + node.path);
        return;
    }

    std::string datos;
    if (!read_file(situado.path, datos)) {
        m_diag.error(node.loc, "no se pudo leer el recurso: " + situado.path);
        return;
    }

    // `limit` se evalua como una expresion porque puede ser una macro.
    if (node.has_limit) {
        const int64_t tope = eval_int(node.limit_tokens, node.loc);
        if (tope <= 0) datos.clear();
        else if (static_cast<uint64_t>(tope) < datos.size()) {
            datos.resize(static_cast<std::size_t>(tope));
        }
    }

    /* Un recurso vacio NO emite ni prefijo ni sufijo, solo lo que diga
     * `if_empty`.  Es lo que hace gcc y esta medido: tiene sentido, porque el
     * prefijo suele ser el separador que une los datos con lo de al lado, y sin
     * datos sobra. */
    /* Lo que se emita ocupa la LINEA de la directiva, con su salto al final.
     *
     * Sin el, los datos se pegaban al texto de la linea siguiente -- salia
     * `97,98,99tres` -- que no es solo feo: cambia lo que el compilador lee.
     * Una directiva que no produce nada si puede desaparecer sin dejar linea,
     * pero esta produce. */
    if (datos.empty()) {
        if (node.has_if_empty) {
            output += expand_to_text(node.if_empty, node.loc);
            output += '\n';
        }
        return;
    }

    if (!node.prefix.empty()) output += expand_to_text(node.prefix, node.loc);

    // Los bytes se emiten como enteros separados por comas, que es lo que pide
    // el estandar para poder inicializar un array de unsigned char.
    output.reserve(output.size() + datos.size() * 4);
    for (std::size_t i = 0; i < datos.size(); ++i) {
        if (i) output += ',';
        output += std::to_string(
            static_cast<unsigned>(static_cast<unsigned char>(datos[i])));
    }

    if (!node.suffix.empty()) output += expand_to_text(node.suffix, node.loc);
    output += '\n';
}
void Preprocessor::eval_include(const IncludeNode& node, std::string& output) {
    // Ruta a traves de macros: `#include CABECERA`.  El parser dejo los tokens
    // sin resolver porque no ve la tabla; se expanden aqui y el resultado tiene
    // que formar "..." o <...>, igual que si se hubiera escrito literalmente.
    std::string ruta      = node.path;
    std::string ruta_resuelta;
    bool        es_sistema = node.is_system;

    if (!node.path_tokens.empty()) {
        auto expandidos = m_macros.expand(node.path_tokens, node.loc);

        std::string texto;
        for (const auto& t : expandidos) {
            if (t.type == PPTokenType::PP_EOF) break;
            if (t.type == PPTokenType::WHITESPACE ||
                t.type == PPTokenType::NEWLINE) continue;
            texto += t.value;
        }

        if (texto.size() >= 2 && texto.front() == '"' && texto.back() == '"') {
            ruta       = texto.substr(1, texto.size() - 2);
            es_sistema = false;
        } else if (texto.size() >= 2 && texto.front() == '<' &&
                                        texto.back()  == '>') {
            ruta       = texto.substr(1, texto.size() - 2);
            es_sistema = true;
        } else {
            m_diag.error(node.loc,
                "#include requiere \"archivo\" o <archivo>; tras expandir "
                "macros quedo: " + texto);
            return;
        }
    }

    /* Se AVERIGUA cual es el fichero antes de decidir nada, pero sin leerlo.
     *
     * La identidad de una inclusion es su ruta RESUELTA, no la escrita: un
     * `#include "_types.h"` desde dos directorios distintos nombra dos ficheros
     * distintos.  Usar la escrita hacia que lo recordado de uno se aplicara al
     * otro y se saltara el que no debia. */
    const ResolvedInclude situado = locate_include(node, ruta, es_sistema);
    if (!situado.found) {
        // Que no aparezca lo diagnostica resolve_include mas abajo, con su
        // mensaje y su ubicacion; aqui solo se sabe que no hay nada que saltar.
        ruta_resuelta.clear();
    } else {
        ruta_resuelta = situado.path;
    }
    const std::string& clave = ruta_resuelta.empty() ? ruta : ruta_resuelta;

    /* Optimizacion de inclusion multiple: si de una visita anterior se sabe que
     * este fichero es una guarda envolviendolo todo, y esa guarda sigue
     * definida, incluirlo no puede producir nada.  Se sale sin abrirlo siquiera.
     * Se comprueba que la macro SIGA definida porque un `#undef` por medio
     * vuelve a hacer significativo el contenido. */
    {
        const auto it = m_include_guards.find(clave);
        if (it != m_include_guards.end() && m_macros.is_defined(it->second)) {
            return;
        }
    }

    // verificar que el archivo no haya sido incluido con #pragma once
    if (m_include_guard_once.count(clave)) return;

    // #import tiene semantica auto-once: registrar el modulo antes de procesarlo
    // para que importaciones circulares o duplicadas sean silenciosamente ignoradas
    if (node.is_import) {
        m_include_guard_once.insert(clave);
    }

    /* Se lee el fichero que YA se localizo arriba, sin volver a buscarlo.
     *
     * Repetir la busqueda no era gratis ni de lejos: recorrer la lista de rutas
     * cuesta una consulta al sistema de ficheros por candidato, y buscando dos
     * veces se pagaban todas dos veces.  Medido con VTune, esas consultas eran
     * el 35% del tiempo. */
    ResolvedInclude hallado;
    if (situado.found) {
        hallado = situado;
        if (!read_file(hallado.path, hallado.content)) {
            // Estaba al mirar y no esta al abrir.
            m_diag.error(node.loc, "archivo incluido no encontrado: " + ruta);
            return;
        }
    } else {
        // Sin localizar: o hay un resolvedor del usuario, o es un #import, o
        // de verdad no aparece.  De todo eso se encarga resolve_include, que
        // ademas emite el diagnostico que corresponda.
        IncludeNode resuelto(node.loc, ruta, es_sistema, node.is_import);
        resuelto.is_next = node.is_next;
        hallado = resolve_include(resuelto);
        if (!hallado.found) return;
    }
    const std::string& content = hallado.content;

    if (m_opts.emit_line_markers) {
        output += "#line 1 \"";
        output += ruta;
        output += "\"\n";
    }

    // El fichero incluido se procesa EN LINEA, con este mismo preprocesador:
    // asi las macros que defina quedan visibles para quien lo incluyo, que es
    // justo lo que un #include significa.
    // Se lexa con la ruta RESUELTA, no con la escrita: es la que acabara en la
    // ubicacion de cada token, y de ella cuelgan tanto los mensajes como la
    // busqueda de un vecino desde este fichero.
    // Cada fichero declara su propio dialecto; el de quien lo incluye no cuenta.
    LexerOptions lex_opts = m_opts.lexer;
    apply_dialect_line(content, lex_opts,
                       hallado.path.empty() ? ruta : hallado.path, m_diag);

    m_sources.add(hallado.path.empty() ? ruta : hallado.path, content);
    PPLexer lexer(content, hallado.path.empty() ? ruta : hallado.path,
                  m_diag, lex_opts);
    auto tokens = lexer.tokenize();
    PPParser parser(std::move(tokens), m_diag);
    auto ast = parser.parse();
    if (!m_diag.has_errors()) {
        const auto* block = static_cast<const BlockNode*>(ast.get());
        /* Se apila la ruta RESUELTA junto con el directorio de la lista en el
         * que aparecio.  Lo primero permite que un `#include "vecino.h"` de
         * dentro busque al lado de este fichero y no del de partida; lo segundo
         * es lo que necesita un `#include_next` para reanudar en el siguiente
         * directorio en vez de volver a encontrarse a si mismo. */
        /* Un fichero cuenta como de sistema si aparecio en un directorio de
         * sistema O si lo incluye otro que ya lo era.  Lo segundo hace falta:
         * una cabecera del sistema tira de otras por rutas propias, y todas
         * siguen siendo ajenas.  Es la regla de cc para `-MM`. */
        const bool es_sistema_dep =
            situado.system_dir ||
            (!m_include_stack.empty() && m_include_stack.back().system);
        if (es_sistema_dep) m_system_includes.insert(clave);

        m_include_stack.push_back(
            IncludeFrame{hallado.path.empty() ? ruta : hallado.path,
                         hallado.search_index, es_sistema_dep});
        /* Y ademas se APUNTA, que no es lo mismo: la pila se vacia al salir y
         * esto tiene que sobrevivir a todo el preproceso.  Es lo que permite
         * que un cache sepa que este fuente depende de aquel fichero, y es lo
         * que se emite como lista de dependencias.  Se apunta la ruta RESUELTA:
         * la escrita no le sirve a nadie de fuera, porque depende de desde donde
         * se incluyera.  Se comprueba antes de anadir porque el mismo se puede
         * incluir desde varios sitios y la lista es para consultarla, no para
         * contar. */
        if (std::find(m_included_files.begin(), m_included_files.end(), clave) ==
            m_included_files.end())
            m_included_files.push_back(clave);
        /* Se anota su guarda ANTES de evaluarlo: al evaluar se define, y desde
         * la siguiente inclusion el fichero ya se puede saltar entero. */
        record_include_guard(clave, *block);
        eval_block(*block, output);
        m_include_stack.pop_back();
    }

    if (m_opts.emit_line_markers) {
        output += "#line ";
        output += std::to_string(node.loc.line + 1);
        output += " \"";
        output += node.loc.file();
        output += "\"\n";
    }
}

void Preprocessor::eval_if_block(const IfBlockNode& node, std::string& output) {
    bool branch_taken = false;

    // evaluar la condicion principal
    bool cond = false;
    switch (node.variant) {
        case IfBlockNode::Kind::IF:
            cond = eval_condition(node.condition, node.loc);
            break;
        case IfBlockNode::Kind::IFDEF:
            if (!node.condition.empty() &&
                node.condition[0].type == PPTokenType::IDENT) {
                cond = name_is_defined(node.condition[0].value);
            } else {
                // buscar el primer IDENT en la condicion
                for (const auto& t : node.condition) {
                    if (t.type == PPTokenType::IDENT) {
                        cond = name_is_defined(t.value);
                        break;
                    }
                }
            }
            break;
        case IfBlockNode::Kind::IFNDEF:
            if (!node.condition.empty() &&
                node.condition[0].type == PPTokenType::IDENT) {
                cond = !name_is_defined(node.condition[0].value);
            } else {
                for (const auto& t : node.condition) {
                    if (t.type == PPTokenType::IDENT) {
                        cond = !name_is_defined(t.value);
                        break;
                    }
                }
            }
            break;
    }

    if (cond) {
        branch_taken = true;
        if (node.then_block) {
            eval_block(static_cast<const BlockNode&>(*node.then_block), output);
        }
    }

    // evaluar cadena de #elif si la rama principal fue falsa
    if (!branch_taken) {
        for (const auto& branch : node.elif_chain) {
            bool elif_cond = eval_condition(branch.condition, node.loc);
            if (elif_cond) {
                branch_taken = true;
                if (branch.body) {
                    eval_block(static_cast<const BlockNode&>(*branch.body), output);
                }
                break;
            }
        }
    }

    // evaluar #else si ninguna rama anterior fue tomada
    if (!branch_taken && node.else_block) {
        eval_block(static_cast<const BlockNode&>(*node.else_block), output);
    }
}

void Preprocessor::eval_foreach(const ForeachNode& node, std::string& output) {
    if (!node.body) return;

    // resolver la lista de items: array nombrado o lista inline
    const std::vector<std::string>* items_ptr = nullptr;
    std::vector<std::string>        inline_items;

    if (!node.array_name.empty()) {
        // iterar sobre un array definido con #array
        items_ptr = m_macros.get_array(node.array_name);
        if (!items_ptr) {
            m_diag.error(node.loc,
                "#foreach: array no definido: " + node.array_name);
            return;
        }
    } else {
        inline_items = node.items;
        items_ptr    = &inline_items;
    }

    size_t foreach_idx = 0;
    for (const auto& item : *items_ptr) {
        // definir la variable de iteracion con el valor actual del item
        SourceLocation foreach_loc = node.loc;
        PPToken val_tok(PPTokenType::IDENT, item, foreach_loc);
        MacroDef iter_def(node.var_name, {val_tok}, foreach_loc);
        m_macros.define(std::move(iter_def));

        // definir variable de indice opcional (#foreach VAR, IDX in ...)
        if (!node.index_var.empty()) {
            PPToken idx_tok(PPTokenType::NUMBER, std::to_string(foreach_idx), foreach_loc);
            MacroDef idx_def(node.index_var, {idx_tok}, foreach_loc, true);
            m_macros.define(std::move(idx_def));
        }

        eval_block(static_cast<const BlockNode&>(*node.body), output);
        ++foreach_idx;
    }

    // eliminar variables de iteracion al terminar
    m_macros.undef(node.var_name);
    if (!node.index_var.empty()) m_macros.undef(node.index_var);
}

void Preprocessor::eval_repeat(const RepeatNode& node, std::string& output) {
    if (!node.body) return;

    // evaluar la expresion de conteo
    PPEvaluator eval(m_macros, m_diag);
    int64_t count = eval.evaluate(node.count_expr, node.loc);
    if (count < 0) count = 0;
    if (count > 100000) {
        m_diag.error(node.loc, "#repeat: conteo excesivo (maximo 100000)");
        return;
    }

    for (int64_t i = 0; i < count; ++i) {
        // definir __REPEAT_INDEX__ con el indice actual
        SourceLocation rl = node.loc;
        std::string idx_str = std::to_string(i);
        PPToken idx_tok(PPTokenType::NUMBER, idx_str, rl);
        MacroDef idx_def("__REPEAT_INDEX__", {idx_tok}, rl, true);
        m_macros.define(std::move(idx_def));

        eval_block(static_cast<const BlockNode&>(*node.body), output);
    }
    m_macros.undef("__REPEAT_INDEX__");
}


int64_t Preprocessor::eval_int(const std::vector<PPToken>& tokens,
                               const SourceLocation& loc) {
    // Mismo evaluador que las condiciones; lo unico que cambia es que aqui
    // interesa el VALOR y no si es cero.  Tenerlo aparte evita que quien
    // necesite un numero tenga que reconstruir el evaluador por su cuenta.
    const SourceLocation pos = mapped_position(loc);
    m_macros.set_source_position(pos.file(), pos.line);
    PPEvaluator eval(m_macros, m_diag,
        [this](const std::string& op, const std::string& arg) {
            return resolve_capability(op, arg);
        },
        [this](const std::string& name) {
            return can_answer_capability(name);
        });
    return eval.evaluate(tokens, loc);
}

std::string Preprocessor::expand_to_text(const std::vector<PPToken>& tokens,
                                         const SourceLocation& loc) {
    if (tokens.empty()) return {};

    std::string out;
    if (!m_opts.expand_macros) {
        for (const auto& t : tokens) out += t.value;
        return out;
    }
    const SourceLocation pos = mapped_position(loc);
    m_macros.set_source_position(pos.file(), pos.line);
    for (const auto& t : m_macros.expand(tokens, loc)) out += t.value;
    return out;
}
bool Preprocessor::eval_condition(const std::vector<PPToken>& tokens,
                                   const SourceLocation& loc) {
    // una condicion puede usar __LINE__, asi que tambien necesita la posicion
    const SourceLocation pos = mapped_position(loc);
    m_macros.set_source_position(pos.file(), pos.line);
    PPEvaluator eval(m_macros, m_diag,
        [this](const std::string& op, const std::string& arg) {
            return resolve_capability(op, arg);
        },
        [this](const std::string& name) {
            return can_answer_capability(name);
        });
    return eval.evaluate(tokens, loc) != 0;
}


ResolvedInclude Preprocessor::locate_include(const IncludeNode& node,
                                             const std::string& path,
                                             bool is_system) {
    ResolvedInclude r;

    // Con un resolvedor del usuario no hay nada que localizar: lo que sirva no
    // tiene por que existir en el disco.  Se deja sin situar y la identidad
    // vuelve a ser la ruta escrita, que es lo unico que hay.
    if (m_resolver || node.is_import) return r;

    // El buscador se monta una vez al arrancar (ver process): rehacerlo aqui
    // copiaba la lista de rutas en CADA inclusion.

    const std::string desde = m_include_stack.empty() ? node.loc.file()
                                                      : m_include_stack.back().path;
    int inicio = 0;
    if (node.is_next && !m_include_stack.empty()) {
        inicio = m_include_stack.back().search_index + 1;
    }
    return m_search.locate(path, is_system, desde, inicio);
}
ResolvedInclude Preprocessor::resolve_include(const IncludeNode& node) {
    ResolvedInclude r;

    // si hay un resolvedor personalizado, usarlo primero
    if (m_resolver) {
        std::string content = m_resolver(node.loc.file(), node.path, node.is_system);
        if (!content.empty()) {
            // Lo que sirve un resolvedor del usuario no tiene por que existir en
            // el disco, asi que no hay directorio del que colgar un vecino ni
            // indice de busqueda que reanudar.
            r.found   = true;
            r.content = std::move(content);
            r.path    = node.path;
            return r;
        }
        // si el resolvedor falla para #import, continuar con la busqueda por defecto
        if (!node.is_import) {
            m_diag.error(node.loc, "archivo incluido no encontrado: " + node.path);
            return r;
        }
    }

    // La busqueda en si vive en IncludeSearch; aqui solo se decide QUE pedirle
    // y que hacer si no aparece.
    // Idem: el buscador ya esta montado.

    if (node.is_import) {
        r = m_search.resolve_import(node.path);
        if (!r.found) {
            // no fatal: el modulo puede no existir todavia
            m_diag.error(node.loc, "#import: modulo no encontrado: " + node.path);
        }
        return r;
    }

    // El fichero desde el que se busca es el que esta en curso, con su ruta
    // RESUELTA.  La que trae el nodo es la que se escribio en la directiva, y
    // el vecino de una cabecera esta al lado de donde ESA cabecera aparecio.
    const std::string desde = m_include_stack.empty() ? node.loc.file()
                                                      : m_include_stack.back().path;

    // Un #include_next reanuda justo despues del directorio en el que aparecio
    // el fichero actual; un #include normal empieza por el principio.
    int inicio = 0;
    if (node.is_next && !m_include_stack.empty()) {
        inicio = m_include_stack.back().search_index + 1;
    }

    r = m_search.resolve(node.path, node.is_system, desde, inicio);
    if (!r.found) {
        m_diag.error(node.loc, "archivo incluido no encontrado: " + node.path);
    }
    return r;
}

void Preprocessor::eval_array_def(const ArrayDefNode& node) {
    // registrar el array en la tabla de macros con su nombre y lista de items
    m_macros.define_array(node.name, node.items);
}

void Preprocessor::eval_exec(const ExecDirNode& node) {
    // expandir el comando antes de ejecutarlo (permite usar macros en el nombre)
    PPLexer cmd_lex(node.command, node.loc.file(), m_diag, m_opts.lexer);
    auto cmd_toks  = cmd_lex.tokenize();
    auto expanded  = m_macros.expand(cmd_toks, node.loc);
    std::string cmd;
    for (const auto& t : expanded) {
        if (t.type == PPTokenType::PP_EOF) break;
        if (t.type == PPTokenType::WHITESPACE) cmd += ' ';
        else cmd += t.value;
    }
    while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t'))
        cmd.pop_back();

    // ejecutar el comando y capturar stdout
    FILE* pipe = VPP_POPEN(cmd.c_str(), "r");
    if (!pipe) {
        m_diag.error(node.loc, "#exec: no se pudo ejecutar: " + cmd);
        return;
    }
    char buf[256];
    std::string output;
    while (std::fgets(buf, sizeof(buf), pipe)) output += buf;
    VPP_PCLOSE(pipe);

    // eliminar salto de linea final
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    // almacenar la salida como macro
    SourceLocation l = node.loc;
    PPToken val_tok(PPTokenType::IDENT, output, l);
    MacroDef def(node.var_name, {val_tok}, l);
    m_macros.define(std::move(def));
}

void Preprocessor::eval_set(const SetDirNode& node) {
    SourceLocation l = node.loc;
    const std::string& name = node.name;
    const std::string& op   = node.op;

    // helper: leer valor entero actual de la variable (0 si no existe)
    auto current_int = [&]() -> int64_t {
        const MacroDef* existing = m_macros.get(name);
        if (!existing || existing->body.empty()) return 0;
        // expandir por si el cuerpo referencia otras macros
        auto expanded = m_macros.expand(existing->body, l);
        for (const auto& t : expanded) {
            if (t.type == PPTokenType::NUMBER) {
                try { return std::stoll(t.value, nullptr, 0); }
                catch (...) { return 0; }
            }
            if (t.type == PPTokenType::IDENT) {
                try { return std::stoll(t.value, nullptr, 0); }
                catch (...) { return 0; }
            }
        }
        return 0;
    };

    // helper: helper para crear una MacroDef de variable con valor entero
    auto make_int_def = [&](int64_t v) {
        PPToken tok(PPTokenType::NUMBER, std::to_string(v), l);
        MacroDef def(name, {tok}, l);
        def.is_variable = true;
        return def;
    };

    // incremento / decremento sin RHS
    if (op == "++" || op == "--") {
        int64_t cur = current_int();
        m_macros.define(make_int_def(op == "++" ? cur + 1 : cur - 1));
        return;
    }

    // evaluar RHS: primero expandir macros en la expresion
    auto rhs_expanded = m_macros.expand(node.expr, l);

    // detectar si es una asignacion de cadena: unico token STRING
    auto non_ws = [&]() -> const PPToken* {
        for (const auto& t : rhs_expanded)
            if (t.type != PPTokenType::WHITESPACE && t.type != PPTokenType::PP_EOF)
                return &t;
        return nullptr;
    };

    // para "=" con valor STRING: almacenar como macro de cadena
    if (op == "=") {
        const PPToken* first = non_ws();
        if (first && first->type == PPTokenType::STRING) {
            MacroDef def(name, {*first}, l);
            def.is_variable = true;
            m_macros.define(std::move(def));
            return;
        }
    }

    // evaluar como expresion entera para todos los demas casos
    PPEvaluator eval(m_macros, m_diag);
    int64_t rhs_val = eval.evaluate(rhs_expanded, l);

    int64_t result;
    if      (op == "=")   result = rhs_val;
    else if (op == "+=")  result = current_int() + rhs_val;
    else if (op == "-=")  result = current_int() - rhs_val;
    else if (op == "*=")  result = current_int() * rhs_val;
    else if (op == "/=")  {
        if (rhs_val == 0) { m_diag.error(l, "#set: division por cero"); return; }
        result = current_int() / rhs_val;
    }
    else if (op == "%=")  {
        if (rhs_val == 0) { m_diag.error(l, "#set: modulo por cero"); return; }
        result = current_int() % rhs_val;
    }
    else if (op == "&=")  result = current_int() & rhs_val;
    else if (op == "|=")  result = current_int() | rhs_val;
    else if (op == "^=")  result = current_int() ^ rhs_val;
    else if (op == "<<=") result = (int64_t)((uint64_t)current_int() << (uint64_t)rhs_val);
    else if (op == ">>=") result = (int64_t)((uint64_t)current_int() >> (uint64_t)rhs_val);
    else {
        m_diag.error(l, "#set: operador desconocido: " + op);
        return;
    }

    m_macros.define(make_int_def(result));
}

void Preprocessor::eval_assert(const AssertNode& node) {
    PPEvaluator eval(m_macros, m_diag);
    int64_t val = eval.evaluate(node.condition, node.loc);
    if (val == 0) {
        std::string msg = node.message.empty()
            ? "asercion fallida en preprocesado"
            : node.message;
        m_diag.error(node.loc, "#assert: " + msg);
    }
}

void Preprocessor::eval_macro(const MacroBlockNode& node) {
    // registrar como macro funcion con el cuerpo multilinea
    MacroDef def(node.name, node.params, node.is_variadic,
                 node.body, node.loc);
    m_macros.define(std::move(def));
}


} // namespace vpp
