#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ast.h"
#include "classify.h"
#include "codegen.h"
#include "desugar.h"
#include "diagnostic.h"
#include "grammar.h"
#include "layout.h"
#include "lex.h"
#include "optimize.h"
#include "parse.h"
#include "token.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: slidsc_v2 <source.sl> [-o <out.ll>]\n";
        return 1;
    }

    std::string in_path = argv[1];
    std::string out_path;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        }
    }

    std::ifstream f(in_path);
    std::stringstream buf;
    buf << f.rdbuf();
    std::string source = buf.str();

    diagnostic::Sink diag;
    token::List tokens;
    parse::Tree parse_tree;
    ast::Tree ast_tree;

    int file_id = 0;
    lex::run(file_id, source, tokens, diag);
    grammar::run(tokens, parse_tree, diag);
    classify::run(parse_tree, diag);
    desugar::run(parse_tree, ast_tree, diag);
    optimize::run(ast_tree, diag);
    layout::run(ast_tree, diag);

    if (out_path.empty()) {
        codegen::run(ast_tree, std::cout, diag);
    } else {
        std::ofstream out(out_path);
        codegen::run(ast_tree, out, diag);
    }

    return 0;
}
