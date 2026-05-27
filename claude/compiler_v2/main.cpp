#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast.h"
#include "classify.h"
#include "codegen.h"
#include "desugar.h"
#include "diagnostic.h"
#include "grammar.h"
#include "layout.h"
#include "lex.h"
#include "numeric.h"
#include "optimize.h"
#include "parse.h"
#include "token.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <out.ll>] [-I <path>...]\n";
        return 1;
    }

    std::string in_path = argv[1];
    std::string out_path;
    std::vector<std::string> import_paths;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            import_paths.push_back(argv[++i]);
        }
    }

    diagnostic::Sink diag;
    token::List tokens;
    parse::Tree parse_tree;
    ast::Tree ast_tree;

    lex::run(in_path, import_paths, tokens, diag);
    numeric::run(tokens, diag);
    grammar::run(tokens, parse_tree, diag);
    classify::run(parse_tree, diag);
    desugar::run(parse_tree, ast_tree, diag);
    optimize::run(ast_tree, diag);
    layout::run(ast_tree, diag);

    if (diagnostic::hasErrors(diag)) {
        diagnostic::render(tokens, diag, std::cerr);
        return 1;
    }

    std::ostringstream codegen_buf;
    codegen::run(ast_tree, codegen_buf, diag);

    if (diagnostic::hasErrors(diag)) {
        diagnostic::render(tokens, diag, std::cerr);
        return 1;
    }

    if (out_path.empty()) {
        std::cout << codegen_buf.str();
    } else {
        std::ofstream out(out_path);
        out << codegen_buf.str();
    }

    return 0;
}
