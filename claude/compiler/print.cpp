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
bool classifyInt(widen::TypeRef t, IntDesc& d) {
    widen::TypeKind k;
    if (!widen::classify(t, k)) return false;
    if (k.cat == widen::Category::kFloat) return false;
    d.llty = "i" + std::to_string(k.bits);
    d.is_unsigned = (k.cat != widen::Category::kSignedInt);   // unsigned int or bool
    d.bits = k.bits;
    return true;
}

bool isFloatType(widen::TypeRef t) {
    widen::TypeKind k;
    return widen::classify(t, k) && k.cat == widen::Category::kFloat;
}

// Widen a already-emitted integer value of type `t` to i64 (sext/zext by sign),
// so a slice bound of any int width feeds sub / getelementptr uniformly.
std::string toI64(std::string const& v, widen::TypeRef t, std::ostream& out) {
    IntDesc d;
    if (!classifyInt(t, d) || d.bits >= 64) return v;
    std::string ext = newTmp("s64");
    out << "  " << ext << " = " << (d.is_unsigned ? "zext " : "sext ")
        << d.llty << " " << v << " to i64\n";
    return ext;
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
        widen::TypeRef sty = seg->inferred_type;
        assert(sty != widen::kNoType && "print: segment missing inferred_type");

        if (sty == widen::intern("bool")) {
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
            if (sty != widen::intern("float64")) {
                std::string ext = newTmp("fext");
                out << "  " << ext << " = fpext float " << v << " to double\n";
                v = ext;
            }
            fmt += "%g";
            args.push_back({"double", v});
            continue;
        }

        // A range slice `base[lo..hi]` (kIndexExpr marked ".."): print exactly
        // (hi - lo) chars starting at base+lo, length-bounded via printf `%.*s`
        // (a substring is NOT NUL-terminated). __print-only — classify guarantees a
        // char[] base and integer bounds.
        if (seg->kind == ast::Kind::kIndexExpr && seg->text == "..") {
            ast::Node const& base = *seg->children[0];
            ast::Node const& lo = *seg->children[1];
            ast::Node const& hi = *seg->children[2];
            std::string bv = codegen::emitExpr(base, syms, pool, out, diag,
                                               base.inferred_type);
            std::string lov = toI64(codegen::emitExpr(lo, syms, pool, out, diag,
                                                      lo.inferred_type),
                                    lo.inferred_type, out);
            std::string hiv = toI64(codegen::emitExpr(hi, syms, pool, out, diag,
                                                      hi.inferred_type),
                                    hi.inferred_type, out);
            std::string len = newTmp("slen");
            out << "  " << len << " = sub i64 " << hiv << ", " << lov << "\n";
            std::string len32 = newTmp("slen");
            out << "  " << len32 << " = trunc i64 " << len << " to i32\n";
            std::string start = newTmp("sptr");
            out << "  " << start << " = getelementptr i8, ptr " << bv
                << ", i64 " << lov << "\n";
            fmt += "%.*s";
            args.push_back({"i32", len32});
            args.push_back({"ptr", start});
            continue;
        }

        // A char string — a char iterator, element const or not: a bare `char[]`
        // and the `(const char)[]` a `char[]` parameter carries deep-strip to the
        // same handle. printf reads the pointer as a NUL-terminated %s.
        if (widen::deepStrip(sty) == widen::intern("char[]")) {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            fmt += "%s";
            args.push_back({"ptr", v});
            continue;
        }

        IntDesc d;
        if (classifyInt(sty, d)) {
            std::string v = codegen::emitExpr(*seg, syms, pool, out, diag, sty);
            if (sty == widen::intern("char")) {
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
            + widen::spell(sty) + "'.", {}});
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
