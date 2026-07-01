#pragma once

#include "../ast/ast.h"
#include <string>
#include <sstream>
#include <vector>

namespace pangofly {
namespace idl {

class CodeGenerator {
public:
    explicit CodeGenerator(const std::shared_ptr<IDLDocument>& document);
    
    std::string generate();
    
private:
    std::string generate_header();
    std::string generate_includes();
    std::string generate_namespace_open(const std::string& ns);
    std::string generate_namespace_close(const std::string& ns);
    
    std::string generate_struct(const Struct& s);
    std::string generate_field(const Field& f);
    std::string generate_getter(const Field& f);
    std::string generate_type_name(const Type& t);
    
    std::shared_ptr<IDLDocument> document_;
};

inline std::string generate_header_code(const std::shared_ptr<IDLDocument>& document) {
    CodeGenerator generator(document);
    return generator.generate();
}

} // namespace idl
} // namespace pangofly
