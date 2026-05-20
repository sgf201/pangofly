#include "lexer.h"
#include <cctype>
#include <unordered_map>

namespace pangofly {
namespace idl {

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"namespace", TokenType::KEYWORD_NAMESPACE},
    {"struct", TokenType::KEYWORD_STRUCT},
    {"bool", TokenType::KEYWORD_BOOL},
    {"int8", TokenType::KEYWORD_INT8},
    {"uint8", TokenType::KEYWORD_UINT8},
    {"int16", TokenType::KEYWORD_INT16},
    {"uint16", TokenType::KEYWORD_UINT16},
    {"int32", TokenType::KEYWORD_INT32},
    {"uint32", TokenType::KEYWORD_UINT32},
    {"int64", TokenType::KEYWORD_INT64},
    {"uint64", TokenType::KEYWORD_UINT64},
    {"float32", TokenType::KEYWORD_FLOAT32},
    {"float64", TokenType::KEYWORD_FLOAT64},
    {"string", TokenType::KEYWORD_STRING},
    {"vector", TokenType::KEYWORD_VECTOR},
    {"array", TokenType::KEYWORD_ARRAY},
};

Lexer::Lexer(const std::string& source)
    : source_(source), pos_(0), line_(1), column_(1) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (pos_ < source_.size()) {
        skip_whitespace();
        if (pos_ >= source_.size()) break;
        
        char c = source_[pos_];
        uint32_t start_line = line_;
        uint32_t start_col = column_;
        
        // 跳过单行注释
        if (c == '/' && peek_next() == '/') {
            skip_comment();
            continue;
        }
        
        // 标识符或关键字
        if (is_alpha(c) || c == '_') {
            Token token = read_identifier();
            tokens.push_back(token);
        }
        // 数字
        else if (is_digit(c) || (c == '.' && is_digit(peek_next()))) {
            Token token = read_number();
            tokens.push_back(token);
        }
        // 字符串
        else if (c == '"') {
            Token token = read_string();
            tokens.push_back(token);
        }
        // 符号
        else {
            switch (c) {
                case '{': tokens.push_back(make_token(TokenType::LBRACE, "{")); advance(); break;
                case '}': tokens.push_back(make_token(TokenType::RBRACE, "}")); advance(); break;
                case '[': tokens.push_back(make_token(TokenType::LBRACKET, "[")); advance(); break;
                case ']': tokens.push_back(make_token(TokenType::RBRACKET, "]")); advance(); break;
                case '(': tokens.push_back(make_token(TokenType::LPAREN, "(")); advance(); break;
                case ')': tokens.push_back(make_token(TokenType::RPAREN, ")")); advance(); break;
                case '<': tokens.push_back(make_token(TokenType::LESS, "<")); advance(); break;
                case '>': tokens.push_back(make_token(TokenType::GREATER, ">")); advance(); break;
                case ',': tokens.push_back(make_token(TokenType::COMMA, ",")); advance(); break;
                case ';': tokens.push_back(make_token(TokenType::SEMICOLON, ";")); advance(); break;
                case ':': tokens.push_back(make_token(TokenType::COLON, ":")); advance(); break;
                case '.': tokens.push_back(make_token(TokenType::DOT, ".")); advance(); break;
                case '&': tokens.push_back(make_token(TokenType::AMPERSAND, "&")); advance(); break;
                case '*': tokens.push_back(make_token(TokenType::ASTERISK, "*")); advance(); break;
                case '?': tokens.push_back(make_token(TokenType::QUESTION, "?")); advance(); break;
                case '=':
                    advance();
                    tokens.push_back(make_token(TokenType::LESS, "="));
                    break;
                default:
                    report_error("Unexpected character: " + std::string(1, c));
                    advance();
                    break;
            }
        }
    }
    
    tokens.push_back(make_token(TokenType::EOF_TOKEN, ""));
    return tokens;
}

void Lexer::skip_whitespace() {
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') {
                line_++;
                column_ = 1;
            } else {
                column_++;
            }
            pos_++;
        } else {
            break;
        }
    }
}

void Lexer::skip_comment() {
    while (pos_ < source_.size() && source_[pos_] != '\n') {
        advance();
    }
}

Token Lexer::read_identifier() {
    uint32_t start_line = line_;
    uint32_t start_col = column_;
    std::string text;
    
    while (pos_ < source_.size() && (is_alnum(source_[pos_]) || source_[pos_] == '_')) {
        text += advance();
    }
    
    auto it = KEYWORDS.find(text);
    if (it != KEYWORDS.end()) {
        return make_token(it->second, text);
    }
    
    return make_token(TokenType::IDENTIFIER, text);
}

Token Lexer::read_number() {
    uint32_t start_line = line_;
    uint32_t start_col = column_;
    std::string text;
    bool is_float = false;
    
    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (is_digit(c) || c == '_') {
            text += advance();
        } else if (c == '.' && !is_float) {
            is_float = true;
            text += advance();
        } else if ((c == 'e' || c == 'E') && !text.empty()) {
            text += advance();
            if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) {
                text += advance();
            }
        } else {
            break;
        }
    }
    
    return make_token(is_float ? TokenType::FLOAT_LITERAL : TokenType::INTEGER_LITERAL, text);
}

Token Lexer::read_string() {
    uint32_t start_line = line_;
    uint32_t start_col = column_;
    std::string text;
    advance(); // 跳过开始引号
    
    while (pos_ < source_.size() && source_[pos_] != '"') {
        if (source_[pos_] == '\\' && pos_ + 1 < source_.size()) {
            advance(); // 跳过转义符
            char esc = advance();
            switch (esc) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case 'r': text += '\r'; break;
                case '"': text += '"'; break;
                case '\\': text += '\\'; break;
                default: text += esc; break;
            }
        } else if (source_[pos_] == '\n') {
            report_error("Unterminated string literal");
            break;
        } else {
            text += advance();
        }
    }
    
    if (pos_ < source_.size() && source_[pos_] == '"') {
        advance(); // 跳过结束引号
    }
    
    return make_token(TokenType::STRING_LITERAL, text);
}

char Lexer::peek() {
    if (pos_ < source_.size()) {
        return source_[pos_];
    }
    return '\0';
}

char Lexer::peek_next() {
    if (pos_ + 1 < source_.size()) {
        return source_[pos_ + 1];
    }
    return '\0';
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

bool Lexer::is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

Token Lexer::make_token(TokenType type, const std::string& text) {
    return Token(type, text, line_, column_);
}

void Lexer::report_error(const std::string& msg) {
    errors_.push_back("Line " + std::to_string(line_) + 
                      ", Column " + std::to_string(column_) + 
                      ": " + msg);
}

} // namespace idl
} // namespace pangofly
