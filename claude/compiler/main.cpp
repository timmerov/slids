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
                     "[-MF <deps.d>|-M] [--instantiate <dir>] "
                     "[--scan-deps <frag.mk>]\n";
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
    std::string scan_path;      // --scan-deps <file>: lex-only import scan; write
                                //   the make fragment wiring the consumer's
                                //   template-library instantiation stage
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
        } else if (arg == "--scan-deps" && i + 1 < argc) {
            scan_path = argv[++i];
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

    lex::run(in_path, import_paths, tokens, diag, extra_imports,
             /*load_all_sources=*/!scan_path.empty());
    if (diagnostic::hasErrors(diag)) {
        if (scan_path.empty()) return bail();
        // The import scan only feeds make: the compile that follows reports the
        // errors in full, so the scan names each erroring file once and stops
        // (a closure shared by several TUs would otherwise repeat every error).
        std::set<std::string> named;
        for (auto const& r : diag.records) {
            std::string path = (r.file_id >= 0 && r.file_id < (int)tokens.files.size())
                ? tokens.files[r.file_id].path : in_path;
            if (named.insert(path).second)
                std::cerr << "slidsc: scan-deps: " << path << " has errors.\n";
        }
        return 1;
    }
    // A METHOD demand is a flavor spelling + `:name` after the flavor's closing
    // `>` (`slids:Vector<int>:compare`, `slids:Vector<int>:op>=`): the flavor
    // and the lazy method to build. The `>` is found by bracket depth — an
    // operator name may carry its own.
    for (auto& d : inst_demands) {
        std::string spelling = d, method;
        std::size_t lt = d.find('<');
        if (lt != std::string::npos) {
            int depth = 0;
            for (std::size_t i = lt; i < d.size(); i++) {
                if (d[i] == '<') depth++;
                else if (d[i] == '>' && --depth == 0) {
                    if (i + 1 < d.size() && d[i + 1] == ':') {
                        spelling = d.substr(0, i + 1);
                        method = d.substr(i + 2);
                    }
                    break;
                }
            }
        }
        parse_tree.inst_demands.push_back({spelling, false, {}, method});
    }

    // --scan-deps: the IMPORT SCAN — lex only, no compile. The lexer chased
    // the whole import closure and loaded each template source sitting beside
    // an imported header (token::File.template_source), so the file table
    // already names every template library this consumer pulls in, and the
    // token stream carries each file's `import x;` statements — the
    // library-to-library ordering edges. Emit the make fragment that wires
    // the instantiation stage:
    //   <bld>/X.ll ...: <bld>/<consumer>.ll     (demand pool first)
    //   <bld>/Y.ll: <bld>/X.ll                  (X's files import Y)
    //   <consumer>_TMPL_OBJS = <bld>/X.o ...    (the link line's extras)
    //   <fragment>: <every scanned file>        (self-maintaining)
    // The build directory is the fragment's own directory — the same place
    // the .ll/.o/.sli files land. Exits before the pipeline: dependency
    // generation must not require the program to compile.
    if (!scan_path.empty()) {
        // The consumer's BUILD name is the fragment's (`<name>.deps.mk`), not the
        // source's stem: a consumer may share a stem with a library it imports
        // (test/lib/vector.sl), so the Makefile names its outputs apart.
        std::string consumer = std::filesystem::path(scan_path).filename().string();
        {
            auto pos = consumer.rfind(".deps.mk");
            if (pos != std::string::npos) consumer.erase(pos);
            else consumer = std::filesystem::path(scan_path).stem().string();
        }
        std::vector<std::string> libs;          // template-library modules
        std::set<std::string> lib_set;
        // A loaded template source is never the root (the lexer skips that by path).
        for (auto const& f : tokens.files) {
            std::string m = moduleOf(f.path);
            if (f.template_source && lib_set.insert(m).second)
                libs.push_back(m);
        }
        // module -> modules its files import (header and source both count;
        // the lexer consumed the statements but recorded every edge).
        std::map<std::string, std::set<std::string>> mod_imports;
        for (auto const& [fid, module] : tokens.import_edges) {
            if (fid < 0 || fid >= (int)tokens.files.size()) continue;
            mod_imports[moduleOf(tokens.files[fid].path)].insert(module);
        }
        std::string bld =
            std::filesystem::path(scan_path).parent_path().string();
        if (bld.empty()) bld = ".";
        auto ll = [&](std::string const& m) { return bld + "/" + m + ".ll"; };
        std::ostringstream mk;
        mk << "# generated by slidsc --scan-deps; do not edit.\n";
        if (!libs.empty()) {
            // The demand pool first: every template library waits for the
            // consumer's compile (which dumps the .sli beside its .ll).
            for (auto const& m : libs) mk << ll(m) << " ";
            mk << ": " << ll(consumer) << "\n";
            // Library-to-library ordering: X's files importing Y means X's
            // compile dumps demands FOR Y — Y waits for X.
            for (auto const& x : libs) {
                for (auto const& y : mod_imports[x]) {
                    if (y != x && lib_set.count(y))
                        mk << ll(y) << ": " << ll(x) << "\n";
                }
            }
            // The template-library compile flags, as a TARGET-SPECIFIC
            // variable the directory's generic .ll rule splices in: read the
            // demand pool, and put the consumer's own directory on the import
            // path (a pool's provenance imports may name its headers).
            for (auto const& m : libs) mk << ll(m) << " ";
            mk << ": SLIDS_INST = -I . --instantiate " << bld << "\n";
        }
        mk << consumer << "_TMPL_OBJS =";
        for (auto const& m : libs) mk << " " << bld << "/" << m << ".o";
        mk << "\n" << scan_path << ":";
        for (auto const& f : tokens.files) mk << " " << f.path;
        mk << "\n";
        std::filesystem::path sp(scan_path);
        if (sp.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(sp.parent_path(), ec);
        }
        std::ofstream sf(scan_path);
        if (!sf) {
            std::cerr << "slidsc: error: cannot write scan-deps file '"
                      << scan_path << "'\n";
            return 1;
        }
        sf << mk.str();
        return 0;
    }

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
    // AN UNNEEDED `import+` is an error (there are no warnings): every instance is
    // minted by now, so a root `import+` none of whose loaded sources an instance over
    // a locally defined private class drew on asks for a source nobody uses. Only the
    // ROOT's own statements are judged — a transitive load is not the author's line.
    for (auto const& st : parse_tree.import_stmts) {
        if (!st.plus || st.file_id != 0) continue;
        bool needed = false;
        for (int h : st.full_headers)
            if (h >= 0 && h < (int)parse_tree.header_source_used.size()
                && parse_tree.header_source_used[h]) needed = true;
        if (!needed)
            diagnostic::report(diag, {st.file_id, st.tok,
                "import+ " + st.module + " is not needed: no template declared in "
                + st.module + ".slh uses a locally defined private class.", {}});
    }
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
        // THE QUALIFIED SPELLING of a demand. The template source's --instantiate
        // TU has none of this TU's `alias Ns;` imports in force, so the template's
        // own name and every class in its arguments carry their full namespace path
        // (`slids:Map<slids:String, int>`). A path is the owner chain of namespace /
        // class names; a class that has a stable symbol path (Type::symbol_path)
        // spells it with `:`. A class-template INSTANCE argument spells as its
        // template (qualified) over its own arguments, recursively.
        auto colons = [](std::string p) {
            for (char& c : p) if (c == '.') c = ':';
            return p;
        };
        auto qualPrefix = [&](int frame) -> std::string {
            std::string out;
            int guard = (int)parse_tree.entries.size() + 2;
            while (frame >= 0 && guard-- > 0) {
                int found = -1;
                for (std::size_t i = 0; i < parse_tree.entries.size(); i++) {
                    parse::Entry const& q = parse_tree.entries[i];
                    if (q.ns_frame_id == frame
                        && (q.kind == parse::EntryKind::kClass
                            || q.kind == parse::EntryKind::kNamespace)) {
                        found = (int)i;
                        break;
                    }
                }
                if (found < 0) break;
                parse::Entry const& q = parse_tree.entries[found];
                if (q.kind == parse::EntryKind::kClass) {
                    widen::Type const& ct = widen::get(widen::strip(q.slids_type));
                    if (!ct.symbol_path.empty()) return colons(ct.symbol_path) + ":" + out;
                }
                out = q.name + ":" + out;
                frame = q.owner_ns_frame;
            }
            return out;
        };
        std::map<int, std::pair<int, std::vector<widen::TypeRef>>> class_inst_of;
        for (auto& [tid, ti] : parse_tree.templates) {
            if (!ti.def || ti.def->kind != parse::Kind::kClassDef) continue;
            for (auto& [args, iid] : ti.instances) class_inst_of[iid] = {tid, args};
        }
        auto spellDemand = [&](int tid, std::vector<widen::TypeRef> const& args) {
            parse::Entry const& te = parse_tree.entries[tid];
            std::string s = qualPrefix(te.owner_ns_frame) + te.name + "<";
            for (std::size_t k = 0; k < args.size(); k++) {
                if (k) s += ", ";
                s += widen::spellOrEmpty(args[k]);
            }
            return s + ">";
        };
        widen::setSlidSpeller([&](widen::TypeRef s) -> std::string {
            int cid = parse::classEntryForType(parse_tree, s);
            if (cid < 0) return "";
            auto it = class_inst_of.find(cid);
            if (it != class_inst_of.end())
                return spellDemand(it->second.first, it->second.second);
            widen::Type const& t = widen::get(s);
            return t.symbol_path.empty() ? std::string() : colons(t.symbol_path);
        });
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
                // The instance entry's flag is authoritative (it also covers a
                // NESTED local — `quicksort<Vector<Container>>`, where the
                // local class hides inside an imported template's flavor); the
                // scan below stays as the class-flavor arm's test.
                if (iid >= 0 && iid < (int)parse_tree.entries.size()
                    && parse_tree.entries[iid].tu_local_instance) continue;
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
                slot.second.push_back(spellDemand(tid, args));
                // LAZY TEMPLATE METHODS: each method this TU calls on the
                // aggregated flavor — the source builds only what is demanded.
                auto dm = parse_tree.demanded_methods.find(iid);
                if (te.kind == parse::EntryKind::kClass
                    && dm != parse_tree.demanded_methods.end())
                    for (std::string const& mn : dm->second)
                        slot.second.push_back(spellDemand(tid, args) + ":" + mn);
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
        widen::setSlidSpeller(nullptr);   // spell() is bare again everywhere else
        std::string sli_path = out_path;
        {
            auto pos = sli_path.rfind(".ll");
            if (pos != std::string::npos) sli_path.replace(pos, 3, ".sli");
            else sli_path += ".sli";
        }
        if (targets.empty()) {
            // NO demands: REMOVE a leftover .sli. The pool is every .sli in the
            // build dir, so a stale one from an earlier compile of this TU would
            // keep demanding flavors this TU no longer names — the importer
            // comments out its one `dump(...)` call, recompiles clean, and the
            // library's --instantiate stage still fails on the old demand.
            std::error_code ec;
            std::filesystem::remove(sli_path, ec);
        } else {
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
