#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

// Compute byte size of a struct's fields using LLVM natural-alignment rules.
static int64_t computeStructSize(const std::vector<FieldDef>& fields) {
    int64_t offset = 0, max_align = 1;
    for (auto& f : fields) {
        int64_t sz, al;
        const std::string& t = f.type;
        if (t == "bool" || t == "int8" || t == "uint8" || t == "char") { sz = 1; al = 1; }
        else if (t == "int16" || t == "uint16")                         { sz = 2; al = 2; }
        else if (t == "int" || t == "int32" || t == "uint" || t == "uint32" || t == "float32") { sz = 4; al = 4; }
        else                                                             { sz = 8; al = 8; }
        if (al > max_align) max_align = al;
        offset = (offset + al - 1) & ~(al - 1);
        offset += sz;
    }
    return (offset + max_align - 1) & ~(max_align - 1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <output.ll>] [--import-path <dir>]... [--export-path <dir>]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    std::vector<std::string> import_paths;
    std::string export_path;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc)
            output_path = argv[++i];
        else if (arg == "--import-path" && i + 1 < argc)
            import_paths.push_back(argv[++i]);
        else if (arg == "--export-path" && i + 1 < argc)
            export_path = argv[++i];
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

        Parser parser(std::move(tokens), source_dir, import_paths, export_path);
        auto program = parser.parse();

        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "slidsc: cannot write '" << output_path << "'\n";
            return 1;
        }

        Codegen codegen(program, out);
        codegen.emit();

        std::cout << "slidsc: wrote " << output_path << "\n";

        // emit annotated .slh for each transported module
        for (auto& ti : program.transports) {
            if (export_path.empty()) {
                std::cerr << "slidsc: warning: 'transport " << ti.module_name
                          << "' used but no --export-path specified; annotated .slh not written\n";
                continue;
            }

            // find the completed SlidDef (merged; has no ellipsis suffix, has method bodies)
            // use slid_names from the transport info to look them up
            int64_t struct_size = 0;
            for (auto& sname : ti.slid_names) {
                for (auto& slid : program.slids) {
                    if (slid.name == sname && !slid.has_ellipsis_suffix) {
                        struct_size = computeStructSize(slid.fields);
                        break;
                    }
                }
                if (struct_size) break;
            }

            // read the source .slh text
            std::ifstream slh_in(ti.source_slh_path);
            if (!slh_in) {
                std::cerr << "slidsc: warning: cannot re-read '" << ti.source_slh_path << "'\n";
                continue;
            }
            std::string slh_text((std::istreambuf_iterator<char>(slh_in)),
                                   std::istreambuf_iterator<char>());

            // replace "sizeof = delete;" line with "sizeof = N;" (preserving indentation)
            std::string size_str = std::to_string(struct_size);
            bool replaced = false;
            {
                std::ostringstream out_slh;
                std::istringstream lines(slh_text);
                std::string line;
                while (std::getline(lines, line)) {
                    std::string stripped = line;
                    stripped.erase(0, stripped.find_first_not_of(" \t"));
                    if (stripped == "sizeof = delete;") {
                        std::string indent = line.substr(0, line.find_first_not_of(" \t"));
                        out_slh << indent << "sizeof = " << size_str << ";\n";
                        replaced = true;
                    } else {
                        out_slh << line << "\n";
                    }
                }
                slh_text = out_slh.str();
            }

            // if no placeholder was found, insert before the last }
            if (!replaced) {
                auto last_brace = slh_text.rfind('}');
                if (last_brace != std::string::npos)
                    slh_text.insert(last_brace, "    sizeof = " + size_str + ";\n");
            }

            std::filesystem::create_directories(export_path);
            std::string out_slh_path = export_path + "/" + ti.module_name + ".slh";
            std::ofstream slh_out(out_slh_path);
            if (!slh_out) {
                std::cerr << "slidsc: cannot write '" << out_slh_path << "'\n";
                continue;
            }
            slh_out << slh_text;
            std::cout << "slidsc: wrote " << out_slh_path << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "slidsc: error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
