#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

namespace pangofly {
namespace idl {

enum class TypeKind {
    PRIMITIVE_BOOL,
    PRIMITIVE_INT8,
    PRIMITIVE_UINT8,
    PRIMITIVE_INT16,
    PRIMITIVE_UINT16,
    PRIMITIVE_INT32,
    PRIMITIVE_UINT32,
    PRIMITIVE_INT64,
    PRIMITIVE_UINT64,
    PRIMITIVE_FLOAT32,
    PRIMITIVE_FLOAT64,
    USER_DEF,
    VECTOR,
    STRING,
    FIXED_ARRAY,
};

struct Type {
    TypeKind kind;
    std::string name;  // 用于用户定义类型或模板参数名
    
    Type(TypeKind k) : kind(k) {}
    Type(TypeKind k, const std::string& n) : kind(k), name(n) {}
    virtual ~Type() = default;
    
    bool is_primitive() const {
        return kind >= TypeKind::PRIMITIVE_BOOL && 
               kind <= TypeKind::PRIMITIVE_FLOAT64;
    }
    
    bool is_vector() const { return kind == TypeKind::VECTOR; }
    bool is_string() const { return kind == TypeKind::STRING; }
    bool is_user_defined() const { return kind == TypeKind::USER_DEF; }
    bool is_fixed_array() const { return kind == TypeKind::FIXED_ARRAY; }
};

struct FixedArrayType : public Type {
    std::shared_ptr<Type> element_type;
    size_t size;
    
    FixedArrayType(std::shared_ptr<Type> elem_type, size_t n)
        : Type(TypeKind::FIXED_ARRAY), element_type(std::move(elem_type)), size(n) {}
};

struct VectorType : public Type {
    std::shared_ptr<Type> element_type;
    
    VectorType(std::shared_ptr<Type> elem_type)
        : Type(TypeKind::VECTOR), element_type(std::move(elem_type)) {}
};

struct Field {
    std::shared_ptr<Type> type;
    std::string name;
    std::optional<std::string> default_value;
    
    Field(std::shared_ptr<Type> t, const std::string& n)
        : type(std::move(t)), name(n) {}
};

struct Struct : public Type {
    std::string qualified_name;
    std::vector<Field> fields;
    std::string namespace_name;
    
    Struct(const std::string& qname, const std::string& ns)
        : Type(TypeKind::USER_DEF, qname), 
          qualified_name(qname), 
          namespace_name(ns) {}
    
    void add_field(Field&& f) {
        fields.push_back(std::move(f));
    }
};

struct Namespace {
    std::string name;
    std::vector<std::shared_ptr<Struct>> structs;
    
    explicit Namespace(const std::string& n) : name(n) {}
    
    void add_struct(std::shared_ptr<Struct> s) {
        structs.push_back(std::move(s));
    }
};

struct IDLDocument {
    std::vector<Namespace> namespaces;
    
    std::shared_ptr<Struct> find_struct(const std::string& qualified_name) const;
    std::shared_ptr<Namespace> find_namespace(const std::string& name) const;
    
    void add_namespace(Namespace&& ns) {
        namespaces.push_back(std::move(ns));
    }
};

// 类型映射表
inline std::string to_cpp_type(const Type& t) {
    switch (t.kind) {
        case TypeKind::PRIMITIVE_BOOL:    return "bool";
        case TypeKind::PRIMITIVE_INT8:   return "int8_t";
        case TypeKind::PRIMITIVE_UINT8:  return "uint8_t";
        case TypeKind::PRIMITIVE_INT16:   return "int16_t";
        case TypeKind::PRIMITIVE_UINT16:  return "uint16_t";
        case TypeKind::PRIMITIVE_INT32:   return "int32_t";
        case TypeKind::PRIMITIVE_UINT32:  return "uint32_t";
        case TypeKind::PRIMITIVE_INT64:   return "int64_t";
        case TypeKind::PRIMITIVE_UINT64:  return "uint64_t";
        case TypeKind::PRIMITIVE_FLOAT32: return "float";
        case TypeKind::PRIMITIVE_FLOAT64: return "double";
        case TypeKind::USER_DEF:          return t.name;
        case TypeKind::STRING:            return "pangofly::String";
        case TypeKind::VECTOR:            return "pangofly::Vector<" + 
                                              std::dynamic_pointer_cast<VectorType>(
                                                  std::make_shared<Type>(t))->element_type->name + ">";
        case TypeKind::FIXED_ARRAY:       return "std::array<" + t.name + ", " + 
                                              std::to_string(
                                                  std::dynamic_pointer_cast<FixedArrayType>(
                                                      std::make_shared<Type>(t))->size) + ">";
        default:                          return "void";
    }
}

inline size_t primitive_size(TypeKind kind) {
    switch (kind) {
        case TypeKind::PRIMITIVE_BOOL:
        case TypeKind::PRIMITIVE_INT8:
        case TypeKind::PRIMITIVE_UINT8:   return 1;
        case TypeKind::PRIMITIVE_INT16:
        case TypeKind::PRIMITIVE_UINT16:  return 2;
        case TypeKind::PRIMITIVE_INT32:
        case TypeKind::PRIMITIVE_UINT32:
        case TypeKind::PRIMITIVE_FLOAT32: return 4;
        case TypeKind::PRIMITIVE_INT64:
        case TypeKind::PRIMITIVE_UINT64:
        case TypeKind::PRIMITIVE_FLOAT64: return 8;
        default:                          return 0;
    }
}

} // namespace idl
} // namespace pangofly
