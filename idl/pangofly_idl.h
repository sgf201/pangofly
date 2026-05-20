#pragma once

#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "codegen/code_generator.h"
#include "container/vector.h"
#include "container/string.h"
#include "allocator/slab_allocator.h"

#define PANGOFLY_IDL_VERSION "1.0.0"

namespace pangofly {

class IDLCompiler {
public:
    IDLCompiler() = default;
    
    bool compile(const std::string& idl_source, std::string* output);
    bool compile_file(const std::string& input_file, const std::string& output_file);
    
    const std::vector<std::string>& get_errors() const { return errors_; }
    
private:
    std::vector<std::string> errors_;
};

} // namespace pangofly
