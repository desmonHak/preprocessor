/**
 * @file pp_macro.cpp
 * @brief Implementacion de la tabla de macros y motor de expansion del preprocesador vpp.
 */

#include "preprocessor/pp_macro.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
    #define VPP_POPEN  _popen
    #define VPP_PCLOSE _pclose
#else
    #define VPP_POPEN  popen
    #define VPP_PCLOSE pclose
#endif

// macros predefinidas de plataforma detectadas en tiempo de compilacion
#if defined(_WIN32) || defined(_WIN64)
    #define VPP_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define VPP_PLATFORM_LINUX   1
#elif defined(__APPLE__)
    #define VPP_PLATFORM_MACOS   1
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    #define VPP_ARCH_BITS 64
#elif defined(__i386__) || defined(_M_IX86) || defined(__arm__)
    #define VPP_ARCH_BITS 32
#else
    #define VPP_ARCH_BITS 32
#endif

namespace vpp {

/* --- utilidades locales --------------------------------------------------- */

// @brief Escapa un token para meterlo dentro de una cadena de stringify.
//
// El estandar pide insertar una barra invertida delante de cada `"` y cada `\`
// DE UNA CADENA O CONSTANTE DE CARACTER.  Fuera de un literal no se escapa
// nada: por eso `#x` con `x\y` da `"x\y"` y no `"x\y"`.  Sin esto, un
// `STR(a "b" c)` producia `"a"b"c"`, que ni siquiera es C valido.
static std::string escape_for_stringify(const PPToken& t) {
    if (t.type != PPTokenType::STRING && t.type != PPTokenType::CHAR_LIT) {
        return t.value;
    }
    std::string out;
    out.reserve(t.value.size() + 4);
    for (char c : t.value) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// @brief Deshace el escapado de una cadena para el operador _Pragma.
//
// Es la operacion inversa a la de stringify: dentro del literal, `" y `\ se
// escribieron escapados y hay que devolverlos a su forma original.  El
// estandar lo llama "destringizar".
//
// @param literal Literal de cadena, con sus comillas.
// @return Texto sin comillas y sin los escapes de comilla y barra.
static std::string destringize(const std::string& literal) {
    std::string out;
    if (literal.size() < 2) return out;
    // saltar las comillas de los extremos
    for (size_t i = 1; i + 1 < literal.size(); ++i) {
        if (literal[i] == 0x5C && i + 2 < literal.size() &&
            (literal[i + 1] == '"' || literal[i + 1] == 0x5C)) {
            ++i;   // el escape se cae; queda el caracter que protegia
        }
        out += literal[i];
    }
    return out;
}

// @brief Convierte los tokens de un argumento a su texto para stringify.
//
// Cada tramo de blancos ENTRE tokens se convierte en UN espacio, y los de los
// extremos desaparecen.  Descartarlos del todo -- que era lo que se hacia --
// pegaba los tokens: `STR(a b)` daba `"ab"` en vez de `"a b"`.
static std::string tokens_raw(const std::vector<PPToken>& toks) {
    std::string s;
    bool pending_space = false;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::WHITESPACE ||
            t.type == PPTokenType::NEWLINE) {
            pending_space = !s.empty();   // nunca un espacio al principio
            continue;
        }
        if (pending_space) {
            s += ' ';
            pending_space = false;
        }
        s += escape_for_stringify(t);
    }
    return s;
}

// crea un token IDENT sintetico con un nombre dado
// crea un token STRING con el contenido entre comillas
static PPToken make_string(const std::string& content, const SourceLocation& loc) {
    return PPToken(PPTokenType::STRING, "\"" + content + "\"", loc);
}

/* --- MacroTable ----------------------------------------------------------- */

MacroTable::MacroTable(DiagnosticEngine& diag) : m_diag(diag) {
    register_platform_macros();
    register_builtin_fns();
}

void MacroTable::register_platform_macros() {
    SourceLocation builtin_loc("<builtin>", 0, 0);

    // --- plataforma ---
#ifdef VPP_PLATFORM_WINDOWS
    define_flag("__WINDOWS__");
    define_flag("_WIN32");
    #ifdef _WIN64
    define_flag("_WIN64");
    #endif
#endif
#ifdef VPP_PLATFORM_LINUX
    define_flag("__LINUX__");
    define_flag("__linux__");
#endif
#ifdef VPP_PLATFORM_MACOS
    define_flag("__MACOS__");
    define_flag("__APPLE__");
#endif

    // --- arquitectura ---
#if VPP_ARCH_BITS == 64
    define_flag("__ARCH_64__");
    define_simple("__POINTER_WIDTH__", "64");
#elif VPP_ARCH_BITS == 32
    define_flag("__ARCH_32__");
    define_simple("__POINTER_WIDTH__", "32");
#else
    define_flag("__ARCH_16__");
    define_simple("__POINTER_WIDTH__", "16");
#endif

    // --- version del preprocesador ---
    define_simple("__VPP_VERSION_MAJOR__", "1");
    define_simple("__VPP_VERSION_MINOR__", "0");
    define_simple("__VPP_VERSION__", "100");

    // macros dinamicas: se definen como flags vacias;
    // el Preprocessor principal las actualiza en cada linea
    define_simple("__FILE__",    "\"<unknown>\"");
    define_simple("__LINE__",    "0");
    define_simple("__COUNTER__", "0");
}

void MacroTable::define_simple(const std::string& name, const std::string& value) {
    SourceLocation l("<builtin>", 0, 0);
    std::vector<PPToken> body;
    body.emplace_back(PPTokenType::IDENT, value, l);
    MacroDef def(name, std::move(body), l, true);
    m_table.emplace(name, std::move(def));
}

void MacroTable::define_flag(const std::string& name) {
    SourceLocation l("<builtin>", 0, 0);
    MacroDef def(name, {}, l, true);
    m_table.emplace(name, std::move(def));
}

void MacroTable::define(MacroDef def) {
    auto it = m_table.find(def.name);
    if (it != m_table.end() && !it->second.is_builtin && !def.is_variable) {
        // advertir solo si la redefinicion difiere y no es una variable #set
        bool same = (it->second.body.size() == def.body.size());
        if (same) {
            for (size_t i = 0; i < def.body.size(); ++i) {
                if (def.body[i].value != it->second.body[i].value) {
                    same = false; break;
                }
            }
        }
        if (!same) {
            m_diag.warning(def.def_loc, "redefinicion de macro: " + def.name);
        }
    }
    m_table.insert_or_assign(def.name, std::move(def));
}

void MacroTable::undef(const std::string& name) {
    m_table.erase(name);
}

bool MacroTable::is_defined(const std::string& name) const {
    return m_table.count(name) > 0;
}

const MacroDef* MacroTable::get(const std::string& name) const {
    auto it = m_table.find(name);
    if (it == m_table.end()) return nullptr;
    return &it->second;
}

/* --- arrays --------------------------------------------------------------- */

void MacroTable::define_array(const std::string& name,
                               std::vector<std::string> items) {
    m_arrays.insert_or_assign(name, std::move(items));
}

bool MacroTable::has_array(const std::string& name) const {
    return m_arrays.count(name) > 0;
}

const std::vector<std::string>* MacroTable::get_array(
    const std::string& name) const
{
    auto it = m_arrays.find(name);
    if (it == m_arrays.end()) return nullptr;
    return &it->second;
}

void MacroTable::undef_array(const std::string& name) {
    m_arrays.erase(name);
}

const BuiltinFnHandler* MacroTable::get_builtin_fn(
    const std::string& name) const
{
    auto it = m_builtin_fns.find(name);
    if (it == m_builtin_fns.end()) return nullptr;
    return &it->second;
}

/* --- utilidades para macros funcion predefinidas --------------------------- */

// quita comillas dobles si el string las tiene (ej: "hello" -> hello)
static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// convierte string a int64, devuelve 0 si no es numerico
static int64_t to_int(const std::string& s) {
    if (s.empty()) return 0;
    try {
        // soportar prefijos 0x, 0b, 0 (octal)
        size_t idx = 0;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return static_cast<int64_t>(std::stoull(s.substr(2), &idx, 16));
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
            return static_cast<int64_t>(std::stoull(s.substr(2), &idx, 2));
        return std::stoll(s, &idx);
    } catch (...) { return 0; }
}

// formatea un int64 como string en la base indicada (2, 8, 10, 16)
static std::string fmt_int(int64_t v, int base) {
    if (base == 10) return std::to_string(v);
    uint64_t u = static_cast<uint64_t>(v);
    if (base == 16) {
        std::ostringstream oss;
        oss << std::hex << u;
        return oss.str();
    }
    if (base == 8) {
        std::ostringstream oss;
        oss << std::oct << u;
        return oss.str();
    }
    if (base == 2) {
        if (u == 0) return "0";
        std::string r;
        while (u) { r = (char)('0' + (u & 1)) + r; u >>= 1; }
        return r;
    }
    return std::to_string(v);
}

/* --- registro de macros funcion predefinidas ------------------------------- */

void MacroTable::register_builtin_fns() {
    // helper para crear token NUMBER
    auto mknum = [](int64_t n, const SourceLocation& l) -> PPToken {
        return PPToken(PPTokenType::NUMBER, std::to_string(n), l);
    };
    // helper para crear token STRING (con comillas)
    auto mkstr = [](const std::string& s, const SourceLocation& l) -> PPToken {
        return PPToken(PPTokenType::STRING, "\"" + s + "\"", l);
    };

    // --- operaciones de cadena ---

    // __STRLEN__(s) -> longitud de la cadena s (sin comillas)
    m_builtin_fns["__STRLEN__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        return mknum(static_cast<int64_t>(unquote(args[0]).size()), l);
    };

    // __TOUPPER__(s) -> s en mayusculas
    m_builtin_fns["__TOUPPER__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        std::string s = unquote(args[0]);
        for (char& c : s) c = static_cast<char>(std::toupper((unsigned char)c));
        return mkstr(s, l);
    };

    // __TOLOWER__(s) -> s en minusculas
    m_builtin_fns["__TOLOWER__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        std::string s = unquote(args[0]);
        for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
        return mkstr(s, l);
    };

    // __SUBSTR__(s, start, len) -> subcadena de s desde start con longitud len
    m_builtin_fns["__SUBSTR__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 3) return mkstr("", l);
        std::string s     = unquote(args[0]);
        size_t start      = static_cast<size_t>(to_int(unquote(args[1])));
        size_t len        = static_cast<size_t>(to_int(unquote(args[2])));
        if (start >= s.size()) return mkstr("", l);
        return mkstr(s.substr(start, len), l);
    };

    // __STRCAT__(a, b) -> concatenacion de a y b
    m_builtin_fns["__STRCAT__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mkstr(args.empty() ? "" : unquote(args[0]), l);
        return mkstr(unquote(args[0]) + unquote(args[1]), l);
    };

    // __STREQ__(a, b) -> 1 si a == b, 0 en caso contrario
    m_builtin_fns["__STREQ__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(0, l);
        return mknum(unquote(args[0]) == unquote(args[1]) ? 1 : 0, l);
    };

    // __CONTAINS__(s, sub) -> 1 si s contiene sub, 0 en caso contrario
    m_builtin_fns["__CONTAINS__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(0, l);
        std::string s   = unquote(args[0]);
        std::string sub = unquote(args[1]);
        return mknum(s.find(sub) != std::string::npos ? 1 : 0, l);
    };

    // __STRREPLACE__(s, from, to) -> reemplaza todas las ocurrencias de from por to en s
    m_builtin_fns["__STRREPLACE__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 3) return mkstr(args.empty() ? "" : unquote(args[0]), l);
        std::string s    = unquote(args[0]);
        std::string from = unquote(args[1]);
        std::string to   = unquote(args[2]);
        if (!from.empty()) {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
        }
        return mkstr(s, l);
    };

    // __TRIM__(s) -> elimina espacios iniciales y finales de s
    m_builtin_fns["__TRIM__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        std::string s = unquote(args[0]);
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return mkstr("", l);
        return mkstr(s.substr(a, b - a + 1), l);
    };

    // --- operaciones numericas ---

    // __MIN__(a, b) -> minimo de a y b
    m_builtin_fns["__MIN__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(0, l);
        int64_t a = to_int(unquote(args[0]));
        int64_t b = to_int(unquote(args[1]));
        return mknum(a < b ? a : b, l);
    };

    // __MAX__(a, b) -> maximo de a y b
    m_builtin_fns["__MAX__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(0, l);
        int64_t a = to_int(unquote(args[0]));
        int64_t b = to_int(unquote(args[1]));
        return mknum(a > b ? a : b, l);
    };

    // __ABS__(n) -> valor absoluto de n
    m_builtin_fns["__ABS__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        int64_t n = to_int(unquote(args[0]));
        return mknum(n < 0 ? -n : n, l);
    };

    // __POW__(base, exp) -> base elevado a exp (enteros, exp >= 0)
    m_builtin_fns["__POW__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(1, l);
        int64_t base = to_int(unquote(args[0]));
        int64_t exp  = to_int(unquote(args[1]));
        if (exp < 0) return mknum(0, l);
        int64_t result = 1;
        for (int64_t i = 0; i < exp; ++i) result *= base;
        return mknum(result, l);
    };

    // __CLAMP__(val, lo, hi) -> val limitado al rango [lo, hi]
    m_builtin_fns["__CLAMP__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 3) return mknum(0, l);
        int64_t val = to_int(unquote(args[0]));
        int64_t lo  = to_int(unquote(args[1]));
        int64_t hi  = to_int(unquote(args[2]));
        if (val < lo) val = lo;
        if (val > hi) val = hi;
        return mknum(val, l);
    };

    // __LOG2__(n) -> floor(log2(n)), o 0 para n <= 0
    m_builtin_fns["__LOG2__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        int64_t n = to_int(unquote(args[0]));
        if (n <= 0) return mknum(0, l);
        int64_t r = 0;
        while (n > 1) { n >>= 1; ++r; }
        return mknum(r, l);
    };

    // __ISODD__(n) -> 1 si n es impar, 0 si es par
    m_builtin_fns["__ISODD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        return mknum((to_int(unquote(args[0])) & 1) ? 1 : 0, l);
    };

    // __ISEVEN__(n) -> 1 si n es par, 0 si es impar
    m_builtin_fns["__ISEVEN__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(1, l);
        return mknum((to_int(unquote(args[0])) & 1) ? 0 : 1, l);
    };

    // __NUMFMT__(n, base) -> representacion de n en la base dada (2, 8, 10, 16)
    m_builtin_fns["__NUMFMT__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mkstr("0", l);
        int64_t n    = to_int(unquote(args[0]));
        int     base = static_cast<int>(to_int(unquote(args[1])));
        if (base != 2 && base != 8 && base != 10 && base != 16) base = 10;
        return mkstr(fmt_int(n, base), l);
    };

    // --- operaciones sobre arrays ---

    // __ARRAY_SIZE__(NAME) -> numero de elementos del array NAME
    m_builtin_fns["__ARRAY_SIZE__"] = [mknum](
        MacroTable& tbl, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        const auto* arr = tbl.get_array(unquote(args[0]));
        if (!arr) return mknum(0, l);
        return mknum(static_cast<int64_t>(arr->size()), l);
    };

    // __ARRAY_GET__(NAME, idx) -> elemento en la posicion idx del array NAME
    m_builtin_fns["__ARRAY_GET__"] = [](
        MacroTable& tbl, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return PPToken(PPTokenType::IDENT, "", l);
        const auto* arr = tbl.get_array(unquote(args[0]));
        if (!arr || arr->empty()) return PPToken(PPTokenType::IDENT, "", l);
        size_t idx = static_cast<size_t>(to_int(unquote(args[1])));
        if (idx >= arr->size()) idx = arr->size() - 1;
        return PPToken(PPTokenType::IDENT, (*arr)[idx], l);
    };

    // __ARRAY_JOIN__(NAME, sep) -> todos los elementos del array unidos por sep (STRING)
    m_builtin_fns["__ARRAY_JOIN__"] = [mkstr](
        MacroTable& tbl, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        const auto* arr = tbl.get_array(unquote(args[0]));
        if (!arr || arr->empty()) return mkstr("", l);
        std::string sep = args.size() >= 2 ? unquote(args[1]) : "";
        std::string result;
        for (size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) result += sep;
            result += (*arr)[i];
        }
        return mkstr(result, l);
    };

    // __QUOTE__(x) -> devuelve x como cadena entre comillas dobles
    // Si x ya esta entre comillas, lo devuelve tal cual.
    m_builtin_fns["__QUOTE__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        const std::string& a = args[0];
        // si ya tiene comillas, no anidar
        if (a.size() >= 2 && a.front() == '"' && a.back() == '"')
            return PPToken(PPTokenType::STRING, a, l);
        return mkstr(a, l);
    };

    // __UNQUOTE__(s) -> devuelve el contenido de s sin las comillas externas
    // Util para usar un string como identificador o token crudo.
    m_builtin_fns["__UNQUOTE__"] = [](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return PPToken(PPTokenType::IDENT, "", l);
        std::string val = args[0];
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        return PPToken(PPTokenType::IDENT, val, l);
    };

    // __EXEC__(cmd) -> salida estandar del comando cmd (STRING literal)
    m_builtin_fns["__EXEC__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mkstr("", l);
        std::string cmd = unquote(args[0]);
        FILE* pipe = VPP_POPEN(cmd.c_str(), "r");
        if (!pipe) return mkstr("", l);
        char buf[256];
        std::string output;
        while (std::fgets(buf, sizeof(buf), pipe)) output += buf;
        VPP_PCLOSE(pipe);
        // eliminar salto de linea final
        while (!output.empty() &&
               (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        return mkstr(output, l);
    };

    // =========================================================================
    // Macros funcion de flotantes (IEEE 754 double via bit-cast)
    // Los argumentos son bits IEEE 754 como entero de 64 bits.
    // =========================================================================

    // helper: convierte bits enteros a double
    auto bits2d = [](int64_t bits) -> double {
        uint64_t u = static_cast<uint64_t>(bits);
        double d;
        std::memcpy(&d, &u, 8);
        return d;
    };
    // helper: convierte double a bits enteros
    auto d2bits = [](double d) -> int64_t {
        uint64_t u;
        std::memcpy(&u, &d, 8);
        return static_cast<int64_t>(u);
    };
    // helper: parse double de cadena (puede ser entero o flotante)
    auto parse_double = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };

    // __F2BITS__(x) -> representacion de bits IEEE 754 del double x
    m_builtin_fns["__F2BITS__"] = [mknum, parse_double, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double d = args.empty() ? 0.0 : parse_double(unquote(args[0]));
        return mknum(d2bits(d), l);
    };

    // __BITS2F__(bits) -> cadena con el valor double en notacion decimal
    m_builtin_fns["__BITS2F__"] = [mkstr, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t bits = args.empty() ? 0 : to_int(unquote(args[0]));
        double d = bits2d(bits);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", d);
        return mkstr(buf, l);
    };

    // __FADD__(a, b) -> bits de a + b
    m_builtin_fns["__FADD__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(a + b), l);
    };

    // __FSUB__(a, b) -> bits de a - b
    m_builtin_fns["__FSUB__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(a - b), l);
    };

    // __FMUL__(a, b) -> bits de a * b
    m_builtin_fns["__FMUL__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(a * b), l);
    };

    // __FDIV__(a, b) -> bits de a / b
    m_builtin_fns["__FDIV__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(b != 0.0 ? a / b : 0.0), l);
    };

    // __FMOD__(a, b) -> bits de fmod(a, b)
    m_builtin_fns["__FMOD__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(std::fmod(a, b)), l);
    };

    // __FABS__(a) -> bits de |a|
    m_builtin_fns["__FABS__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::fabs(a)), l);
    };

    // __FNEG__(a) -> bits de -a
    m_builtin_fns["__FNEG__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(-a), l);
    };

    // __FSQRT__(a) -> bits de sqrt(a)
    m_builtin_fns["__FSQRT__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::sqrt(a)), l);
    };

    // __FPOW__(a, b) -> bits de pow(a, b)
    m_builtin_fns["__FPOW__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(std::pow(a, b)), l);
    };

    // __FFLOOR__(a) -> bits de floor(a)
    m_builtin_fns["__FFLOOR__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::floor(a)), l);
    };

    // __FCEIL__(a) -> bits de ceil(a)
    m_builtin_fns["__FCEIL__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::ceil(a)), l);
    };

    // __FROUND__(a) -> bits de round(a)
    m_builtin_fns["__FROUND__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::round(a)), l);
    };

    // __FTRUNC__(a) -> bits de trunc(a)
    m_builtin_fns["__FTRUNC__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::trunc(a)), l);
    };

    // __FMIN__(a, b) -> bits de min(a, b)
    m_builtin_fns["__FMIN__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(a < b ? a : b), l);
    };

    // __FMAX__(a, b) -> bits de max(a, b)
    m_builtin_fns["__FMAX__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(d2bits(a > b ? a : b), l);
    };

    // __FCLAMP__(val, lo, hi) -> bits de clamp(val, lo, hi)
    m_builtin_fns["__FCLAMP__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double v = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double lo= args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        double hi= args.size() > 2 ? bits2d(to_int(unquote(args[2]))) : 0.0;
        return mknum(d2bits(v < lo ? lo : (v > hi ? hi : v)), l);
    };

    // __FSIN__(a) -> bits de sin(a)  [a en radianes como bits IEEE 754]
    m_builtin_fns["__FSIN__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::sin(a)), l);
    };

    // __FCOS__(a) -> bits de cos(a)
    m_builtin_fns["__FCOS__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::cos(a)), l);
    };

    // __FTAN__(a) -> bits de tan(a)
    m_builtin_fns["__FTAN__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::tan(a)), l);
    };

    // __FASIN__(a) -> bits de asin(a)
    m_builtin_fns["__FASIN__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::asin(a)), l);
    };

    // __FACOS__(a) -> bits de acos(a)
    m_builtin_fns["__FACOS__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::acos(a)), l);
    };

    // __FATAN__(a) -> bits de atan(a)
    m_builtin_fns["__FATAN__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::atan(a)), l);
    };

    // __FATAN2__(y, x) -> bits de atan2(y, x)
    m_builtin_fns["__FATAN2__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double y = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double x = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 1.0;
        return mknum(d2bits(std::atan2(y, x)), l);
    };

    // __FLOG__(a) -> bits de log(a)  (logaritmo natural)
    m_builtin_fns["__FLOG__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 1.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::log(a)), l);
    };

    // __FLOG2__(a) -> bits de log2(a)
    m_builtin_fns["__FLOG2__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 1.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::log2(a)), l);
    };

    // __FLOG10__(a) -> bits de log10(a)
    m_builtin_fns["__FLOG10__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 1.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::log10(a)), l);
    };

    // __FEXP__(a) -> bits de exp(a)
    m_builtin_fns["__FEXP__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(std::exp(a)), l);
    };

    // __FFORMAT__(bits) -> cadena con valor IEEE 754 formateado
    m_builtin_fns["__FFORMAT__"] = [mkstr, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t bits = args.empty() ? 0 : to_int(unquote(args[0]));
        double d = bits2d(bits);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", d);
        return mkstr(buf, l);
    };

    // __FTOI__(bits) -> parte entera truncada del double como entero
    m_builtin_fns["__FTOI__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t bits = args.empty() ? 0 : to_int(unquote(args[0]));
        double d = bits2d(bits);
        return mknum(static_cast<int64_t>(d), l);
    };

    // __ITOF__(n) -> bits IEEE 754 del double equivalente al entero n
    m_builtin_fns["__ITOF__"] = [mknum, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(d2bits(static_cast<double>(n)), l);
    };

    // __FEQ__(a, b) -> 1 si los doubles son iguales, 0 si no
    m_builtin_fns["__FEQ__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(a == b ? 1 : 0, l);
    };

    // __FLT__(a, b) -> 1 si a < b
    m_builtin_fns["__FLT__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(a < b ? 1 : 0, l);
    };

    // __FGT__(a, b) -> 1 si a > b
    m_builtin_fns["__FGT__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double a = args.size() > 0 ? bits2d(to_int(unquote(args[0]))) : 0.0;
        double b = args.size() > 1 ? bits2d(to_int(unquote(args[1]))) : 0.0;
        return mknum(a > b ? 1 : 0, l);
    };

    // __FISNAN__(bits) -> 1 si el double es NaN
    m_builtin_fns["__FISNAN__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double d = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(std::isnan(d) ? 1 : 0, l);
    };

    // __FISINF__(bits) -> 1 si el double es infinito
    m_builtin_fns["__FISINF__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double d = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(std::isinf(d) ? 1 : 0, l);
    };

    // __FISZERO__(bits) -> 1 si el double es cero (positivo o negativo)
    m_builtin_fns["__FISZERO__"] = [mknum, bits2d](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        double d = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d == 0.0 ? 1 : 0, l);
    };

    // __FDEG2RAD__(bits) -> bits de (deg * pi / 180)
    m_builtin_fns["__FDEG2RAD__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        static const double PI = 3.14159265358979323846;
        double deg = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(deg * PI / 180.0), l);
    };

    // __FRAD2DEG__(bits) -> bits de (rad * 180 / pi)
    m_builtin_fns["__FRAD2DEG__"] = [mknum, bits2d, d2bits](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        static const double PI = 3.14159265358979323846;
        double rad = args.empty() ? 0.0 : bits2d(to_int(unquote(args[0])));
        return mknum(d2bits(rad * 180.0 / PI), l);
    };

    // __FPI__ -> bits IEEE 754 de pi
    m_builtin_fns["__FPI__"] = [mknum, d2bits](
        MacroTable&, const std::vector<std::string>&,
        const SourceLocation& l) -> PPToken
    {
        static const double PI = 3.14159265358979323846;
        return mknum(d2bits(PI), l);
    };

    // =========================================================================
    // Macros de conversion numerica y manipulacion de bits
    // =========================================================================

    // __DEC2HEX__(n) -> representacion hexadecimal de n (sin prefijo 0x)
    m_builtin_fns["__DEC2HEX__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mkstr(fmt_int(n, 16), l);
    };

    // __DEC2BIN__(n) -> representacion binaria de n (sin prefijo 0b)
    m_builtin_fns["__DEC2BIN__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mkstr(fmt_int(n, 2), l);
    };

    // __DEC2OCT__(n) -> representacion octal de n (sin prefijo 0)
    m_builtin_fns["__DEC2OCT__"] = [mkstr](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mkstr(fmt_int(n, 8), l);
    };

    // __HEX2DEC__(s) -> valor decimal del numero hexadecimal s
    m_builtin_fns["__HEX2DEC__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        std::string s = unquote(args[0]);
        // aceptar con o sin prefijo 0x
        if (s.size() > 1 && s[0] == '0' && (s[1]=='x'||s[1]=='X'))
            s = s.substr(2);
        try { return mknum(static_cast<int64_t>(std::stoull(s, nullptr, 16)), l); }
        catch (...) { return mknum(0, l); }
    };

    // __BIN2DEC__(s) -> valor decimal del numero binario s
    m_builtin_fns["__BIN2DEC__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        std::string s = unquote(args[0]);
        if (s.size() > 1 && s[0] == '0' && (s[1]=='b'||s[1]=='B'))
            s = s.substr(2);
        try { return mknum(static_cast<int64_t>(std::stoull(s, nullptr, 2)), l); }
        catch (...) { return mknum(0, l); }
    };

    // __OCT2DEC__(s) -> valor decimal del numero octal s
    m_builtin_fns["__OCT2DEC__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        std::string s = unquote(args[0]);
        try { return mknum(static_cast<int64_t>(std::stoull(s, nullptr, 8)), l); }
        catch (...) { return mknum(0, l); }
    };

    // __PARSE_INT__(s, base) -> valor decimal del numero s en la base dada
    m_builtin_fns["__PARSE_INT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.empty()) return mknum(0, l);
        std::string s = unquote(args[0]);
        int base = args.size() > 1 ? static_cast<int>(to_int(unquote(args[1]))) : 10;
        try { return mknum(static_cast<int64_t>(std::stoull(s, nullptr, base)), l); }
        catch (...) { return mknum(0, l); }
    };

    // __LOBYTE__(n) -> byte bajo de n (bits 0..7)
    m_builtin_fns["__LOBYTE__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(n & 0xFF, l);
    };

    // __HIBYTE__(n) -> byte alto del word bajo de n (bits 8..15)
    m_builtin_fns["__HIBYTE__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum((n >> 8) & 0xFF, l);
    };

    // __LOWORD__(n) -> word bajo de n (bits 0..15)
    m_builtin_fns["__LOWORD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(n & 0xFFFF, l);
    };

    // __HIWORD__(n) -> word alto del dword bajo de n (bits 16..31)
    m_builtin_fns["__HIWORD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum((n >> 16) & 0xFFFF, l);
    };

    // __SWAP16__(n) -> intercambia los dos bytes de un word de 16 bits
    m_builtin_fns["__SWAP16__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        uint16_t n = static_cast<uint16_t>(args.empty() ? 0 : to_int(unquote(args[0])));
        uint16_t s = static_cast<uint16_t>((n >> 8) | (n << 8));
        return mknum(static_cast<int64_t>(s), l);
    };

    // __SWAP32__(n) -> invierte el orden de bytes de un dword de 32 bits
    m_builtin_fns["__SWAP32__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        uint32_t n = static_cast<uint32_t>(args.empty() ? 0 : to_int(unquote(args[0])));
        uint32_t s = ((n & 0xFF000000u) >> 24)
                   | ((n & 0x00FF0000u) >>  8)
                   | ((n & 0x0000FF00u) <<  8)
                   | ((n & 0x000000FFu) << 24);
        return mknum(static_cast<int64_t>(s), l);
    };

    // __SWAP64__(n) -> invierte el orden de bytes de un qword de 64 bits
    m_builtin_fns["__SWAP64__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        uint64_t n = static_cast<uint64_t>(args.empty() ? 0 : to_int(unquote(args[0])));
        uint64_t s = ((n & 0xFF00000000000000ull) >> 56)
                   | ((n & 0x00FF000000000000ull) >> 40)
                   | ((n & 0x0000FF0000000000ull) >> 24)
                   | ((n & 0x000000FF00000000ull) >>  8)
                   | ((n & 0x00000000FF000000ull) <<  8)
                   | ((n & 0x0000000000FF0000ull) << 24)
                   | ((n & 0x000000000000FF00ull) << 40)
                   | ((n & 0x00000000000000FFull) << 56);
        return mknum(static_cast<int64_t>(s), l);
    };

    // __BITSET__(n, bit) -> n con el bit 'bit' puesto a 1
    m_builtin_fns["__BITSET__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n   = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t bit = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(n | (int64_t(1) << bit), l);
    };

    // __BITCLEAR__(n, bit) -> n con el bit 'bit' puesto a 0
    m_builtin_fns["__BITCLEAR__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n   = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t bit = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(n & ~(int64_t(1) << bit), l);
    };

    // __BITTEST__(n, bit) -> 1 si el bit 'bit' de n esta a 1, 0 si no
    m_builtin_fns["__BITTEST__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n   = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t bit = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum((n >> bit) & 1, l);
    };

    // __BITCOUNT__(n) -> numero de bits a 1 en n (popcount)
    m_builtin_fns["__BITCOUNT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        uint64_t n = static_cast<uint64_t>(args.empty() ? 0 : to_int(unquote(args[0])));
        int count = 0;
        while (n) { count += (int)(n & 1); n >>= 1; }
        return mknum(count, l);
    };

    // __SIGNEXT__(n, width) -> extension de signo de n con 'width' bits a 64 bits
    m_builtin_fns["__SIGNEXT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n     = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t width = args.size() > 1 ? to_int(unquote(args[1])) : 64;
        if (width <= 0 || width >= 64) return mknum(n, l);
        int64_t sign_bit = int64_t(1) << (width - 1);
        int64_t mask     = sign_bit - 1;
        int64_t result   = (n & mask) | -((n >> (width - 1)) & 1) * sign_bit;
        return mknum(result, l);
    };

    // =========================================================================
    // Aritmetica entera basica
    // =========================================================================

    // __ADD__(a, b) -> a + b
    m_builtin_fns["__ADD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a + b, l);
    };

    // __SUB__(a, b) -> a - b
    m_builtin_fns["__SUB__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a - b, l);
    };

    // __MUL__(a, b) -> a * b
    m_builtin_fns["__MUL__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 1;
        return mknum(a * b, l);
    };

    // __DIV__(a, b) -> division entera de a entre b (0 si b == 0)
    m_builtin_fns["__DIV__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 1;
        return mknum(b != 0 ? a / b : 0, l);
    };

    // __MOD__(a, b) -> modulo entero de a entre b (0 si b == 0)
    m_builtin_fns["__MOD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 1;
        return mknum(b != 0 ? a % b : 0, l);
    };

    // __NEG__(a) -> negacion entera de a
    m_builtin_fns["__NEG__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(-a, l);
    };

    // __AND__(a, b) -> a AND b (bit a bit)
    m_builtin_fns["__AND__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a & b, l);
    };

    // __OR__(a, b) -> a OR b (bit a bit)
    m_builtin_fns["__OR__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a | b, l);
    };

    // __XOR__(a, b) -> a XOR b (bit a bit)
    m_builtin_fns["__XOR__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a ^ b, l);
    };

    // __NOT__(a) -> NOT bit a bit de a (complemento a uno en 64 bits)
    m_builtin_fns["__NOT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(~a, l);
    };

    // __SHL__(a, n) -> a desplazado n bits a la izquierda
    m_builtin_fns["__SHL__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t n = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(n >= 0 && n < 64 ? a << n : 0, l);
    };

    // __SHR__(a, n) -> a desplazado n bits a la derecha (logico, sin extension de signo)
    m_builtin_fns["__SHR__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        uint64_t a = static_cast<uint64_t>(args.size() > 0 ? to_int(unquote(args[0])) : 0);
        int64_t  n = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(static_cast<int64_t>(n >= 0 && n < 64 ? a >> n : 0), l);
    };

    // __SAR__(a, n) -> a desplazado n bits a la derecha (aritmetico, con extension de signo)
    m_builtin_fns["__SAR__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t n = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(n >= 0 && n < 64 ? a >> n : (a < 0 ? -1 : 0), l);
    };

    // __LODWORD__(n) -> dword bajo de n (bits 0..31)
    m_builtin_fns["__LODWORD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(n & 0xFFFFFFFF, l);
    };

    // __HIDWORD__(n) -> dword alto de n (bits 32..63)
    m_builtin_fns["__HIDWORD__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n = args.empty() ? 0 : to_int(unquote(args[0]));
        return mknum(static_cast<int64_t>(static_cast<uint64_t>(n) >> 32), l);
    };

    // __ZEROEXT__(n, width) -> n truncado a 'width' bits (extension de cero a 64 bits)
    m_builtin_fns["__ZEROEXT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t n     = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t width = args.size() > 1 ? to_int(unquote(args[1])) : 64;
        if (width <= 0 || width >= 64) return mknum(n, l);
        uint64_t mask = (uint64_t(1) << width) - 1;
        return mknum(static_cast<int64_t>(static_cast<uint64_t>(n) & mask), l);
    };

    // __ALIGN__(val, align) -> val redondeado hacia arriba al multiplo de align
    m_builtin_fns["__ALIGN__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t val   = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t align = args.size() > 1 ? to_int(unquote(args[1])) : 1;
        if (align <= 0) return mknum(val, l);
        return mknum((val + align - 1) / align * align, l);
    };

    // =========================================================================
    // Comparaciones enteras
    // =========================================================================

    // __EQ__(a, b) -> 1 si a == b, 0 si no
    m_builtin_fns["__EQ__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a == b ? 1 : 0, l);
    };

    // __NEQ__(a, b) -> 1 si a != b, 0 si no
    m_builtin_fns["__NEQ__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a != b ? 1 : 0, l);
    };

    // __LT__(a, b) -> 1 si a < b, 0 si no
    m_builtin_fns["__LT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a < b ? 1 : 0, l);
    };

    // __GT__(a, b) -> 1 si a > b, 0 si no
    m_builtin_fns["__GT__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a > b ? 1 : 0, l);
    };

    // __LE__(a, b) -> 1 si a <= b, 0 si no
    m_builtin_fns["__LE__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a <= b ? 1 : 0, l);
    };

    // __GE__(a, b) -> 1 si a >= b, 0 si no
    m_builtin_fns["__GE__"] = [mknum](
        MacroTable&, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        int64_t a = args.size() > 0 ? to_int(unquote(args[0])) : 0;
        int64_t b = args.size() > 1 ? to_int(unquote(args[1])) : 0;
        return mknum(a >= b ? 1 : 0, l);
    };

    // =========================================================================
    // Busqueda en arrays
    // =========================================================================

    // __ARRAY_FIND__(NAME, val) -> indice de 'val' en el array NAME (-1 si no encontrado)
    m_builtin_fns["__ARRAY_FIND__"] = [mknum](
        MacroTable& tbl, const std::vector<std::string>& args,
        const SourceLocation& l) -> PPToken
    {
        if (args.size() < 2) return mknum(-1, l);
        const auto* arr = tbl.get_array(unquote(args[0]));
        if (!arr) return mknum(-1, l);
        std::string val = unquote(args[1]);
        for (size_t i = 0; i < arr->size(); ++i) {
            if ((*arr)[i] == val) return mknum(static_cast<int64_t>(i), l);
        }
        return mknum(-1, l);
    };
}

/* --- expansion ------------------------------------------------------------- */

// recolecta argumentos de una llamada de funcion sin necesitar un MacroDef
static std::vector<std::vector<PPToken>> collect_generic_args(
    const std::vector<PPToken>& tokens,
    size_t& pos) // pos debe apuntar al LPAREN de apertura
{
    pos++; // consumir '('
    std::vector<std::vector<PPToken>> args;
    std::vector<PPToken> cur_arg;
    int depth = 1;
    while (pos < tokens.size() && depth > 0) {
        const PPToken& t = tokens[pos];
        if (t.type == PPTokenType::LPAREN) {
            ++depth; cur_arg.push_back(t);
        } else if (t.type == PPTokenType::RPAREN) {
            --depth;
            if (depth == 0) {
                args.push_back(std::move(cur_arg));
                cur_arg.clear();
                pos++;
                break;
            }
            cur_arg.push_back(t);
        } else if (t.type == PPTokenType::COMMA && depth == 1) {
            args.push_back(std::move(cur_arg));
            cur_arg.clear();
        } else {
            cur_arg.push_back(t);
        }
        pos++;
    }
    // eliminar whitespace inicial y final de cada argumento
    for (auto& arg : args) {
        // Tambien los NEWLINE: una llamada repartida en varias lineas mete un
        // salto al principio del argumento siguiente, y sin recortarlo la
        // expansion arrastra un espacio de mas frente a lo que emite un
        // preprocesador de C.  El blanco que rodea a un argumento no forma
        // parte de el.
        auto is_blank = [](const PPToken& t) {
            return t.type == PPTokenType::WHITESPACE ||
                   t.type == PPTokenType::NEWLINE;
        };
        while (!arg.empty() && is_blank(arg.front()))
            arg.erase(arg.begin());
        while (!arg.empty() && is_blank(arg.back()))
            arg.pop_back();
    }
    return args;
}

// convierte una secuencia de tokens expandidos a una cadena plana para built-ins
// si es un unico token STRING, quita las comillas externas
static std::string tokens_to_arg_string(const std::vector<PPToken>& toks) {
    if (toks.size() == 1 && toks[0].type == PPTokenType::STRING) {
        const std::string& v = toks[0].value;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            return v.substr(1, v.size() - 2);
    }
    std::string s;
    for (const auto& t : toks) {
        if (t.type == PPTokenType::WHITESPACE) s += ' ';
        else s += t.value;
    }
    return s;
}

// declaracion adelantada: expand_impl es llamado por expand_one_impl
static std::vector<PPToken> expand_impl(
    const std::vector<PPToken>& tokens,
    MacroTable& table,
    std::unordered_map<std::string, bool>& guard);

static std::vector<PPToken> expand_one_impl(
    const std::vector<PPToken>& tokens,
    size_t& pos,
    MacroTable& table,
    std::unordered_map<std::string, bool>& guard)
{
    const PPToken& tok = tokens[pos];
    const std::string& name = tok.value;

    // si la macro esta en el guard, no expandir (evitar recursion infinita)
    if (guard.count(name)) {
        return { tokens[pos++] };
    }

    // verificar si es una macro funcion predefinida del sistema (built-in)
    const BuiltinFnHandler* bfn = table.get_builtin_fn(name);
    if (bfn) {
        // pos+1: saltar el IDENT del built-in antes de buscar el '('
        size_t tmp = pos + 1;
        while (tmp < tokens.size() &&
               tokens[tmp].type == PPTokenType::WHITESPACE) ++tmp;
        if (tmp < tokens.size() && tokens[tmp].type == PPTokenType::LPAREN) {
            pos = tmp; // actualizar pos al '(' (el IDENT queda consumido)
            // recolectar argumentos crudos
            auto raw_args = collect_generic_args(tokens, pos);
            // expandir cada argumento y convertirlo a cadena plana
            std::vector<std::string> str_args;
            str_args.reserve(raw_args.size());
            for (auto& arg : raw_args) {
                auto expanded = expand_impl(arg, table, guard);
                str_args.push_back(tokens_to_arg_string(expanded));
            }
            // invocar el manejador y devolver el token resultado
            PPToken result = (*bfn)(table, str_args, tok.loc);
            return { result };
        }
        // sin '(', no es llamada: emitir el nombre tal cual
        return { tokens[pos++] };
    }

    const MacroDef* mac = table.get(name);
    if (!mac) {
        return { tokens[pos++] };
    }

    pos++; // consumir el nombre de la macro

    if (!mac->is_function) {
        // macro objeto: expandir el cuerpo compartiendo el guard activo
        guard[name] = true;
        auto result = expand_impl(mac->body, table, guard);
        guard.erase(name);
        return result;
    }

    // macro funcion: saltar espacios para encontrar '('
    size_t tmp_pos = pos;
    while (tmp_pos < tokens.size() &&
           tokens[tmp_pos].type == PPTokenType::WHITESPACE) {
        ++tmp_pos;
    }
    if (tmp_pos >= tokens.size() ||
        tokens[tmp_pos].type != PPTokenType::LPAREN) {
        // no hay '(', no es una llamada: devolver el token sin expandir
        return { tok };
    }
    pos = tmp_pos; // actualizar posicion al '('

    // recolectar argumentos
    auto args = table.collect_args(tokens, pos, *mac, tok.loc);

    // aplicar # (stringify) sobre el cuerpo original, antes de sustituir los parametros
    // para que los nombres de parametro formales aun sean visibles.
    // Usa los argumentos SIN expandir, que es lo que pide el estandar para `#`.
    auto stringified = table.apply_stringify(mac->body, mac->params, args);

    // Argumentos completamente expandidos.
    //
    // El estandar dice que un argumento se expande del todo ANTES de meterlo en
    // el cuerpo, salvo cuando el parametro es operando de `#` o de `##`.  Antes
    // se sustituian siempre los crudos y se confiaba en el reescaneo posterior;
    // eso cuela en los casos simples -- `ID(A)` acaba expandiendo A igual -- pero
    // no cuando el parametro va a parar a OTRA macro funcion: el idioma
    // universal
    //     #define STR(x)  #x
    //     #define XSTR(x) STR(x)
    // daba XSTR(V) -> "V" en vez de "42", porque STR recibia el token sin
    // expandir.  Se expande aqui, antes de meter la macro actual en el guard:
    // durante la expansion del argumento la macro que se esta llamando todavia
    // no esta oculta.
    std::vector<std::vector<PPToken>> expanded_args;
    expanded_args.reserve(args.size());
    for (const auto& a : args) {
        expanded_args.push_back(expand_impl(a, table, guard));
    }

    // sustituir parametros en el cuerpo (ya sin los tokens #param que fueron stringificados)
    auto substituted = table.substitute(stringified, mac->params,
                                        args, expanded_args, tok.loc);

    // aplicar ## (token paste) despues de la sustitucion
    substituted = table.apply_token_paste(substituted);

    // expandir recursivamente compartiendo el guard (evitar re-expansion de la macro actual)
    guard[name] = true;
    auto result = expand_impl(substituted, table, guard);
    guard.erase(name);
    return result;
}

static std::vector<PPToken> expand_impl(
    const std::vector<PPToken>& tokens,
    MacroTable& table,
    std::unordered_map<std::string, bool>& guard)
{
    std::vector<PPToken> result;
    result.reserve(tokens.size());
    size_t pos = 0;
    while (pos < tokens.size()) {
        if (tokens[pos].type == PPTokenType::IDENT) {
            // Las tres macros dinamicas se resuelven AQUI y no en la tabla,
            // porque su valor no es fijo: depende de donde y cuando se
            // expanden.  Registrarlas como macros normales -- que es lo que se
            // hacia -- las dejaba congeladas en el valor con el que nacieron,
            // de ahi que __LINE__ diera siempre 0 y __COUNTER__ nunca subiera.
            //
            // La posicion sale del estado de la tabla y no del token, para que
            // un __LINE__ escrito DENTRO del cuerpo de una macro de la linea
            // donde se invoca y no donde se definio.  Es justo el caso para el
            // que se usa: `#define LOG(x) fprintf(f, "%s:%d", __FILE__, __LINE__)`.
            const std::string& name = tokens[pos].value;
            if (name == "__LINE__") {
                result.emplace_back(PPTokenType::NUMBER,
                                    std::to_string(table.current_line()),
                                    tokens[pos].loc);
                ++pos;
                continue;
            }
            if (name == "__FILE__") {
                result.emplace_back(PPTokenType::STRING,
                                    "\"" + table.current_file() + "\"",
                                    tokens[pos].loc);
                ++pos;
                continue;
            }
            if (name == "_Pragma") {
                // _Pragma("texto") equivale a escribir `#pragma texto`.
                // Existe porque una macro no puede generar una directiva, y
                // esta es la via que da el estandar para conseguirlo.
                size_t j = pos + 1;
                while (j < tokens.size() &&
                       tokens[j].type == PPTokenType::WHITESPACE) ++j;

                if (j < tokens.size() &&
                    tokens[j].type == PPTokenType::LPAREN) {
                    size_t k = j + 1;
                    while (k < tokens.size() &&
                           tokens[k].type == PPTokenType::WHITESPACE) ++k;

                    if (k < tokens.size() &&
                        tokens[k].type == PPTokenType::STRING) {
                        size_t m = k + 1;
                        while (m < tokens.size() &&
                               tokens[m].type == PPTokenType::WHITESPACE) ++m;

                        if (m < tokens.size() &&
                            tokens[m].type == PPTokenType::RPAREN) {
                            // La directiva tiene que quedar sola en su linea,
                            // de ahi los saltos que la envuelven.
                            const SourceLocation& l = tokens[pos].loc;
                            result.emplace_back(PPTokenType::NEWLINE, "\n", l);
                            result.emplace_back(PPTokenType::TEXT,
                                "#pragma " + destringize(tokens[k].value), l);
                            result.emplace_back(PPTokenType::NEWLINE, "\n", l);
                            pos = m + 1;
                            continue;
                        }
                    }
                }
                // mal formado: se deja pasar tal cual, que es mas util que
                // inventarse una directiva
            }

            if (name == "__COUNTER__") {
                // se consume por EXPANSION: dos usos en la misma linea tienen
                // que dar valores distintos
                result.emplace_back(PPTokenType::NUMBER,
                                    std::to_string(table.next_counter()),
                                    tokens[pos].loc);
                ++pos;
                continue;
            }

            const std::string nombre_previo = tokens[pos].value;
            auto exp = expand_one_impl(tokens, pos, table, guard);

            // Reescaneo con lo que VIENE DETRAS.
            //
            // El estandar manda releer el resultado de una expansion junto con
            // los tokens que le siguen en el fuente, no solo por su cuenta.
            // Aqui se emitia el resultado directamente a la salida, con lo que
            // un nombre de macro funcion PRODUCIDO por una expansion ya no se
            // encontraba nunca con los parentesis que tenia al lado.
            //
            // Es justo el idioma con el que las cabeceras eligen macro segun el
            // numero de argumentos:
            //
            //     #define F(...) ELIGE(__VA_ARGS__, F3, F2, F1)(__VA_ARGS__)
            //
            // donde `ELIGE(...)` produce el NOMBRE y los parentesis que le
            // siguen son sus argumentos.  Sin el reescaneo la salida se quedaba
            // en `F2(a, b)` sin expandir; asi moria el <stdio.h> de macOS, que
            // lo usa para `__API_AVAILABLE`.
            //
            // Cada vuelta consume al menos la llamada que acaba de leer, asi
            // que esto termina siempre.
            //
            // Una macro no reentra en si misma por esta via: si lo que reaparece
            // es SU PROPIO nombre, se deja como esta.  Es la regla de la pintura
            // azul del estandar -- los tokens que salen de expandir `R` llevan
            // `R` tapado consigo -- y hace que `#define R(x) R` con `R(1)(2)`
            // de `R(2)`.  El guard normal ya se levanto al volver de la
            // expansion, asi que la reentrada solo era posible por aqui.
            //
            // Se comprueba sobre el nombre y no tapandolo durante la relectura:
            // taparlo dejaria oculta la macro para TODO lo que viene detras, y
            // entonces la segunda de dos llamadas iguales en lineas contiguas
            // se quedaba sin expandir.
            if (!exp.empty() && exp.back().type == PPTokenType::IDENT &&
                exp.back().value != nombre_previo &&
                !guard.count(exp.back().value)) {
                const MacroDef* siguiente = table.get(exp.back().value);
                size_t k = pos;
                while (k < tokens.size() &&
                       tokens[k].type == PPTokenType::WHITESPACE) ++k;

                if (siguiente && siguiente->is_function &&
                    k < tokens.size() &&
                    tokens[k].type == PPTokenType::LPAREN) {
                    // El nombre y los tokens que le siguen se releen JUNTOS.
                    std::vector<PPToken> resto;
                    resto.reserve(1 + tokens.size() - pos);
                    resto.push_back(exp.back());
                    resto.insert(resto.end(),
                                 tokens.begin() + static_cast<std::ptrdiff_t>(pos),
                                 tokens.end());
                    exp.pop_back();

                    result.insert(result.end(), exp.begin(), exp.end());
                    auto releido = expand_impl(resto, table, guard);
                    result.insert(result.end(), releido.begin(), releido.end());
                    pos = tokens.size();
                    continue;
                }
            }

            result.insert(result.end(), exp.begin(), exp.end());
        } else {
            result.push_back(tokens[pos++]);
        }
    }
    return result;
}

std::vector<PPToken> MacroTable::expand(const std::vector<PPToken>& tokens,
                                         const SourceLocation& call_loc) {
    std::unordered_map<std::string, bool> guard; // guard raiz (no recursivo)
    return expand_impl(tokens, *this, guard);
}

std::vector<PPToken> MacroTable::expand_one(
    const std::vector<PPToken>& tokens,
    size_t& pos,
    std::unordered_map<std::string, bool>& guard)
{
    return expand_one_impl(tokens, pos, *this, guard);
}

std::vector<std::vector<PPToken>> MacroTable::collect_args(
    const std::vector<PPToken>& tokens,
    size_t& pos,
    const MacroDef& mac,
    const SourceLocation& call_loc)
{
    // pos debe apuntar al '('
    if (pos >= tokens.size() || tokens[pos].type != PPTokenType::LPAREN) {
        m_diag.error(call_loc, "se esperaba '(' en llamada a macro funcion");
        return {};
    }
    pos++; // consume '('

    std::vector<std::vector<PPToken>> args;
    std::vector<PPToken> current_arg;
    int depth = 1; // profundidad de parentesis

    while (pos < tokens.size() && depth > 0) {
        const PPToken& t = tokens[pos];
        if (t.type == PPTokenType::LPAREN) {
            ++depth;
            current_arg.push_back(t);
        } else if (t.type == PPTokenType::RPAREN) {
            --depth;
            if (depth == 0) {
                args.push_back(std::move(current_arg));
                current_arg.clear();
                pos++;
                break;
            }
            current_arg.push_back(t);
        } else if (t.type == PPTokenType::COMMA && depth == 1) {
            // separador de argumento solo al nivel mas externo
            //
            // Con variadicas se trocea mientras queden parametros FIJOS por
            // llenar; a partir de ahi las comas pertenecen a __VA_ARGS__ y se
            // pegan al ultimo argumento.  `params` guarda solo los nombrados,
            // el `...` no esta ahi: restarle uno hacia que `L(a, ...)` no
            // troceara nunca (0 < 0 es falso) y que `M(...)` funcionase de
            // milagro, porque 0 - 1 hace underflow a SIZE_MAX y entonces
            // troceaba siempre.
            if (!mac.is_variadic ||
                args.size() < mac.params.size()) {
                args.push_back(std::move(current_arg));
                current_arg.clear();
            } else {
                // para variadic, agregar la coma al ultimo argumento
                current_arg.push_back(t);
            }
        } else {
            current_arg.push_back(t);
        }
        pos++;
    }

    // eliminar whitespace inicial y final de cada argumento (comportamiento estandar)
    for (auto& arg : args) {
        // Tambien los NEWLINE: una llamada repartida en varias lineas mete un
        // salto al principio del argumento siguiente, y sin recortarlo la
        // expansion arrastra un espacio de mas frente a lo que emite un
        // preprocesador de C.  El blanco que rodea a un argumento no forma
        // parte de el.
        auto is_blank = [](const PPToken& t) {
            return t.type == PPTokenType::WHITESPACE ||
                   t.type == PPTokenType::NEWLINE;
        };
        while (!arg.empty() && is_blank(arg.front()))
            arg.erase(arg.begin());
        while (!arg.empty() && is_blank(arg.back()))
            arg.pop_back();
    }

    // `F()` sobre una macro SIN parametros es una llamada sin argumentos, no
    // una llamada con un argumento vacio.  El bucle de arriba empuja el
    // argumento en curso al cerrar el parentesis aunque este vacio, asi que hay
    // que deshacer ese caso o la cuenta da 1 frente a 0 y se rechaza una
    // llamada perfectamente valida.  No es un caso rebuscado: las cabeceras del
    // SDK de macOS usan `_LIBC_SINGLE_BY_DEFAULT()`, definida asi, y sin esto
    // no se puede preprocesar ni un <stdio.h>.
    if (mac.params.empty() && !mac.is_variadic &&
        args.size() == 1 && args[0].empty()) {
        args.clear();
    }

    // verificar numero de argumentos
    size_t expected = mac.params.size();
    if (mac.is_variadic && expected > 0) --expected; // el '...' no cuenta como param fijo

    if (!mac.is_variadic && args.size() != mac.params.size()) {
        m_diag.error(call_loc,
            "numero incorrecto de argumentos en macro " + mac.name);
    }

    return args;
}

std::vector<PPToken> MacroTable::substitute(
    const std::vector<PPToken>& body,
    const std::vector<std::string>& params,
    const std::vector<std::vector<PPToken>>& raw_args,
    const std::vector<std::vector<PPToken>>& expanded_args,
    const SourceLocation& loc)
{
    std::vector<PPToken> result;
    result.reserve(body.size());

    // @brief Indica si la posicion i del cuerpo toca un `##`.
    //
    // Un parametro pegado con `##` usa su argumento SIN expandir; en cualquier
    // otro sitio usa el expandido.  De ahi que haya que mirar a los dos lados,
    // saltando los blancos que no forman parte del operador.
    auto touches_paste = [&body](size_t i) {
        size_t j = i + 1;
        while (j < body.size() && body[j].type == PPTokenType::WHITESPACE) ++j;
        if (j < body.size() && body[j].type == PPTokenType::HASHHASH) return true;
        if (i == 0) return false;
        size_t k = i;
        do {
            --k;
            if (body[k].type == PPTokenType::HASHHASH) return true;
            if (body[k].type != PPTokenType::WHITESPACE) return false;
        } while (k > 0);
        return false;
    };

    for (size_t i = 0; i < body.size(); ++i) {
        const PPToken& bt = body[i];
        if (bt.type != PPTokenType::IDENT) {
            result.push_back(bt);
            continue;
        }

        const auto& args = touches_paste(i) ? raw_args : expanded_args;

        // buscar si el ident es un parametro formal
        auto it = std::find(params.begin(), params.end(), bt.value);
        if (it != params.end()) {
            size_t idx = static_cast<size_t>(it - params.begin());
            if (idx < args.size()) {
                // insertar los tokens del argumento correspondiente
                result.insert(result.end(), args[idx].begin(), args[idx].end());
            }
        } else if (bt.value == "__VA_OPT__") {
            // __VA_OPT__(contenido): el contenido se emite solo si la parte
            // variadica trae ALGUN token.  Es la forma estandar de resolver la
            // coma colgante de `LOG(fmt, ...)` cuando se llama sin variadicos,
            // que sin esto genera C invalido.
            size_t j = i + 1;
            while (j < body.size() &&
                   body[j].type == PPTokenType::WHITESPACE) ++j;
            if (j >= body.size() || body[j].type != PPTokenType::LPAREN) {
                // sin parentesis no es el operador: se emite tal cual
                result.push_back(bt);
                continue;
            }

            // recoger el contenido respetando el anidamiento de parentesis
            std::vector<PPToken> inner;
            int depth = 1;
            size_t k = j + 1;
            for (; k < body.size() && depth > 0; ++k) {
                if (body[k].type == PPTokenType::LPAREN)  ++depth;
                if (body[k].type == PPTokenType::RPAREN) {
                    --depth;
                    if (depth == 0) break;
                }
                inner.push_back(body[k]);
            }

            bool has_varargs = false;
            for (size_t vi = params.size(); vi < raw_args.size(); ++vi) {
                if (!raw_args[vi].empty()) { has_varargs = true; break; }
            }

            if (has_varargs) {
                // El contenido puede llevar parametros y __VA_ARGS__, asi que
                // pasa por la misma sustitucion.
                auto sub = substitute(inner, params, raw_args,
                                      expanded_args, loc);
                result.insert(result.end(), sub.begin(), sub.end());
            }
            i = k;   // saltar hasta el ')' de cierre
        } else if (bt.value == "__VA_ARGS__") {
            // insertar todos los argumentos variadicos separados por coma
            for (size_t vi = params.size(); vi < args.size(); ++vi) {
                if (vi > params.size()) {
                    result.emplace_back(PPTokenType::COMMA, ",", loc);
                }
                result.insert(result.end(), args[vi].begin(), args[vi].end());
            }
        } else {
            result.push_back(bt);
        }
    }
    return result;
}

std::vector<PPToken> MacroTable::apply_token_paste(
    const std::vector<PPToken>& tokens)
{
    std::vector<PPToken> result;
    result.reserve(tokens.size());

    // @brief Blanco que puede rodear al operador sin formar parte de el.
    auto is_blank = [](const PPToken& t) {
        return t.type == PPTokenType::WHITESPACE ||
               t.type == PPTokenType::NEWLINE;
    };

    // Un operando de `##` puede quedar VACIO -- ocurre en cuanto se pasa un
    // argumento vacio, como en CAT(x,) -- y entonces el resultado del pegado es
    // simplemente el otro operando.  La version anterior daba por hecho que
    // siempre habia un token a cada lado y, al no encontrarlo, dejaba el `##`
    // literal en la salida: CAT(,) producia "## " en vez de nada.
    //
    // Se lleva un indicador en vez de mirar hacia adelante, porque asi el caso
    // "no hay izquierdo", "no hay derecho" y "no hay ninguno" salen solos.
    bool pending = false;   // se acaba de ver un ##

    for (const PPToken& t : tokens) {
        if (t.type == PPTokenType::HASHHASH) {
            // el blanco a ambos lados pertenece al operador
            while (!result.empty() && is_blank(result.back())) result.pop_back();
            pending = true;
            continue;
        }

        if (is_blank(t)) {
            if (!pending) result.push_back(t);
            continue;
        }

        if (pending) {
            pending = false;
            if (!result.empty() && !is_blank(result.back())) {
                // los dos operandos existen: se pegan de verdad
                const std::string joined = result.back().value + t.value;
                // El tipo se deduce del texto resultante: un `1 ## 2` da un
                // numero, no un identificador, y marcarlo mal lo expondria a
                // una expansion de macro que no le corresponde.
                const PPTokenType ty =
                    (!joined.empty() && std::isdigit(
                        static_cast<unsigned char>(joined[0])))
                    ? PPTokenType::NUMBER
                    : PPTokenType::IDENT;
                result.back() = PPToken(ty, joined, result.back().loc);
            } else {
                // operando izquierdo vacio: queda el derecho tal cual
                result.push_back(t);
            }
            continue;
        }

        result.push_back(t);
    }

    // Un `##` al final sin operando derecho no necesita nada: el izquierdo ya
    // esta en result.
    return result;
}

std::vector<PPToken> MacroTable::apply_stringify(
    const std::vector<PPToken>& tokens,
    const std::vector<std::string>& params,
    const std::vector<std::vector<PPToken>>& args)
{
    std::vector<PPToken> result;
    result.reserve(tokens.size());
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].type == PPTokenType::HASH && i + 1 < tokens.size()) {
            size_t j = i + 1;
            while (j < tokens.size() && tokens[j].type == PPTokenType::WHITESPACE) ++j;
            if (j < tokens.size() && tokens[j].type == PPTokenType::IDENT) {
                // #__VA_ARGS__: se estringifica TODA la parte variadica, con
                // sus comas incluidas.  No es un parametro con nombre, asi que
                // la busqueda de abajo no lo encontraba y el # se colaba
                // literal en la salida.
                if (tokens[j].value == "__VA_ARGS__") {
                    std::vector<PPToken> todos;
                    for (size_t vi = params.size(); vi < args.size(); ++vi) {
                        if (vi > params.size()) {
                            todos.emplace_back(PPTokenType::COMMA, ",",
                                               tokens[j].loc);
                            todos.emplace_back(PPTokenType::WHITESPACE, " ",
                                               tokens[j].loc);
                        }
                        todos.insert(todos.end(), args[vi].begin(),
                                     args[vi].end());
                    }
                    result.push_back(make_string(tokens_raw(todos),
                                                 tokens[i].loc));
                    i = j + 1;
                    continue;
                }

                auto it = std::find(params.begin(), params.end(), tokens[j].value);
                if (it != params.end()) {
                    size_t idx = static_cast<size_t>(it - params.begin());
                    std::string raw = (idx < args.size()) ? tokens_raw(args[idx]) : "";
                    result.push_back(make_string(raw, tokens[i].loc));
                    i = j + 1;
                    continue;
                }
            }
        }
        result.push_back(tokens[i++]);
    }
    return result;
}

} // namespace vpp
