#pragma once

#include "lexer/lexer.h"
#include "ast/ast.h"
#include <vector>
#include <string>
#include <optional>

namespace pangofly {
namespace idl {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    
    std::shared_ptr<IDLDocument> parse();
    const std::vector<std::string>& get_errors() const { return errors_; }
    
private:
    // 解析入口
    void parse_document();
    
    // 命名空间解析
    void parse_namespace();
    std::string parse_qualified_name();
    
    // 结构体解析
    std::shared_ptr<Struct> parse_struct(const std::string& namespace_name);
    Field parse_field();
    
    // 类型解析
    std::shared_ptr<Type> parse_type();
    std::shared_ptr<Type> parse_primitive_type();
    std::shared_ptr<Type> parse_user_type();
    std::shared_ptr<Type> parse_vector_type();
    std::shared_ptr<Type> parse_string_type();
    std::shared_ptr<Type> parse_fixed_array_type();
    
    // 工具函数
    bool check(TokenType type);
    bool check_keyword(const std::string& keyword);
    bool match(TokenType type);
    bool match_keyword(const std::string& keyword);
    const Token& advance();
    const Token& peek(size_t offset = 0);
    const Token& previous();
    
    void report_error(const std::string& msg);
    void report_error_at(const Token& token, const std::string& msg);
    
    bool is_at_end() const;
    
    const std::vector<Token>& tokens_;
    size_t current_;
    std::vector<std::string> errors_;
    std::shared_ptr<IDLDocument> document_;
};

std::shared_ptr<IDLDocument> parse_idl(const std::string& source);

} // namespace idl
} // namespace pangofly
