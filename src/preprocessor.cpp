/**
 * @file preprocessor.cpp
 * @brief Implementacion del preprocesador principal vpp.
 */

#include "preprocessor/preprocessor.h"
#include "preprocessor/pp_lexer.h"
#include "preprocessor/pp_parser.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cstdio>
#ifdef _WIN32
    #define VPP_POPEN  _popen
    #define VPP_PCLOSE _pclose
#else
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

std::string Preprocessor::process(const std::string& source,
                                   const std::string& filename) {
    // registrar predefiniciones desde las opciones
    for (const auto& def : m_opts.predefines) {
        add_define(def);
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

    // fase 1: tokenizar
    PPLexer lexer(source, filename, m_diag, m_opts.lexer);
    auto tokens = lexer.tokenize();

    // fase 2: parsear el AST
    PPParser parser(std::move(tokens), m_diag);
    auto ast = parser.parse();

    if (m_diag.has_errors()) return "";

    // fase 3: evaluar el AST produciendo el texto de salida
    std::string output;
    output.reserve(source.size());

    const auto* block = static_cast<const BlockNode*>(ast.get());
    eval_block(*block, output);

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
                    ? n.loc.file
                    : m_include_stack.back();
                m_include_guard_once.insert(guard_key);
            }
            // pragmas desconocidos se ignoran silenciosamente
            break;
        }

        case NodeKind::LINE_DIR: {
            const auto& n = static_cast<const LineDirNode&>(node);
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

void Preprocessor::eval_text(const TextNode& node, std::string& output) {
    if (!m_opts.expand_macros) {
        // sin expansion: emitir el texto tal cual
        for (const auto& t : node.tokens) {
            output += t.value;
        }
        return;
    }
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

void Preprocessor::eval_include(const IncludeNode& node, std::string& output) {
    // verificar que el archivo no haya sido incluido con #pragma once
    if (m_include_guard_once.count(node.path)) return;

    // #import tiene semantica auto-once: registrar el modulo antes de procesarlo
    // para que importaciones circulares o duplicadas sean silenciosamente ignoradas
    if (node.is_import) {
        m_include_guard_once.insert(node.path);
    }

    // resolver el contenido del archivo
    std::string content = resolve_include(node);
    if (content.empty() && m_diag.has_errors()) return;

    if (m_opts.emit_line_markers) {
        output += "#line 1 \"";
        output += node.path;
        output += "\"\n";
    }

    // preprocesar el archivo incluido recursivamente
    Preprocessor sub_pp(m_opts.emit_line_markers
        ? DiagCallback([this](const Diagnostic& d){ /* reenviar al padre */
            // no podemos capturar m_diag directamente, se usa el motor compartido
          })
        : DiagCallback(nullptr));
    // compartir la misma tabla de macros (las macros del include son visibles)
    // para eso usamos el mismo preprocesador pero procesamos el include inline
    // con el motor de diagnosticos compartido

    // procesamiento inline del include compartiendo el estado de macros
    PPLexer lexer(content, node.path, m_diag, m_opts.lexer);
    auto tokens = lexer.tokenize();
    PPParser parser(std::move(tokens), m_diag);
    auto ast = parser.parse();
    if (!m_diag.has_errors()) {
        const auto* block = static_cast<const BlockNode*>(ast.get());
        // empujar la ruta del include a la pila para que #pragma once pueda identificar el archivo
        m_include_stack.push_back(node.path);
        eval_block(*block, output);
        m_include_stack.pop_back();
    }

    if (m_opts.emit_line_markers) {
        output += "#line ";
        output += std::to_string(node.loc.line + 1);
        output += " \"";
        output += node.loc.file;
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
                cond = m_macros.is_defined(node.condition[0].value);
            } else {
                // buscar el primer IDENT en la condicion
                for (const auto& t : node.condition) {
                    if (t.type == PPTokenType::IDENT) {
                        cond = m_macros.is_defined(t.value);
                        break;
                    }
                }
            }
            break;
        case IfBlockNode::Kind::IFNDEF:
            if (!node.condition.empty() &&
                node.condition[0].type == PPTokenType::IDENT) {
                cond = !m_macros.is_defined(node.condition[0].value);
            } else {
                for (const auto& t : node.condition) {
                    if (t.type == PPTokenType::IDENT) {
                        cond = !m_macros.is_defined(t.value);
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

bool Preprocessor::eval_condition(const std::vector<PPToken>& tokens,
                                   const SourceLocation& loc) {
    PPEvaluator eval(m_macros, m_diag);
    return eval.evaluate(tokens, loc) != 0;
}

std::string Preprocessor::resolve_include(const IncludeNode& node) {
    // helper: leer un archivo dado su ruta absoluta o relativa como std::string
    auto read_file = [](const std::filesystem::path& p) -> std::string {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) return "";
        return std::string((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
    };

    // si hay un resolvedor personalizado, usarlo primero
    if (m_resolver) {
        std::string content = m_resolver(node.loc.file, node.path, node.is_system);
        if (!content.empty()) return content;
        // si el resolvedor falla para #import, continuar con la busqueda por defecto
        if (!node.is_import) {
            m_diag.error(node.loc, "archivo incluido no encontrado: " + node.path);
            return "";
        }
    }

    // para #import: buscar primero en import_paths con extensiones .vph, .vel, sin extension
    if (node.is_import) {
        static const char* kImportExts[] = { ".vph", ".vel", "" };
        for (const auto& imp_dir : m_opts.import_paths) {
            std::filesystem::path base_imp = std::filesystem::path(imp_dir) / node.path;
            for (const char* ext : kImportExts) {
                std::filesystem::path candidate = base_imp;
                if (ext[0] != '\0') candidate += ext;  // agrega la extension
                std::string content = read_file(candidate);
                if (!content.empty()) return content;
            }
        }
        // #import no encontrado: error no fatal (el modulo puede no existir aun)
        m_diag.error(node.loc, "#import: modulo no encontrado: " + node.path);
        return "";
    }

    // resolucion estandar para #include:
    // primero intentar relativo al archivo actual si es include local
    if (!node.is_system && !node.loc.file.empty()) {
        std::filesystem::path base = std::filesystem::path(node.loc.file).parent_path();
        std::string content = read_file(base / node.path);
        if (!content.empty()) return content;
    }

    // buscar en las rutas de inclusion configuradas
    for (const auto& inc_path : m_opts.include_paths) {
        std::string content = read_file(std::filesystem::path(inc_path) / node.path);
        if (!content.empty()) return content;
    }

    // no encontrado: emitir error
    m_diag.error(node.loc, "archivo incluido no encontrado: " + node.path);
    return "";
}

void Preprocessor::eval_array_def(const ArrayDefNode& node) {
    // registrar el array en la tabla de macros con su nombre y lista de items
    m_macros.define_array(node.name, node.items);
}

void Preprocessor::eval_exec(const ExecDirNode& node) {
    // expandir el comando antes de ejecutarlo (permite usar macros en el nombre)
    PPLexer cmd_lex(node.command, node.loc.file, m_diag, m_opts.lexer);
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

    // evaluar RHS — primero expandir macros en la expresion
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

void Preprocessor::update_dynamic_macros(const std::string& file, uint32_t line) {
    SourceLocation l("<builtin>", 0, 0);
    // actualizar __FILE__
    m_macros.define(MacroDef("__FILE__",
        {PPToken(PPTokenType::STRING, "\"" + file + "\"", l)}, l, true));
    // actualizar __LINE__
    m_macros.define(MacroDef("__LINE__",
        {PPToken(PPTokenType::NUMBER, std::to_string(line), l)}, l, true));
    // actualizar __COUNTER__
    m_macros.define(MacroDef("__COUNTER__",
        {PPToken(PPTokenType::NUMBER, std::to_string(m_counter++), l)}, l, true));
}

} // namespace vpp
