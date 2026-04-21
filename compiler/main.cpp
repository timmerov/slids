#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <set>

// --instantiate <dir>: read all .sli files, deduplicate, write __instantiations.sl
static int runInstantiate(const std::string& dir, const std::string& out_path) {
    // collect unique imports (preserving first-seen order) and instantiations
    struct Import { std::string module; bool is_template; };
    std::vector<Import> imports;
    std::set<std::string> import_set;
    std::vector<std::string> insts;
    std::set<std::string> inst_set;

    std::error_code ec;
    for (auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".sli") continue;
        std::ifstream f(entry.path());
        std::string line;
        enum class Section { None, Class, Template, Instantiations } section = Section::None;
        while (std::getline(f, line)) {
            auto s = line.find_first_not_of(" \t");
            if (s == std::string::npos) continue;
            line = line.substr(s);
            if (line == "/* class declarations. */")            { section = Section::Class; continue; }
            if (line == "/* template declarations. */")         { section = Section::Template; continue; }
            if (line == "/* explicit template instantiations. */") { section = Section::Instantiations; continue; }
            if (line.rfind("import ", 0) == 0) {
                auto semi = line.find(';');
                if (semi == std::string::npos) continue;
                std::string mod = line.substr(7, semi - 7);
                auto ms = mod.find_first_not_of(" \t");
                auto me = mod.find_last_not_of(" \t");
                if (ms == std::string::npos) continue;
                mod = mod.substr(ms, me - ms + 1);
                bool is_tmpl = (section == Section::Template);
                if (import_set.insert(mod).second)
                    imports.push_back({mod, is_tmpl});
            } else if (line.find('<') != std::string::npos) {
                // instantiation line: Name<Types>;
                auto semi = line.find(';');
                if (semi == std::string::npos) continue;
                std::string inst = line.substr(0, semi);
                auto ie = inst.find_last_not_of(" \t");
                if (ie == std::string::npos) continue;
                inst = inst.substr(0, ie + 1);
                if (inst_set.insert(inst).second)
                    insts.push_back(inst);
            }
        }
    }
    if (ec) {
        std::cerr << "slidsc: --instantiate: cannot read directory '" << dir << "'\n";
        return 1;
    }
    if (insts.empty()) {
        std::cout << "slidsc: --instantiate: no instantiations found\n";
        return 0;
    }

    // sort instantiations for deterministic output
    std::sort(insts.begin(), insts.end());

    std::filesystem::path op(out_path);
    std::filesystem::create_directories(op.parent_path(), ec);
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "slidsc: cannot write '" << out_path << "'\n";
        return 1;
    }
    out << "/* class declarations. */\n";
    for (auto& [mod, is_tmpl] : imports)
        if (!is_tmpl) out << "import " << mod << ";\n";
    out << "\n";
    out << "/* template declarations. */\n";
    for (auto& [mod, is_tmpl] : imports)
        if (is_tmpl) out << "import " << mod << ";\n";
    out << "\n";
    out << "/* explicit template instantiations. */\n";
    for (auto& inst : insts)
        out << inst << ";\n";

    std::cout << "slidsc: wrote " << out_path << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <output.ll>] [--import-path <dir>]... [-MF <deps.d>|-M]\n"
                  << "       slidsc --instantiate <build-dir> -o <output.sl>\n";
        return 1;
    }

    // --instantiate mode
    if (std::string(argv[1]) == "--instantiate") {
        if (argc < 3) { std::cerr << "slidsc: --instantiate requires a directory\n"; return 1; }
        std::string inst_dir = argv[2];
        std::string inst_out = inst_dir + "/__instantiations.sl";
        for (int i = 3; i < argc; i++) {
            if (std::string(argv[i]) == "-o" && i + 1 < argc)
                inst_out = argv[++i];
        }
        return runInstantiate(inst_dir, inst_out);
    }

    std::string input_path = argv[1];
    std::string output_path;
    std::vector<std::string> import_paths;
    std::string deps_path;   // -MF <file>
    bool deps_stdout = false; // -M

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc)
            output_path = argv[++i];
        else if (arg == "--import-path" && i + 1 < argc)
            import_paths.push_back(argv[++i]);
        else if (arg == "-MF" && i + 1 < argc)
            deps_path = argv[++i];
        else if (arg == "-M")
            deps_stdout = true;
    }

    if (output_path.empty()) {
        output_path = input_path;
        auto pos = output_path.rfind(".sl");
        if (pos != std::string::npos)
            output_path.replace(pos, 3, ".ll");
        else
            output_path += ".ll";
    }

    std::ifstream in(input_path);
    if (!in) {
        std::cerr << "slidsc: cannot open '" << input_path << "'\n";
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string source = buf.str();

    try {
        std::filesystem::path out_path(output_path);
        if (out_path.has_parent_path())
            std::filesystem::create_directories(out_path.parent_path());

        std::string source_dir;
        {
            std::filesystem::path p(input_path);
            if (p.has_parent_path())
                source_dir = p.parent_path().string();
        }

        Lexer lexer(source);
        auto tokens = lexer.tokenize();

        Parser parser(std::move(tokens), source_dir, import_paths);
        auto program = parser.parse();

        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "slidsc: cannot write '" << output_path << "'\n";
            return 1;
        }

        Codegen codegen(program, out);
        codegen.emit();

        std::cout << "slidsc: wrote " << output_path << "\n";
        std::error_code ec;
        std::filesystem::remove(output_path + ".err", ec);

        // TODO: the instantiator should not create a .sli file
        // write .sli file (adjacent to .ll) if there are imported template instantiations
        {
            std::string sli_path = output_path;
            auto dot = sli_path.rfind(".ll");
            if (dot != std::string::npos) sli_path.replace(dot, 3, ".sli");
            else sli_path += ".sli";
            std::ostringstream sli_buf;
            codegen.writeSliFile(sli_buf);
            std::string sli_content = sli_buf.str();
            if (!sli_content.empty()) {
                std::ofstream sli_out(sli_path);
                if (sli_out) {
                    sli_out << sli_content;
                    std::cout << "slidsc: wrote " << sli_path << "\n";
                }
            } else {
                std::filesystem::remove(sli_path, ec); // clean up stale .sli if no longer needed
            }
        }

        // emit dependency info (-MF file or -M to stdout)
        if (!deps_path.empty() || deps_stdout) {
            // build dep line: target: source.sl [header.slh ...]
            std::ostringstream dep;
            dep << output_path << ": " << input_path;
            for (auto& h : program.imported_headers)
                dep << " " << h;
            dep << "\n";

            if (deps_stdout) {
                std::cout << dep.str();
            } else {
                std::filesystem::path dp(deps_path);
                if (dp.has_parent_path())
                    std::filesystem::create_directories(dp.parent_path());
                std::ofstream df(deps_path);
                if (!df)
                    std::cerr << "slidsc: warning: cannot write deps file '" << deps_path << "'\n";
                else
                    df << dep.str();
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "slidsc: error: " << e.what() << "\n";
        // rename partial output so make doesn't treat it as up-to-date,
        // but preserve it for debugging
        std::string err_path = output_path + ".err";
        std::error_code ec;
        std::filesystem::rename(output_path, err_path, ec);
        if (!ec)
            std::cerr << "slidsc: partial output saved as " << err_path << "\n";
        return 1;
    }

    return 0;
}
