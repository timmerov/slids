#include "print.h"

#include <cassert>
#include <ostream>
#include <string>
#include <vector>

#include "ast.h"
#include "codegen.h"
#include "diagnostic.h"
#include "strings.h"

namespace print {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

void flattenPlusChain(ast::Node const& root,
                      std::vector<ast::Node const*>& out) {
    if (root.kind == ast::Kind::kBinaryExpr && root.text == "+"
        && root.children.size() == 2) {
        flattenPlusChain(*root.children[0], out);
        flattenPlusChain(*root.children[1], out);
        return;
    }
    out.push_back(&root);
}

std::string escapePct(std::string const& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) { r += c; if (c == '%') r += '%'; }
    return r;
}

std::string newTmp(char const* tag) {
    static int n = 0;
    return std::string("%") + tag + "_" + std::to_string(n++);
}

struct IntDesc { std::string llty; bool is_unsigned; int bits; };
bool classifyInt(std::string const& t, IntDesc& d) {
    if (t == "bool")                              { d = {"i1",  true, 1};  return true; }
    if (t == "char" || t == "uint8")              { d = {"i8",  true, 8};  return true; }
    if (t == "int8")                              { d = {"i8",  false,8};  return true; }
    if (t == "uint16")                            { d = {"i16", true, 16}; return true; }
    if (t == "int16")                             { d = {"i16", false,16}; return true; }
    if (t == "uint" || t == "uint32")             { d = {"i32", true, 32}; return true; }
    if (t == "int"  || t == "int32")              { d = {"i32", false,32}; return true; }
    if (t == "uint64")                            { d = {"i64", true, 64}; return true; }
    if (t == "int64" || t == "intptr")            { d = {"i64", false,64}; return true; }
    return false;
}

bool isFloatType(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
}

}  // namespace

bool tryEmitCall(ast::Node const& call, codegen::SymTab const& syms,
                 strings::Pool& pool, std::ostream& out,
                 diagnostic::Sink& diag) {
    if (!isPrintIntrinsic(call.name)) return false;
    bool newline = (call.name == "__println");

    if (call.children.size() != 1) return false;
    std::vector<ast::Node const*> segments;
    flattenPlusChain(*call.children[0], segments);

    std::string fmt;
    struct Arg { std::string type; std::string val; };
    std::vector<Arg> args;

    for (ast::Node const* seg : segments) {
        if (seg->kind == ast::Kind::kStringLiteral) {
            fmt += escapePct(seg->text);
            continue;
        }
        std::string const& sty = seg->inferred_type;
        assert(!sty.empty() && "print: segment missing inferred_type");

        if (sty == "bool") {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            int true_id  = strings::add(pool, "true");
            int false_id = strings::add(pool, "false");
            std::string sel = newTmp("bstr");
            out << "  " << sel << " = select i1 " << v
                << ", ptr @.str_" << true_id
                << ", ptr @.str_" << false_id << "\n";
            fmt += "%s";
            args.push_back({"ptr", sel});
            continue;
        }

        if (isFloatType(sty)) {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            if (sty != "float64") {
                std::string ext = newTmp("fext");
                out << "  " << ext << " = fpext float " << v << " to double\n";
                v = ext;
            }
            fmt += "%g";
            args.push_back({"double", v});
            continue;
        }

        if (sty == "char[]") {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            fmt += "%s";
            args.push_back({"ptr", v});
            continue;
        }

        IntDesc d;
        if (classifyInt(sty, d)) {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            if (sty == "char") {
                fmt += "%c";
                args.push_back({"i8", v});
                continue;
            }
            std::string llty = d.llty;
            if (d.bits < 32) {
                std::string ext = newTmp("iext");
                out << "  " << ext << " = " << (d.is_unsigned ? "zext " : "sext ")
                    << d.llty << " " << v << " to i32\n";
                v = ext;
                llty = "i32";
            }
            if (llty == "i64")        fmt += d.is_unsigned ? "%llu" : "%lld";
            else if (d.is_unsigned)   fmt += "%u";
            else                      fmt += "%d";
            args.push_back({llty, v});
            continue;
        }

        // Not yet implemented: pointer segments (Phase 4), tuple segments
        // (Phase 4), slid-typed segments with user-defined __str (Phase 5),
        // enum segments. Each lands with its phase.
        // user notified, accepts state.
        diagnostic::report(diag, {seg->file_id, seg->tok,
            "Print intrinsic does not yet support segments of type '"
            + sty + "'.", {}});
    }

    if (newline) fmt += "\n";

    int fmt_id = strings::add(pool, fmt);
    std::string call_line = "  call i32 (ptr, ...) @printf(ptr @.str_"
        + std::to_string(fmt_id);
    for (auto const& a : args) call_line += ", " + a.type + " " + a.val;
    call_line += ")";
    out << call_line << "\n";
    return true;
}

}  // namespace print
