/**
 * @file pp_parser.cpp
 * @brief Implementacion del parser del preprocesador vpp.
 */

#include "preprocessor/pp_parser.h"
#include <stdexcept>
#include <sstream>

namespace vpp {

/* --- utilidad: concatena tokens en una cadena ----------------------------- */
static std::string tokens_to_string(const std::vector<PPToken>& toks) {
    std::string s;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::WHITESPACE) s += ' ';
        else                                    s += t.value;
    }
    return s;
}

/* --- PPParser ------------------------------------------------------------- */

PPParser::PPParser(std::vector<PPToken> tokens, DiagnosticEngine& diag)
    : m_toks(std::move(tokens))
    , m_pos(0)
    , m_diag(diag)
{}

NodePtr PPParser::parse() {
    SourceLocation root_loc = m_toks.empty()
        ? SourceLocation()
        : m_toks[0].loc;
    return parse_block();
}

/* --- acceso al flujo de tokens -------------------------------------------- */

const PPToken& PPParser::cur() const {
    static PPToken eof(PPTokenType::PP_EOF, "", {});
    if (m_pos >= m_toks.size()) return eof;
    return m_toks[m_pos];
}

const PPToken& PPParser::peek(int offset) const {
    static PPToken eof(PPTokenType::PP_EOF, "", {});
    size_t idx = m_pos + static_cast<size_t>(offset);
    if (idx >= m_toks.size()) return eof;
    return m_toks[idx];
}

PPToken PPParser::consume() {
    if (m_pos >= m_toks.size()) {
        return PPToken(PPTokenType::PP_EOF, "", {});
    }
    return std::move(m_toks[m_pos++]);
}

PPToken PPParser::expect(PPTokenType t, const char* msg) {
    if (cur().type != t) {
        m_diag.error(cur().loc, msg);
        // devolver un token sintetico para continuar el parse
        return PPToken(t, "", cur().loc);
    }
    return consume();
}

bool PPParser::check(PPTokenType t) const {
    return cur().type == t;
}

void PPParser::skip_blanks() {
    while (check(PPTokenType::NEWLINE) || check(PPTokenType::WHITESPACE)) {
        consume();
    }
}

std::vector<PPToken> PPParser::consume_rest_of_line() {
    std::vector<PPToken> toks;
    // saltar espacios iniciales de la linea
    while (check(PPTokenType::WHITESPACE)) consume();
    while (!check(PPTokenType::NEWLINE) && !check(PPTokenType::PP_EOF)) {
        toks.push_back(consume());
    }
    if (check(PPTokenType::NEWLINE)) consume(); // consumir el NEWLINE
    return toks;
}

/* --- construccion del AST ------------------------------------------------- */

NodePtr PPParser::parse_block() {
    SourceLocation l = cur().loc;
    auto block = std::make_unique<BlockNode>(l);

    while (true) {
        // fin de archivo: terminar el bloque
        if (check(PPTokenType::PP_EOF)) break;

        // directivas de cierre de bloque: no consumir, devolver al padre
        if (check(PPTokenType::HASH)) {
            // verificar si es una directiva de cierre: #endif, #else, #elif,
            // #endforeach, #endrepeat
            size_t saved = m_pos;
            consume(); // consume '#'
            // saltar espacios opcionales entre '#' y el nombre
            while (check(PPTokenType::WHITESPACE)) consume();

            if (cur().type == PPTokenType::IDENT) {
                const std::string& name = cur().value;
                if (name == "endif"      || name == "else"     ||
                    name == "elif"       || name == "endforeach"||
                    name == "endrepeat"  || name == "endmacro") {
                    // restaurar posicion: el padre consumira la directiva
                    m_pos = saved;
                    break;
                }
            }
            // no era una directiva de cierre: restaurar y parsear como directiva
            m_pos = saved;
        }

        NodePtr node = parse_node();
        if (node) {
            block->children.push_back(std::move(node));
        }
    }

    return block;
}

NodePtr PPParser::parse_node() {
    // saltar lineas en blanco
    if (check(PPTokenType::NEWLINE)) {
        // preservar la linea en blanco como TextNode de un solo NEWLINE
        PPToken nl = consume();
        std::vector<PPToken> toks;
        toks.push_back(nl);
        return std::make_unique<TextNode>(nl.loc, std::move(toks));
    }

    if (check(PPTokenType::HASH)) {
        return parse_directive();
    }

    // cualquier otro token es parte de una linea de texto
    return parse_text_line();
}

NodePtr PPParser::parse_text_line() {
    SourceLocation l = cur().loc;
    std::vector<PPToken> toks;

    // Profundidad de parentesis.  Una llamada a macro funcion puede repartirse
    // en varias lineas -- es habitual en cabeceras reales, p.ej. el __REDIRECT
    // de glibc:
    //     extern int __REDIRECT (fscanf, (FILE *__restrict __stream,
    //                                     const char *__restrict __format, ...),
    //                            __isoc23_fscanf) ...
    // Si se cortara el nodo en cada salto de linea, la expansion nunca veria el
    // parentesis de cierre y la llamada fallaria con "numero incorrecto de
    // argumentos".  Mientras haya parentesis abiertos se sigue leyendo.
    int depth = 0;

    for (;;) {
        while (!check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)  &&
               !check(PPTokenType::HASH)) {
            if (check(PPTokenType::LPAREN))      ++depth;
            else if (check(PPTokenType::RPAREN) && depth > 0) --depth;
            toks.push_back(consume());
        }

        // consumir el NEWLINE que cierra la linea
        if (check(PPTokenType::NEWLINE)) {
            toks.push_back(consume());
        }

        // Se continua SOLO con parentesis pendientes.  El corte en HASH y en
        // PP_EOF se mantiene: una directiva dentro de los argumentos de una
        // macro no esta definida por el estandar, y sin esos frenos un
        // parentesis que nunca cierra se comeria el resto del fichero.
        if (depth <= 0 ||
            check(PPTokenType::PP_EOF) ||
            check(PPTokenType::HASH)) {
            break;
        }
    }

    return std::make_unique<TextNode>(l, std::move(toks));
}

NodePtr PPParser::parse_directive() {
    SourceLocation hash_loc = cur().loc;
    consume(); // consume '#'

    // saltar espacios opcionales entre '#' y el nombre de directiva
    while (check(PPTokenType::WHITESPACE)) consume();

    if (cur().type != PPTokenType::IDENT) {
        // directiva nula o desconocida: consumir hasta el fin de linea
        if (!check(PPTokenType::NEWLINE) && !check(PPTokenType::PP_EOF)) {
            m_diag.warning(hash_loc, "directiva de preprocesador desconocida o nula");
            consume_rest_of_line();
        } else if (check(PPTokenType::NEWLINE)) {
            consume();
        }
        return nullptr;
    }

    std::string dir_name = cur().value;
    consume(); // consume el nombre de la directiva

    // despachar segun el nombre
    if (dir_name == "define")     return parse_define(hash_loc);
    if (dir_name == "undef")      return parse_undef(hash_loc);
    if (dir_name == "include")    return parse_include(hash_loc);
    if (dir_name == "if")         return parse_if_block(hash_loc, IfBlockNode::Kind::IF);
    if (dir_name == "ifdef")      return parse_if_block(hash_loc, IfBlockNode::Kind::IFDEF);
    if (dir_name == "ifndef")     return parse_if_block(hash_loc, IfBlockNode::Kind::IFNDEF);
    if (dir_name == "error")      return parse_error(hash_loc);
    if (dir_name == "warning")    return parse_warning(hash_loc);
    if (dir_name == "pragma")     return parse_pragma(hash_loc);
    if (dir_name == "line")       return parse_line(hash_loc);
    if (dir_name == "foreach")    return parse_foreach(hash_loc);
    if (dir_name == "repeat")     return parse_repeat(hash_loc);
    if (dir_name == "array")      return parse_array(hash_loc);
    if (dir_name == "exec")       return parse_exec(hash_loc);
    if (dir_name == "set")        return parse_set(hash_loc);
    if (dir_name == "import")     return parse_import_macro(hash_loc);
    if (dir_name == "assert")     return parse_assert(hash_loc);
    if (dir_name == "macro")      return parse_macro(hash_loc);

    // directiva desconocida
    m_diag.warning(hash_loc, "directiva de preprocesador desconocida: #" + dir_name);
    consume_rest_of_line();
    return nullptr;
}

/* --- parsers de directivas especificas ------------------------------------ */

NodePtr PPParser::parse_define(SourceLocation hash_loc) {
    // saltar espacios
    while (check(PPTokenType::WHITESPACE)) consume();

    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#define requiere un nombre de macro");
        consume_rest_of_line();
        return nullptr;
    }

    std::string name = cur().value;
    consume(); // consume el nombre

    std::vector<std::string> params;
    bool is_function = false;
    bool is_variadic = false;

    // verificar si es macro funcion: '(' inmediatamente tras el nombre (sin espacios)
    if (check(PPTokenType::LPAREN)) {
        is_function = true;
        consume(); // consume '('

        // leer parametros
        while (!check(PPTokenType::RPAREN) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            while (check(PPTokenType::WHITESPACE)) consume();
            if (check(PPTokenType::ELLIPSIS)) {
                consume();
                is_variadic = true;
                while (check(PPTokenType::WHITESPACE)) consume();
                break; // '...' debe ser el ultimo parametro
            }
            if (check(PPTokenType::IDENT)) {
                params.push_back(cur().value);
                consume();
                while (check(PPTokenType::WHITESPACE)) consume();
                if (check(PPTokenType::COMMA)) consume();
            } else {
                m_diag.error(cur().loc, "parametro invalido en #define");
                break;
            }
        }
        if (!check(PPTokenType::RPAREN)) {
            m_diag.error(cur().loc, "se esperaba ')' al cerrar parametros de #define");
        } else {
            consume(); // consume ')'
        }
    }

    // leer el cuerpo de la macro (resto de la linea)
    // saltar un espacio inicial del cuerpo si existe
    if (check(PPTokenType::WHITESPACE)) consume();

    std::vector<PPToken> body;
    while (!check(PPTokenType::NEWLINE) && !check(PPTokenType::PP_EOF)) {
        body.push_back(consume());
    }
    if (check(PPTokenType::NEWLINE)) consume();

    // eliminar espacios finales del cuerpo
    while (!body.empty() && body.back().type == PPTokenType::WHITESPACE) {
        body.pop_back();
    }

    return std::make_unique<DefineNode>(hash_loc, std::move(name),
                                        std::move(params), is_function,
                                        is_variadic, std::move(body));
}

NodePtr PPParser::parse_undef(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#undef requiere un nombre de macro");
        consume_rest_of_line();
        return nullptr;
    }

    std::string name = cur().value;
    consume();
    consume_rest_of_line();
    return std::make_unique<UndefNode>(hash_loc, std::move(name));
}

NodePtr PPParser::parse_include(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    std::string path;
    bool is_system = false;

    if (check(PPTokenType::STRING)) {
        // #include "archivo" -> quitar comillas
        std::string raw = cur().value;
        path = raw.substr(1, raw.size() - 2);
        consume();
    } else if (check(PPTokenType::LT)) {
        // #include <archivo> -> escanear hasta '>'
        consume(); // consume '<'
        is_system = true;
        while (!check(PPTokenType::GT) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            path += cur().value;
            consume();
        }
        if (check(PPTokenType::GT)) consume();
    } else if (check(PPTokenType::ANGLE_STRING)) {
        std::string raw = cur().value;
        path = raw.substr(1, raw.size() - 2);
        is_system = true;
        consume();
    } else {
        m_diag.error(cur().loc, "#include requiere \"archivo\" o <archivo>");
        consume_rest_of_line();
        return nullptr;
    }

    consume_rest_of_line();
    return std::make_unique<IncludeNode>(hash_loc, std::move(path), is_system);
}

NodePtr PPParser::parse_if_block(SourceLocation hash_loc,
                                  IfBlockNode::Kind variant) {
    while (check(PPTokenType::WHITESPACE)) consume();

    // leer la condicion principal
    std::vector<PPToken> condition = consume_rest_of_line();

    // parsear el bloque "then"
    NodePtr then_block = parse_block();

    // parsear cadena de #elif y #else
    std::vector<IfBranch> elif_chain;
    NodePtr else_block;

    while (true) {
        if (!check(PPTokenType::HASH)) break;

        size_t saved = m_pos;
        consume(); // consume '#'
        while (check(PPTokenType::WHITESPACE)) consume();

        if (!check(PPTokenType::IDENT)) { m_pos = saved; break; }

        std::string kw = cur().value;

        if (kw == "endif") {
            consume(); // consume "endif"
            consume_rest_of_line();
            goto done; // directiva cerrada correctamente
        }

        if (kw == "elif") {
            consume(); // consume "elif"
            while (check(PPTokenType::WHITESPACE)) consume();
            std::vector<PPToken> elif_cond = consume_rest_of_line();
            NodePtr elif_body = parse_block();
            elif_chain.emplace_back(std::move(elif_cond), false, std::move(elif_body));
            continue;
        }

        if (kw == "else") {
            consume(); // consume "else"
            consume_rest_of_line();
            else_block = parse_block();
            // despues de #else esperamos #endif
            while (check(PPTokenType::WHITESPACE) || check(PPTokenType::NEWLINE)) consume();
            if (check(PPTokenType::HASH)) {
                consume();
                while (check(PPTokenType::WHITESPACE)) consume();
                if (cur().is_ident("endif")) {
                    consume();
                    consume_rest_of_line();
                } else {
                    m_diag.error(cur().loc, "se esperaba #endif");
                }
            } else {
                m_diag.error(cur().loc, "se esperaba #endif al final del bloque condicional");
            }
            goto done;
        }

        // no era una directiva del condicional: restaurar
        m_pos = saved;
        break;
    }

    m_diag.error(hash_loc, "bloque #if sin #endif correspondiente");

done:
    return std::make_unique<IfBlockNode>(hash_loc, variant,
                                          std::move(condition),
                                          std::move(then_block),
                                          std::move(elif_chain),
                                          std::move(else_block));
}

NodePtr PPParser::parse_error(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();
    auto toks = consume_rest_of_line();
    return std::make_unique<ErrorDirNode>(hash_loc, tokens_to_string(toks));
}

NodePtr PPParser::parse_warning(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();
    auto toks = consume_rest_of_line();
    return std::make_unique<WarningDirNode>(hash_loc, tokens_to_string(toks));
}

NodePtr PPParser::parse_pragma(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();
    auto toks = consume_rest_of_line();
    return std::make_unique<PragmaNode>(hash_loc, std::move(toks));
}

NodePtr PPParser::parse_line(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    if (!check(PPTokenType::NUMBER)) {
        m_diag.error(cur().loc, "#line requiere un numero de linea");
        consume_rest_of_line();
        return nullptr;
    }

    uint32_t num = static_cast<uint32_t>(std::stoul(cur().value));
    consume();
    while (check(PPTokenType::WHITESPACE)) consume();

    std::string filename;
    if (check(PPTokenType::STRING)) {
        std::string raw = cur().value;
        filename = raw.substr(1, raw.size() - 2);
        consume();
    }
    consume_rest_of_line();
    return std::make_unique<LineDirNode>(hash_loc, num, std::move(filename));
}

NodePtr PPParser::parse_foreach(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#foreach requiere una variable de iteracion");
        consume_rest_of_line();
        return nullptr;
    }
    std::string var = cur().value;
    consume();

    while (check(PPTokenType::WHITESPACE)) consume();

    // forma opcional: #foreach VAR, IDX in ARRAY
    std::string index_var;
    if (check(PPTokenType::COMMA)) {
        consume(); // consume ','
        while (check(PPTokenType::WHITESPACE)) consume();
        if (!check(PPTokenType::IDENT)) {
            m_diag.error(cur().loc, "#foreach: se esperaba nombre de variable de indice tras ','");
            consume_rest_of_line();
            return nullptr;
        }
        index_var = cur().value;
        consume();
        while (check(PPTokenType::WHITESPACE)) consume();
    }

    // verificar palabra clave 'in'
    if (!cur().is_ident("in")) {
        m_diag.error(cur().loc, "#foreach requiere la palabra clave 'in'");
        consume_rest_of_line();
        return nullptr;
    }
    consume(); // consume 'in'

    while (check(PPTokenType::WHITESPACE)) consume();

    std::string              array_name;
    std::vector<std::string> items;

    if (check(PPTokenType::LPAREN)) {
        // forma inline: #foreach VAR in (item1, item2, ...)
        consume(); // consume '('

        while (!check(PPTokenType::RPAREN) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            while (check(PPTokenType::WHITESPACE)) consume();
            if (check(PPTokenType::RPAREN)) break;
            // leer un item: puede ser ident, numero o cadena
            std::string item;
            while (!check(PPTokenType::COMMA) &&
                   !check(PPTokenType::RPAREN) &&
                   !check(PPTokenType::NEWLINE) &&
                   !check(PPTokenType::PP_EOF)) {
                if (!check(PPTokenType::WHITESPACE)) item += cur().value;
                consume();
            }
            if (!item.empty()) items.push_back(std::move(item));
            if (check(PPTokenType::COMMA)) consume();
        }
        if (check(PPTokenType::RPAREN)) consume();
    } else if (check(PPTokenType::IDENT)) {
        // forma array: #foreach VAR in NOMBRE_ARRAY
        array_name = cur().value;
        consume();
    } else {
        m_diag.error(cur().loc,
            "#foreach requiere una lista entre parentesis o un nombre de array");
        consume_rest_of_line();
        return nullptr;
    }
    consume_rest_of_line();

    // parsear el cuerpo hasta #endforeach
    NodePtr body = parse_block();

    // esperar #endforeach
    if (check(PPTokenType::HASH)) {
        consume();
        while (check(PPTokenType::WHITESPACE)) consume();
        if (cur().is_ident("endforeach")) {
            consume();
            consume_rest_of_line();
        } else {
            m_diag.error(cur().loc, "se esperaba #endforeach");
        }
    } else {
        m_diag.error(hash_loc, "#foreach sin #endforeach correspondiente");
    }

    return std::make_unique<ForeachNode>(hash_loc, std::move(var),
                                          std::move(index_var),
                                          std::move(array_name),
                                          std::move(items), std::move(body));
}

NodePtr PPParser::parse_repeat(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    std::vector<PPToken> count_expr = consume_rest_of_line();

    NodePtr body = parse_block();

    // esperar #endrepeat
    if (check(PPTokenType::HASH)) {
        consume();
        while (check(PPTokenType::WHITESPACE)) consume();
        if (cur().is_ident("endrepeat")) {
            consume();
            consume_rest_of_line();
        } else {
            m_diag.error(cur().loc, "se esperaba #endrepeat");
        }
    } else {
        m_diag.error(hash_loc, "#repeat sin #endrepeat correspondiente");
    }

    return std::make_unique<RepeatNode>(hash_loc,
                                         std::move(count_expr), std::move(body));
}

NodePtr PPParser::parse_array(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    // nombre del array
    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#array requiere un nombre de array");
        consume_rest_of_line();
        return nullptr;
    }
    std::string name = cur().value;
    consume();

    while (check(PPTokenType::WHITESPACE)) consume();

    // lista de items entre parentesis
    if (!check(PPTokenType::LPAREN)) {
        m_diag.error(cur().loc, "#array requiere la lista de items entre parentesis");
        consume_rest_of_line();
        return nullptr;
    }
    consume(); // consume '('

    std::vector<std::string> items;
    while (!check(PPTokenType::RPAREN) &&
           !check(PPTokenType::NEWLINE) &&
           !check(PPTokenType::PP_EOF)) {
        while (check(PPTokenType::WHITESPACE)) consume();
        if (check(PPTokenType::RPAREN)) break;
        // leer un item: concatenar tokens hasta la proxima coma o ')'
        std::string item;
        while (!check(PPTokenType::COMMA) &&
               !check(PPTokenType::RPAREN) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            if (!check(PPTokenType::WHITESPACE)) item += cur().value;
            consume();
        }
        if (!item.empty()) items.push_back(std::move(item));
        if (check(PPTokenType::COMMA)) consume();
    }
    if (check(PPTokenType::RPAREN)) consume();
    consume_rest_of_line();

    return std::make_unique<ArrayDefNode>(hash_loc, std::move(name),
                                           std::move(items));
}

NodePtr PPParser::parse_exec(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    // nombre de la macro donde se guardara la salida
    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#exec requiere el nombre de la macro destino");
        consume_rest_of_line();
        return nullptr;
    }
    std::string var_name = cur().value;
    consume();

    // espacio obligatorio antes del comando
    if (!check(PPTokenType::WHITESPACE)) {
        m_diag.error(cur().loc, "#exec requiere un espacio entre el nombre y el comando");
        consume_rest_of_line();
        return nullptr;
    }
    consume(); // consumir el espacio

    // el resto de la linea es el comando (sin expandir)
    std::string command;
    while (!check(PPTokenType::NEWLINE) && !check(PPTokenType::PP_EOF)) {
        if (check(PPTokenType::WHITESPACE))
            command += ' ';
        else
            command += cur().value;
        consume();
    }
    if (check(PPTokenType::NEWLINE)) consume();

    // eliminar espacios finales
    while (!command.empty() &&
           (command.back() == ' ' || command.back() == '\t'))
        command.pop_back();

    return std::make_unique<ExecDirNode>(hash_loc, std::move(var_name),
                                          std::move(command));
}

NodePtr PPParser::parse_set(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    // nombre de la variable
    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#set requiere un nombre de variable");
        consume_rest_of_line();
        return nullptr;
    }
    std::string var_name = cur().value;
    SourceLocation var_loc = cur().loc;
    consume();

    // saltar espacios entre nombre y operador
    while (check(PPTokenType::WHITESPACE)) consume();

    // operadores de asignacion compuesta y ++/--: detectar token por token
    std::string op;

    if (check(PPTokenType::PLUS)) {
        consume();
        if (check(PPTokenType::PLUS))       { consume(); op = "++"; }  // ++
        else if (check(PPTokenType::ASSIGN)){ consume(); op = "+="; }  // +=
        else { m_diag.error(var_loc, "#set: esperado '+' o '=' despues de '+'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::MINUS)) {
        consume();
        if (check(PPTokenType::MINUS))      { consume(); op = "--"; }  // --
        else if (check(PPTokenType::ASSIGN)){ consume(); op = "-="; }  // -=
        else { m_diag.error(var_loc, "#set: esperado '-' o '=' despues de '-'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::LSHIFT)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "<<="; }
        else { m_diag.error(var_loc, "#set: operador invalido"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::RSHIFT)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = ">>="; }
        else { m_diag.error(var_loc, "#set: operador invalido"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::ASSIGN)) {
        consume();
        op = "=";
    } else if (check(PPTokenType::STAR)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "*="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '*'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::SLASH)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "/="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '/'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::PERCENT)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "%="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '%'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::AMP)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "&="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '&'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::PIPE)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "|="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '|'"); consume_rest_of_line(); return nullptr; }
    } else if (check(PPTokenType::CARET)) {
        consume();
        if (check(PPTokenType::ASSIGN)) { consume(); op = "^="; }
        else { m_diag.error(var_loc, "#set: esperado '=' despues de '^'"); consume_rest_of_line(); return nullptr; }
    } else {
        m_diag.error(var_loc, "#set: operador de asignacion esperado");
        consume_rest_of_line();
        return nullptr;
    }

    // saltar espacios opcionales antes del valor
    while (check(PPTokenType::WHITESPACE)) consume();

    // recoger el RHS hasta fin de linea
    std::vector<PPToken> expr = consume_rest_of_line();
    // eliminar WHITESPACE finales
    while (!expr.empty() && expr.back().type == PPTokenType::WHITESPACE)
        expr.pop_back();

    return std::make_unique<SetDirNode>(
        hash_loc, std::move(var_name), std::move(op), std::move(expr));
}

NodePtr PPParser::parse_import_macro(SourceLocation hash_loc) {
    // saltar espacios opcionales entre '#import' y la ruta
    while (check(PPTokenType::WHITESPACE)) consume();

    std::string path;

    if (check(PPTokenType::ANGLE_STRING)) {
        // caso compacto: el lexer ya produjo un ANGLE_STRING <...>
        std::string raw = cur().value;
        path = raw.substr(1, raw.size() - 2);
        consume();
    } else if (check(PPTokenType::LT)) {
        // caso token-a-token: < ... >
        consume(); // consume '<'
        while (!check(PPTokenType::GT) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            if (check(PPTokenType::WHITESPACE)) path += ' ';
            else                                path += cur().value;
            consume();
        }
        if (check(PPTokenType::GT)) consume();
        // eliminar espacios extremos del nombre de modulo
        while (!path.empty() && path.front() == ' ') path.erase(path.begin());
        while (!path.empty() && path.back()  == ' ') path.pop_back();
    } else {
        m_diag.error(cur().loc, "#import requiere el modulo entre angulos: #import <modulo>");
        consume_rest_of_line();
        return nullptr;
    }

    if (path.empty()) {
        m_diag.error(hash_loc, "#import: nombre de modulo vacio");
        consume_rest_of_line();
        return nullptr;
    }

    consume_rest_of_line();
    // is_system=true, is_import=true
    return std::make_unique<IncludeNode>(hash_loc, std::move(path), true, true);
}

NodePtr PPParser::parse_assert(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    // recoger tokens de la condicion hasta una cadena opcional o fin de linea
    std::vector<PPToken> condition;
    std::string message;

    while (!check(PPTokenType::NEWLINE) && !check(PPTokenType::PP_EOF)) {
        // si encontramos una cadena literal al final, es el mensaje de error
        if (check(PPTokenType::STRING)) {
            // verificar que no queden mas tokens significativos despues
            PPToken str_tok = consume();
            while (check(PPTokenType::WHITESPACE)) consume();
            if (check(PPTokenType::NEWLINE) || check(PPTokenType::PP_EOF)) {
                // el string es el mensaje, no parte de la condicion
                std::string raw = str_tok.value;
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                    message = raw.substr(1, raw.size() - 2);
                else
                    message = raw;
                break;
            }
            // no era el mensaje final: incluirlo en la condicion
            condition.push_back(str_tok);
            continue;
        }
        condition.push_back(consume());
    }
    // eliminar whitespace al final de la condicion
    while (!condition.empty() && condition.back().type == PPTokenType::WHITESPACE)
        condition.pop_back();

    if (check(PPTokenType::NEWLINE)) consume();

    if (condition.empty()) {
        m_diag.error(hash_loc, "#assert requiere una expresion");
        return nullptr;
    }

    return std::make_unique<AssertNode>(hash_loc, std::move(condition), std::move(message));
}

NodePtr PPParser::parse_macro(SourceLocation hash_loc) {
    while (check(PPTokenType::WHITESPACE)) consume();

    if (!check(PPTokenType::IDENT)) {
        m_diag.error(cur().loc, "#macro requiere un nombre");
        consume_rest_of_line();
        return nullptr;
    }
    std::string name = cur().value;
    consume();

    // parsear parametros — igual que en parse_define (LPAREN inmediatamente tras el nombre)
    std::vector<std::string> params;
    bool is_variadic = false;

    if (check(PPTokenType::LPAREN)) {
        consume(); // consume '('
        while (!check(PPTokenType::RPAREN) &&
               !check(PPTokenType::NEWLINE) &&
               !check(PPTokenType::PP_EOF)) {
            while (check(PPTokenType::WHITESPACE)) consume();
            if (check(PPTokenType::ELLIPSIS)) {
                consume();
                is_variadic = true;
                while (check(PPTokenType::WHITESPACE)) consume();
                break;
            }
            if (check(PPTokenType::IDENT)) {
                params.push_back(cur().value);
                consume();
                while (check(PPTokenType::WHITESPACE)) consume();
                if (check(PPTokenType::COMMA)) consume();
            } else {
                m_diag.error(cur().loc, "parametro invalido en #macro");
                break;
            }
        }
        if (!check(PPTokenType::RPAREN)) {
            m_diag.error(cur().loc, "se esperaba ')' al cerrar parametros de #macro");
        } else {
            consume(); // consume ')'
        }
    }
    consume_rest_of_line(); // ignorar resto de la linea #macro NAME(...)

    // recoger todos los tokens del cuerpo (incluyendo NEWLINEs) hasta #endmacro
    std::vector<PPToken> body;
    int depth = 1; // profundidad para #macro anidados (futuro)
    while (!check(PPTokenType::PP_EOF)) {
        if (check(PPTokenType::HASH)) {
            size_t saved = m_pos;
            consume(); // consume '#'
            while (check(PPTokenType::WHITESPACE)) consume();
            if (cur().is_ident("endmacro") && depth == 1) {
                consume(); // consume 'endmacro'
                consume_rest_of_line();
                break;
            }
            if (cur().is_ident("macro")) depth++;
            if (cur().is_ident("endmacro")) depth--;
            // restaurar y añadir '#' al cuerpo como texto
            m_pos = saved;
        }
        body.push_back(consume());
    }

    if (depth != 0 && check(PPTokenType::PP_EOF)) {
        m_diag.error(hash_loc, "#macro sin #endmacro correspondiente");
    }

    return std::make_unique<MacroBlockNode>(hash_loc, std::move(name),
                                            std::move(params), is_variadic,
                                            std::move(body));
}

} // namespace vpp
