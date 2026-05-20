#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>

#include "idl/lexer/lexer.h"
#include "idl/parser/parser.h"
#include "idl/ast/ast.h"
#include "idl/codegen/code_generator.h"

void print_usage(const char* program_name) {
    std::cout << "Pangofly IDL Compiler (IDC) v1.0.0\n";
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  " << program_name << " [options] <input_file>\n";
    std::cout << "  " << program_name << " -o <output_file> <input_file>\n";
    std::cout << "  " << program_name << " -I <source>        (compile from source string)\n";
    std::cout << "\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <file>    Output file (default: stdout)\n";
    std::cout << "  -I, --input <source>   Compile from source string\n";
    std::cout << "  -h, --help             Show this help message\n";
    std::cout << "  -v, --version          Show version\n";
    std::cout << "\n";
}

void print_version() {
    std::cout << "Pangofly IDL Compiler (IDC) v1.0.0\n";
    std::cout << "Built for Pangofly Non-POD Data Support\n";
}

int compile_file(const std::string& input_path, const std::string& output_path) {
    std::ifstream file(input_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_path << std::endl;
        return 1;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();
    file.close();
    
    pangofly::idl::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    if (!lexer.get_errors().empty()) {
        std::cerr << "Lexer errors:" << std::endl;
        for (const auto& err : lexer.get_errors()) {
            std::cerr << "  " << err << std::endl;
        }
        return 1;
    }
    
    pangofly::idl::Parser parser(tokens);
    auto document = parser.parse();
    
    if (!parser.get_errors().empty()) {
        std::cerr << "Parser errors:" << std::endl;
        for (const auto& err : parser.get_errors()) {
            std::cerr << "  " << err << std::endl;
        }
        return 1;
    }
    
    pangofly::idl::CodeGenerator generator(document);
    std::string output = generator.generate();
    
    if (output_path.empty() || output_path == "-") {
        std::cout << output;
    } else {
        std::ofstream out_file(output_path);
        if (!out_file.is_open()) {
            std::cerr << "Error: Cannot open output file: " << output_path << std::endl;
            return 1;
        }
        out_file << output;
        out_file.close();
        std::cout << "Successfully generated: " << output_path << std::endl;
    }
    
    return 0;
}

int compile_source(const std::string& source, const std::string& output_path) {
    pangofly::idl::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    if (!lexer.get_errors().empty()) {
        std::cerr << "Lexer errors:" << std::endl;
        for (const auto& err : lexer.get_errors()) {
            std::cerr << "  " << err << std::endl;
        }
        return 1;
    }
    
    pangofly::idl::Parser parser(tokens);
    auto document = parser.parse();
    
    if (!parser.get_errors().empty()) {
        std::cerr << "Parser errors:" << std::endl;
        for (const auto& err : parser.get_errors()) {
            std::cerr << "  " << err << std::endl;
        }
        return 1;
    }
    
    pangofly::idl::CodeGenerator generator(document);
    std::string output = generator.generate();
    
    if (output_path.empty() || output_path == "-") {
        std::cout << output;
    } else {
        std::ofstream out_file(output_path);
        if (!out_file.is_open()) {
            std::cerr << "Error: Cannot open output file: " << output_path << std::endl;
            return 1;
        }
        out_file << output;
        out_file.close();
        std::cout << "Successfully generated: " << output_path << std::endl;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string input_file;
    std::string output_file;
    std::string source;
    bool compile_from_source = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            } else {
                std::cerr << "Error: Missing argument for " << arg << std::endl;
                return 1;
            }
        } else if (arg == "-I" || arg == "--input") {
            if (i + 1 < argc) {
                compile_from_source = true;
                source = argv[++i];
            } else {
                std::cerr << "Error: Missing argument for " << arg << std::endl;
                return 1;
            }
        } else if (arg[0] != '-') {
            input_file = arg;
        } else {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            return 1;
        }
    }
    
    if (input_file.empty() && source.empty()) {
        std::cerr << "Error: No input file specified" << std::endl;
        return 1;
    }
    
    if (compile_from_source) {
        return compile_source(source, output_file);
    } else {
        return compile_file(input_file, output_file);
    }
}
