/**
 * @file pp_token.h
 * @brief Definicion de tokens y ubicacion de fuente para el preprocesador vpp.
 */
#pragma once

#include "pp_name_pool.h"

#include <string>
#include <cstdint>

namespace vpp {

/**
 * @brief Ubicacion de un token en el fuente.
 *
 * El nombre del fichero NO se guarda por valor sino como puntero a un nombre
 * compartido (ver pp_name_pool.h).  Es lo que hace que copiar una ubicacion
 * -- cosa que pasa millones de veces, porque cada token lleva la suya y los
 * tokens se copian mucho -- no reserve memoria.  Guardarlo por valor la
 * reservaba siempre: una ruta de cabecera no cabe en el buffer pequeno de
 * `std::string`.
 */
struct SourceLocation {
    const std::string* file_name; ///< Nombre del fichero, compartido; nunca nulo
    uint32_t           line;      ///< Numero de linea (base 1)
    uint32_t           col;       ///< Columna (base 1)

    /** @brief Constructor por defecto: sin fichero, linea 1, columna 1. */
    SourceLocation() noexcept
        : file_name(empty_file_name()), line(1), col(1) {}

    /**
     * @brief Constructor a partir de un nombre ya compartido.
     *
     * Es el camino rapido y el que usa quien produce tokens: se interna el
     * nombre una vez al abrir el fichero y desde ahi cada ubicacion solo copia
     * un puntero.
     *
     * @param f Nombre compartido; nulo se toma como "sin fichero".
     * @param l Numero de linea.
     * @param c Columna.
     */
    SourceLocation(const std::string* f, uint32_t l, uint32_t c) noexcept
        : file_name(f ? f : empty_file_name()), line(l), col(c) {}

    /**
     * @brief Constructor a partir de un nombre suelto, que se interna.
     *
     * Comodo para los pocos sitios que construyen una ubicacion aislada.  NO
     * debe usarse por token: internar toma un cerrojo, y a esa frecuencia se
     * notaria.
     *
     * @param f Nombre del fichero.
     * @param l Numero de linea.
     * @param c Columna.
     */
    SourceLocation(const std::string& f, uint32_t l, uint32_t c)
        : file_name(intern_file_name(f)), line(l), col(c) {}

    /** @brief Nombre del fichero. @return El nombre, vacio si no hay. */
    const std::string& file() const noexcept { return *file_name; }

    /**
     * @brief Devuelve la ubicacion como cadena "archivo:linea:col".
     * @return Cadena con la ubicacion formateada.
     */
    std::string to_string() const;
};

/**
 * @brief Tipos de token reconocidos por el preprocesador.
 *
 * Los tokens de tipo TEXT representan contenido plano que se pasa directamente
 * a la salida (despues de expansion de macros). Los demas tokens pertenecen
 * a lineas de directiva.
 */
enum class PPTokenType : uint8_t {
    // --- literales e identificadores ---
    IDENT,          ///< Identificador: [a-zA-Z_][a-zA-Z0-9_]*
    NUMBER,         ///< Literal numerico (entero o flotante)
    STRING,         ///< Literal de cadena entre comillas dobles "..."
    CHAR_LIT,       ///< Literal de caracter entre comillas simples '.'
    ANGLE_STRING,   ///< Cadena de angulo <...>, solo valida en #include

    // --- operadores aritmeticos ---
    PLUS,           ///< +
    MINUS,          ///< -
    STAR,           ///< *
    SLASH,          ///< /
    PERCENT,        ///< %
    AMP,            ///< &
    PIPE,           ///< |
    CARET,          ///< ^
    TILDE,          ///< ~
    BANG,           ///< !

    // --- operadores de desplazamiento ---
    LSHIFT,         ///< <<
    RSHIFT,         ///< >>

    // --- operadores de comparacion ---
    EQ,             ///< ==
    NEQ,            ///< !=
    LT,             ///< <
    GT,             ///< >
    LEQ,            ///< <=
    GEQ,            ///< >=

    // --- operadores logicos ---
    AND,            ///< &&
    OR,             ///< ||

    // --- operador ternario ---
    QUESTION,       ///< ?
    COLON,          ///< :

    // --- delimitadores y operadores especiales ---
    LPAREN,         ///< (
    RPAREN,         ///< )
    LBRACKET,       ///< [
    RBRACKET,       ///< ]
    COMMA,          ///< ,
    HASH,           ///< # al inicio de directiva
    HASHHASH,       ///< ## operador de pegado de tokens en macros
    ELLIPSIS,       ///< ... (parametro variadic en macros)
    BACKSLASH,      ///< \ continuacion de linea
    ASSIGN,         ///< = en contextos de directiva

    // --- control del flujo del lexer ---
    NEWLINE,        ///< Fin de linea logica
    WHITESPACE,     ///< Espacios/tabulaciones (preservado en cuerpo de macros)
    TEXT,           ///< Bloque de texto plano a pasar a la salida
    PP_EOF,         ///< Fin de archivo
    UNKNOWN         ///< Caracter no reconocido
};

/**
 * @brief Convierte un tipo de token a su nombre legible en ASCII.
 * @param t Tipo de token a convertir.
 * @return Puntero a cadena estatica con el nombre.
 */
const char* pp_token_type_name(PPTokenType t);

/**
 * @brief Un token del preprocesador con su tipo, valor textual y ubicacion.
 */
struct PPToken {
    PPTokenType    type;    ///< Tipo del token
    std::string    value;   ///< Valor textual del token tal como aparece en fuente
    SourceLocation loc;     ///< Ubicacion en el archivo fuente

    /** @brief Constructor por defecto: token UNKNOWN vacio. */
    PPToken() : type(PPTokenType::UNKNOWN) {}

    /**
     * @brief Constructor principal.
     * @param t Tipo de token.
     * @param v Valor textual.
     * @param l Ubicacion en fuente.
     */
    PPToken(PPTokenType t, std::string v, SourceLocation l)
        : type(t), value(std::move(v)), loc(std::move(l)) {}

    /**
     * @brief Indica si el token es fin de archivo.
     * @return true si el tipo es PP_EOF.
     */
    bool is_eof()     const noexcept { return type == PPTokenType::PP_EOF; }

    /**
     * @brief Indica si el token es fin de linea.
     * @return true si el tipo es NEWLINE.
     */
    bool is_newline() const noexcept { return type == PPTokenType::NEWLINE; }

    /**
     * @brief Indica si el token es un identificador con un nombre dado.
     * @param name Nombre a comparar.
     * @return true si es IDENT y el valor coincide con name.
     */
    bool is_ident(const char* name) const noexcept {
        return type == PPTokenType::IDENT && value == name;
    }
};

} // namespace vpp
