#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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

namespace {

// The base module name of a path: "build/tmpl_lib.ll" -> "tmpl_lib".
std::string moduleOf(std::string const& path) {
    std::string base = std::filesystem::path(path).stem().string();
    return base;
}

// Parse ONE .sli file (the human-readable, slids-shaped demand format):
//   import vector;
//   import library;
//   import vector { Vector<int>; tsum<int>; }
// Plain imports accumulate per file; a braced block addressed to `target`
// contributes its spellings as demands and the file's plain imports as
// injection candidates (the provenance headers an argument type needs).
void readSliFile(std::string const& path, std::string const& target,
                 std::vector<std::string>& demands,
                 std::vector<std::string>& injects) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::stringstream buf;
    buf << f.rdbuf();
    std::string s = buf.str();
    std::vector<std::string> file_imports;
    std::vector<std::string> file_demands;
    bool addressed_here = false;
    std::size_t i = 0;
    auto skipWs = [&]() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; };
    auto word = [&]() {
        std::size_t b = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
        return s.substr(b, i - b);
    };
    while (true) {
        skipWs();
        if (i >= s.size()) break;
        std::string w = word();
        if (w != "import") break;   // malformed — stop quietly (driver-owned file)
        skipWs();
        std::string module = word();
        skipWs();
        if (i < s.size() && s[i] == ';') {
            i++;
            file_imports.push_back(module);
            continue;
        }
        if (i < s.size() && s[i] == '{') {
            i++;
            bool mine = (module == target);
            if (mine) addressed_here = true;
            while (true) {
                skipWs();
                if (i >= s.size() || s[i] == '}') { if (i < s.size()) i++; break; }
                std::size_t b = i;
                while (i < s.size() && s[i] != ';' && s[i] != '}') i++;
                std::string spelling = s.substr(b, i - b);
                while (!spelling.empty() && std::isspace((unsigned char)spelling.back()))
                    spelling.pop_back();
                if (i < s.size() && s[i] == ';') i++;
                if (mine && !spelling.empty()) file_demands.push_back(spelling);
            }
            continue;
        }
        break;   // malformed
    }
    if (!addressed_here) return;
    for (auto& m : file_imports) injects.push_back(m);
    for (auto& d : file_demands) demands.push_back(d);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: slidsc <source.sl> [-o <out.ll>] [-I <path>...] "
                     "[-MF <deps.d>|-M] [--instantiate <dir>]\n";
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
    std::string inst_dir;       // --instantiate <dir>: read the .sli demand pool
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
        } else if (arg == "--instantiate" && i + 1 < argc) {
            inst_dir = argv[++i];
        }
    }

    // --instantiate: this TU is a template source. Collect the demand pool's
    // entries addressed to it (blocks named with ITS module name), plus the
    // provenance headers those demand files imported — injected into the lex
    // so an argument type from another header is spellable here.
    std::vector<std::string> inst_demands;
    std::vector<std::string> extra_imports;
    if (!inst_dir.empty()) {
        std::string target = moduleOf(in_path);
        std::error_code ec;
        std::vector<std::string> sli_files;
        for (auto const& e :
             std::filesystem::directory_iterator(inst_dir, ec)) {
            if (e.path().extension() == ".sli")
                sli_files.push_back(e.path().string());
        }
        std::sort(sli_files.begin(), sli_files.end());   // deterministic order
        for (auto const& p : sli_files)
            readSliFile(p, target, inst_demands, extra_imports);
        // Dedup, preserving first-seen order.
        std::set<std::string> seen;
        std::vector<std::string> uniq;
        for (auto& m : extra_imports)
            if (m != target && seen.insert(m).second) uniq.push_back(m);
        extra_imports = std::move(uniq);
        seen.clear();
        uniq.clear();
        for (auto& d : inst_demands)
            if (seen.insert(d).second) uniq.push_back(d);
        inst_demands = std::move(uniq);
    }

    diagnostic::Sink diag;
    token::List tokens;
    parse::Tree parse_tree;
    ast::Tree ast_tree;

    auto bail = [&]() {
        diagnostic::render(tokens, diag, std::cerr);
        return 1;
    };

    lex::run(in_path, import_paths, tokens, diag, extra_imports);
    if (diagnostic::hasErrors(diag)) return bail();
    for (auto& d : inst_demands)
        parse_tree.inst_demands.push_back({d, false});

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

    // .sli dump — the demands THIS TU makes on other libraries' templates: for
    // each header-declared template this TU is not the sibling of, every
    // instantiated flavor, tagged with the template's header module plus the
    // provenance header of every class-typed argument (so the template
    // source's --instantiate compile can spell it). Written beside the .ll,
    // human-readable and slids-shaped:
    //   import vector;
    //   import library;
    //   import vector { Vector<int>; tsum<int>; }
    if (!out_path.empty()) {
        auto moduleOfFile = [&](int fid) -> std::string {
            if (fid < 0 || fid >= (int)tokens.files.size()) return "";
            return moduleOf(tokens.files[fid].path);
        };
        // target module -> (provenance imports, demand spellings)
        std::map<std::string,
                 std::pair<std::set<std::string>, std::vector<std::string>>>
            targets;
        for (auto& [tid, ti] : parse_tree.templates) {
            if (!ti.def || ti.instances.empty()) continue;
            parse::Entry const& te = parse_tree.entries[tid];
            if (te.kind == parse::EntryKind::kAlias) continue;  // type-level only
            int pf = ti.def->file_id;
            bool imported = pf >= 0 && pf < (int)parse_tree.file_imported.size()
                         && parse_tree.file_imported[pf];
            bool sibling = pf >= 0 && pf < (int)parse_tree.file_sibling.size()
                        && parse_tree.file_sibling[pf];
            if (!imported || sibling) continue;   // local, or ours to define
            std::string target = moduleOfFile(pf);
            if (target.empty()) continue;
            auto& slot = targets[target];
            for (auto& [args, iid] : ti.instances) {
                // An INLINE-local flavor (an argument type private to this TU)
                // was emitted here, internal — it is no demand, and its
                // argument has no spelling the template source could resolve.
                bool local = false;
                for (widen::TypeRef a : args) {
                    widen::TypeRef s = widen::strip(a);
                    while (widen::form(s) == widen::Type::Form::kPointer
                           || widen::form(s) == widen::Type::Form::kIterator)
                        s = widen::strip(widen::get(s).pointee);
                    if (widen::form(s) != widen::Type::Form::kSlid) continue;
                    auto ci = parse_tree.classes.find(s);
                    if (ci != parse_tree.classes.end()) {
                        int df = ci->second.def_file_id;
                        if (df < 0 || df >= (int)parse_tree.file_imported.size()
                            || !parse_tree.file_imported[df]) {
                            local = true;
                            break;
                        }
                    }
                }
                if (local) continue;
                std::string spelling;
                if (te.kind == parse::EntryKind::kClass) {
                    spelling = parse_tree.entries[iid].name;  // canonical "Vector<int>"
                } else {
                    std::string qual;
                    if (te.owner_ns_frame >= 0) {
                        for (auto& oe : parse_tree.entries) {
                            if (oe.ns_frame_id == te.owner_ns_frame
                                && (oe.kind == parse::EntryKind::kClass
                                    || oe.kind == parse::EntryKind::kNamespace)) {
                                qual = oe.name + ":";
                                break;
                            }
                        }
                    }
                    spelling = qual + te.name + "<";
                    for (std::size_t k = 0; k < args.size(); k++) {
                        if (k) spelling += ", ";
                        spelling += widen::spellOrEmpty(args[k]);
                    }
                    spelling += ">";
                }
                slot.second.push_back(spelling);
                for (widen::TypeRef a : args) {
                    widen::TypeRef s = widen::strip(a);
                    while (widen::form(s) == widen::Type::Form::kPointer
                           || widen::form(s) == widen::Type::Form::kIterator)
                        s = widen::strip(widen::get(s).pointee);
                    if (widen::form(s) != widen::Type::Form::kSlid) continue;
                    auto ci = parse_tree.classes.find(s);
                    if (ci == parse_tree.classes.end()) continue;
                    std::string am = moduleOfFile(ci->second.def_file_id);
                    if (!am.empty() && am != target) slot.first.insert(am);
                }
            }
        }
        if (!targets.empty()) {
            std::string sli_path = out_path;
            auto pos = sli_path.rfind(".ll");
            if (pos != std::string::npos) sli_path.replace(pos, 3, ".sli");
            else sli_path += ".sli";
            std::ofstream sf(sli_path);
            if (!sf) {
                std::cerr << "slidsc: warning: cannot write demand file '"
                          << sli_path << "'\n";
            } else {
                for (auto& [target, slot] : targets) {
                    sf << "import " << target << ";\n";
                    for (auto& m : slot.first) sf << "import " << m << ";\n";
                    sf << "import " << target << " {\n";
                    for (auto& sp : slot.second) sf << "    " << sp << ";\n";
                    sf << "}\n";
                }
            }
        }
    }

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
