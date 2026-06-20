#!/usr/bin/env python3
"""
IDL to C++ Header Generator for Pangofly
Generates zero-copy message types with custom allocator support
"""

import re
import sys
import os
from pathlib import Path

def parse_idl(idl_content):
    """Parse IDL content and extract struct definitions"""
    structs = []
    
    # Find module
    module_match = re.search(r'module\s+(\w+)\s*\{', idl_content)
    module_name = module_match.group(1) if module_match else ""
    
    # Find all struct definitions
    struct_pattern = r'struct\s+(\w+)\s*\{([^}]+)\}'
    for match in re.finditer(struct_pattern, idl_content):
        struct_name = match.group(1)
        fields_text = match.group(2)
        
        fields = []
        for line in fields_text.strip().split('\n'):
            line = line.strip()
            if not line or line.startswith('//'):
                continue
            
            # Parse field: type name; or sequence<type> name;
            seq_match = re.match(r'sequence<(\w+)>\s+(\w+);', line)
            if seq_match:
                fields.append({
                    'type': seq_match.group(1),
                    'name': seq_match.group(2),
                    'is_sequence': True
                })
            else:
                field_match = re.match(r'(\w+)\s+(\w+);', line)
                if field_match:
                    fields.append({
                        'type': field_match.group(1),
                        'name': field_match.group(2),
                        'is_sequence': False
                    })
        
        structs.append({
            'name': struct_name,
            'fields': fields
        })
    
    return module_name, structs

def generate_header(module_name, structs, output_path):
    """Generate C++ header file from parsed IDL"""

    header_name = Path(output_path).stem

    # Type mapping from IDL to C++
    type_map = {
        'int8': 'int8_t',
        'int16': 'int16_t',
        'int32': 'int32_t',
        'int64': 'int64_t',
        'uint8': 'uint8_t',
        'uint16': 'uint16_t',
        'uint32': 'uint32_t',
        'uint64': 'uint64_t',
        'float': 'float',
        'double': 'double',
        'bool': 'bool',
        'string': 'std::string'
    }

    content = f'''// Auto-generated from IDL
// DO NOT EDIT MANUALLY
// Module: {module_name}

#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include "idl/container/vector.h"
#include "idl/allocator/block_allocator.h"

namespace {module_name} '''

    content += '{\n\n'

    # Generate each struct
    for struct in structs:
        content += f'// {struct["name"]} - generated from IDL\n'
        content += f'struct {struct["name"]} {{\n'

        # Generate static type name
        content += f'    static const char* GetTypeName() {{ return "{module_name}::{struct["name"]}"; }}\n\n'

        # Generate fields
        for field in struct['fields']:
            cpp_type = type_map.get(field['type'], field['type'])
            if field['is_sequence']:
                content += f'    pangofly::Vector<{cpp_type}> {field["name"]};\n'
            else:
                content += f'    {cpp_type} {field["name"]};\n'

        content += '\n'

        # Generate default constructor
        content += f'    {struct["name"]}() {{\n'
        for field in struct['fields']:
            if not field['is_sequence']:
                cpp_type = type_map.get(field['type'], field['type'])
                default_map = {
                    'int8_t': '0',
                    'int16_t': '0',
                    'int32_t': '0',
                    'int64_t': '0',
                    'uint8_t': '0',
                    'uint16_t': '0',
                    'uint32_t': '0',
                    'uint64_t': '0',
                    'float': '0.0f',
                    'double': '0.0',
                    'bool': 'false'
                }
                default_val = default_map.get(cpp_type, '{}')
                content += f'        {field["name"]} = {default_val};\n'
        content += '    }\n\n'

        # Generate constructor with allocator
        content += f'    explicit {struct["name"]}(pangofly::BlockAllocator* allocator) {{\n'
        for field in struct['fields']:
            if not field['is_sequence']:
                cpp_type = type_map.get(field['type'], field['type'])
                default_map = {
                    'int8_t': '0',
                    'int16_t': '0',
                    'int32_t': '0',
                    'int64_t': '0',
                    'uint8_t': '0',
                    'uint16_t': '0',
                    'uint32_t': '0',
                    'uint64_t': '0',
                    'float': '0.0f',
                    'double': '0.0',
                    'bool': 'false'
                }
                default_val = default_map.get(cpp_type, '{}')
                content += f'        {field["name"]} = {default_val};\n'
            else:
                content += f'        {field["name"]}.set_block_allocator(allocator);\n'
        content += '    }\n\n'

        # Generate CalculateSize method
        content += '    size_t CalculateSize() const {\n'
        content += '        size_t total = sizeof(*this);\n'
        for field in struct['fields']:
            if field['is_sequence']:
                cpp_type = type_map.get(field['type'], field['type'])
                content += f'        total += {field["name"]}.size() * sizeof({cpp_type});\n'
        content += '        return total;\n'
        content += '    }\n'

        content += '};\n\n'

    content += '} // namespace ' + module_name + '\n'

    with open(output_path, 'w') as f:
        f.write(content)

    print(f"Generated: {output_path}")

def main():
    if len(sys.argv) < 3:
        print("Usage: idl_generator.py <input.idl> <output.h>")
        sys.exit(1)

    input_idl = sys.argv[1]
    output_header = sys.argv[2]

    with open(input_idl, 'r') as f:
        idl_content = f.read()

    module_name, structs = parse_idl(idl_content)
    generate_header(module_name, structs, output_header)

if __name__ == '__main__':
    main()