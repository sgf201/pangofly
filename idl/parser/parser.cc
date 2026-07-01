#include "parser.h"
#include <algorithm>
#include <unordered_map>

namespace pangofly {
namespace idl {

static const std::unordered_map<std::string, TokenType> KEYWORD_TOKEN_MAP = {
    // OMG IDL 4.2 标准关键字
    {"module", TokenType::KEYWORD_MODULE},
    {"struct", TokenType::KEYWORD_STRUCT},
    {"boolean", TokenType::KEYWORD_BOOLEAN},
    {"octet", TokenType::KEYWORD_OCTET},
    {"short", TokenType::KEYWORD_SHORT},
    {"long", TokenType::KEYWORD_LONG},
    {"unsigned", TokenType::KEYWORD_UNSIGNED},
    {"float", TokenType::KEYWORD_FLOAT},
    {"double", TokenType::KEYWORD_DOUBLE},
    {"char", TokenType::KEYWORD_CHAR},
    {"string", TokenType::KEYWORD_STRING},
    {"sequence", TokenType::KEYWORD_SEQUENCE},
    {"enum", TokenType::KEYWORD_ENUM},
    {"typedef", TokenType::KEYWORD_TYPEDEF},
};

Parser::Parser(const std::vector<Token>& tokens)
    : tokens_(tokens), current_(0) {}

std::shared_ptr<IDLDocument> Parser::parse() {
    document_ = std::make_shared<IDLDocument>();
    
    while (!is_at_end()) {
        if (match_keyword("module")) {
            parse_namespace();
        } else if (match_keyword("struct")) {
            // 顶级结构体（无命名空间）
            auto s = parse_struct("");
            if (s) {
                Namespace ns("");
                ns.add_struct(s);
                document_->add_namespace(std::move(ns));
            }
        } else {
            report_error("Unexpected token: " + peek().text);
            advance();
        }
    }
    
    return document_;
}

void Parser::parse_namespace() {
    if (!match(TokenType::IDENTIFIER)) {
        report_error("Expected namespace name");
        return;
    }
    
    std::string ns_name = previous().text;
    
    if (!match(TokenType::LBRACE)) {
        report_error("Expected '{' after namespace name");
        return;
    }
    
    Namespace ns(ns_name);
    
    while (!check(TokenType::RBRACE) && !is_at_end()) {
        if (match_keyword("struct")) {
            auto s = parse_struct(ns_name);
            if (s) {
                ns.add_struct(s);
            }
        } else {
            report_error("Expected 'struct' in namespace");
            advance();
        }
    }
    
    if (!match(TokenType::RBRACE)) {
        report_error("Expected '}' after namespace");
    }
    
    document_->add_namespace(std::move(ns));
}

std::shared_ptr<Struct> Parser::parse_struct(const std::string& namespace_name) {
    if (!match(TokenType::IDENTIFIER)) {
        report_error("Expected struct name");
        return nullptr;
    }
    
    std::string struct_name = previous().text;
    std::string qualified_name = namespace_name.empty() ? 
                                  struct_name : 
                                  namespace_name + "::" + struct_name;
    
    if (!match(TokenType::LBRACE)) {
        report_error("Expected '{' after struct name");
        return nullptr;
    }
    
    auto struct_node = std::make_shared<Struct>(qualified_name, namespace_name);
    
    while (!check(TokenType::RBRACE) && !is_at_end()) {
        auto field = parse_field();
        struct_node->add_field(std::move(field));
    }
    
    if (!match(TokenType::RBRACE)) {
        report_error("Expected '}' after struct body");
    }
    
    if (!match(TokenType::SEMICOLON)) {
        report_error("Expected ';' after struct definition");
    }
    
    return struct_node;
}

Field Parser::parse_field() {
    auto field_type = parse_type();
    
    if (!match(TokenType::IDENTIFIER)) {
        report_error("Expected field name");
        return Field(std::move(field_type), "");
    }
    
    std::string field_name = previous().text;
    
    if (!match(TokenType::SEMICOLON)) {
        report_error("Expected ';' after field declaration");
    }
    
    return Field(std::move(field_type), field_name);
}

std::shared_ptr<Type> Parser::parse_type() {
    // unsigned 前缀类型
    if (match(TokenType::KEYWORD_UNSIGNED)) {
        if (match(TokenType::KEYWORD_SHORT))
            return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT16);
        if (match(TokenType::KEYWORD_LONG)) {
            // unsigned long long 或 unsigned long
            if (match(TokenType::KEYWORD_LONG))
                return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT64);
            return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT32);
        }
        report_error("Expected 'short' or 'long' after 'unsigned'");
        return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT32);
    }
    
    // long long 类型
    if (check(TokenType::KEYWORD_LONG)) {
        // 预读下一个 token 是否也是 long
        if (peek(1).type == TokenType::KEYWORD_LONG) {
            advance(); // 第一个 long
            advance(); // 第二个 long
            return std::make_shared<Type>(TypeKind::PRIMITIVE_INT64);
        }
        advance();
        return std::make_shared<Type>(TypeKind::PRIMITIVE_INT32);
    }
    
    // 标准 OMG IDL 基本类型
    if (match(TokenType::KEYWORD_BOOLEAN)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_BOOL);
    if (match(TokenType::KEYWORD_OCTET)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT8);
    if (match(TokenType::KEYWORD_SHORT)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_INT16);
    if (match(TokenType::KEYWORD_FLOAT)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_FLOAT32);
    if (match(TokenType::KEYWORD_DOUBLE)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_FLOAT64);
    if (match(TokenType::KEYWORD_CHAR)) 
        return std::make_shared<Type>(TypeKind::PRIMITIVE_UINT8);
    
    // sequence 类型
    if (match(TokenType::KEYWORD_SEQUENCE)) {
        if (!match(TokenType::LESS)) {
            report_error("Expected '<' after 'sequence'");
            return std::make_shared<Type>(TypeKind::PRIMITIVE_INT32);
        }
        
        auto elem_type = parse_type();
        
        if (!match(TokenType::GREATER)) {
            report_error("Expected '>' after sequence element type");
            return std::make_shared<Type>(TypeKind::PRIMITIVE_INT32);
        }
        
        return std::make_shared<VectorType>(std::move(elem_type));
    }
    
    // String 类型
    if (match(TokenType::KEYWORD_STRING)) {
        return std::make_shared<Type>(TypeKind::STRING);
    }
    
    // 用户定义类型
    if (match(TokenType::IDENTIFIER)) {
        return std::make_shared<Type>(TypeKind::USER_DEF, previous().text);
    }
    
    report_error("Expected type");
    return std::make_shared<Type>(TypeKind::PRIMITIVE_INT32);
}

bool Parser::check(TokenType type) {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool Parser::check_keyword(const std::string& keyword) {
    if (is_at_end()) return false;
    const auto& token = peek();
    if (token.type == TokenType::IDENTIFIER && token.text == keyword) {
        return true;
    }
    auto it = KEYWORD_TOKEN_MAP.find(keyword);
    if (it != KEYWORD_TOKEN_MAP.end() && token.type == it->second) {
        return true;
    }
    return false;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match_keyword(const std::string& keyword) {
    if (check_keyword(keyword)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::advance() {
    if (!is_at_end()) current_++;
    return previous();
}

const Token& Parser::peek(size_t offset) {
    size_t idx = current_ + offset;
    if (idx >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[idx];
}

const Token& Parser::previous() {
    return tokens_[current_ - 1];
}

void Parser::report_error(const std::string& msg) {
    const auto& token = peek();
    errors_.push_back("Line " + std::to_string(token.line) + 
                      ", Column " + std::to_string(token.column) + 
                      ": " + msg);
}

void Parser::report_error_at(const Token& token, const std::string& msg) {
    errors_.push_back("Line " + std::to_string(token.line) + 
                      ", Column " + std::to_string(token.column) + 
                      ": " + msg);
}

bool Parser::is_at_end() const {
    return current_ >= tokens_.size() || 
           tokens_[current_].type == TokenType::EOF_TOKEN;
}

std::shared_ptr<IDLDocument> parse_idl(const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    Parser parser(tokens);
    auto document = parser.parse();
    
    return document;
}

} // namespace idl
} // namespace pangofly
