#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <output.ll>]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;

    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc)
            output_path = argv[++i];
    }

    if (output_path.empty()) {
        // replace .sl with .ll
        output_path = input_path;
        auto pos = output_path.rfind(".sl");
        if (pos != std::string::npos)
            output_path.replace(pos, 3, ".ll");
        else
            output_path += ".ll";
    }

    // read source
    std::ifstream in(input_path);
    if (!in) {
        std::cerr << "slidsc: cannot open '" << input_path << "'\n";
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string source = buf.str();

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();

        Parser parser(std::move(tokens));
        auto program = parser.parse();

        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "slidsc: cannot write '" << output_path << "'\n";
            return 1;
        }

        Codegen codegen(program, out);
        codegen.emit();

        std::cout << "slidsc: wrote " << output_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "slidsc: error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
