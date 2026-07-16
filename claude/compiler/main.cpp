#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ast.h"
#include "classify.h"
#include "codegen.h"
#include "constfold.h"
#include "desugar.h"
#include "diagnostic.h"
#include "grammar.h"
#include "lex.h"
#include "numeric.h"
#include "optimize.h"
#include "parse.h"
#include "resolve.h"
#include "token.h"
#include "widen.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <out.ll>] [-I <path>...] "
                     "[-MF <deps.d>|-M]\n";
        return 1;
    }

    if (std::string(argv[1]) == "--type-selftest") {
        return widen::typeSelfTest(std::cout) ? 0 : 1;
    }

    std::string in_path = argv[1];
    std::string out_path;
    std::vector<std::string> import_paths;
    std::string deps_path;      // -MF <file>: write make deps to this file
    bool deps_stdout = false;   // -M: write make deps to stdout
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            import_paths.push_back(argv[++i]);
        } else if (arg == "-MF" && i + 1 < argc) {
            deps_path = argv[++i];
        } else if (arg == "-M") {
            deps_stdout = true;
        }
    }

    diagnostic::Sink diag;
    token::List tokens;
    parse::Tree parse_tree;
    ast::Tree ast_tree;

    auto bail = [&]() {
        diagnostic::render(tokens, diag, std::cerr);
        return 1;
    };

    lex::run(in_path, import_paths, tokens, diag);
    if (diagnostic::hasErrors(diag)) return bail();

    // Dependency output (-M / -MF): emit a make rule naming the .ll target and
    // its sources — the root .sl plus every transitively-imported .slh. The
    // lexer has opened every file, so tokens.files IS the complete source list
    // (files[0] is the root; imports follow). Emitted here, before the rest of
    // the pipeline, so deps are written even when a later stage errors.
    if (deps_stdout || !deps_path.empty()) {
        std::string target = out_path;
        if (target.empty()) {   // no -o: mirror the default .ll name
            target = in_path;
            auto pos = target.rfind(".sl");
            if (pos != std::string::npos) target.replace(pos, 3, ".ll");
            else target += ".ll";
        }
        std::ostringstream dep;
        dep << target << ":";
        for (auto const& f : tokens.files) dep << " " << f.path;
        dep << "\n";
        if (deps_stdout) {
            std::cout << dep.str();
        } else {
            std::filesystem::path dp(deps_path);
            if (dp.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(dp.parent_path(), ec);
            }
            std::ofstream df(deps_path);
            if (!df)
                std::cerr << "slidsc: warning: cannot write deps file '"
                          << deps_path << "'\n";
            else
                df << dep.str();
        }
    }

    numeric::run(tokens, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    grammar::run(tokens, parse_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    resolve::run(parse_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    constfold::run(parse_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    classify::run(parse_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    desugar::run(parse_tree, ast_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();
    optimize::run(ast_tree, diag);
    if (diagnostic::hasErrors(diag)) return bail();

    std::ostringstream codegen_buf;
    codegen::run(ast_tree, codegen_buf, diag);

    if (diagnostic::hasErrors(diag)) return bail();

    if (out_path.empty()) {
        std::cout << codegen_buf.str();
    } else {
        std::ofstream out(out_path);
        out << codegen_buf.str();
    }

    return 0;
}
