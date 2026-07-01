#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace pangofly {
namespace idl {

enum class TokenType {
    // 关键字 - OMG IDL 4.2 标准
    KEYWORD_MODULE,
    KEYWORD_STRUCT,
    KEYWORD_BOOLEAN,
    KEYWORD_OCTET,
    KEYWORD_SHORT,
    KEYWORD_LONG,
    KEYWORD_UNSIGNED,
    KEYWORD_FLOAT,
    KEYWORD_DOUBLE,
    KEYWORD_CHAR,
    KEYWORD_STRING,
    KEYWORD_SEQUENCE,
    KEYWORD_ENUM,
    KEYWORD_TYPEDEF,
    
    // 标识符和字面量
    IDENTIFIER,
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    
    // 符号
    LBRACE,           // {
    RBRACE,           // }
    LBRACKET,         // [
    RBRACKET,         // ]
    LPAREN,           // (
    RPAREN,           // )
    LESS,             // <
    GREATER,          // >
    COMMA,            // ,
    SEMICOLON,        // ;
    COLON,            // :
    DOT,              // .
    AMPERSAND,        // &
    ASTERISK,         // *
    QUESTION,         // ?
    
    // 特殊标记
    EOF_TOKEN,
    INVALID_TOKEN
};

struct Token {
    TokenType type;
    std::string text;
    uint32_t line;
    uint32_t column;
    
    Token(TokenType t, const std::string& txt, uint32_t ln, uint32_t col)
        : type(t), text(txt), line(ln), column(col) {}
};

class Lexer {
public:
    explicit Lexer(const std::string& source);
    
    std::vector<Token> tokenize();
    const std::vector<std::string>& get_errors() const { return errors_; }
    
private:
    void skip_whitespace();
    void skip_comment();
    
    Token read_identifier();
    Token read_number();
    Token read_string();
    
    char peek();
    char peek_next();
    char advance();
    
    bool is_alpha(char c);
    bool is_alnum(char c);
    bool is_digit(char c);
    bool is_whitespace(char c);
    
    Token make_token(TokenType type, const std::string& text);
    void report_error(const std::string& msg);
    
    const std::string& source_;
    size_t pos_;
    uint32_t line_;
    uint32_t column_;
    std::vector<std::string> errors_;
};

} // namespace idl
} // namespace pangofly
