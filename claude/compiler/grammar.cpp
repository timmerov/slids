#include "grammar.h"

#include <cassert>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#include "diagnostic.h"
#include "parse.h"
#include "token.h"

namespace grammar {

namespace {

struct PrimitiveSpelling {
    token::Kind kind;
    char const* name;
};

constexpr PrimitiveSpelling kPrimitives[] = {
    {token::Kind::kBool,    "bool"},
    {token::Kind::kChar,    "char"},
    {token::Kind::kInt,     "int"},
    {token::Kind::kInt8,    "int8"},
    {token::Kind::kInt16,   "int16"},
    {token::Kind::kInt32,   "int32"},
    {token::Kind::kInt64,   "int64"},
    {token::Kind::kUint,    "uint"},
    {token::Kind::kUint8,   "uint8"},
    {token::Kind::kUint16,  "uint16"},
    {token::Kind::kUint32,  "uint32"},
    {token::Kind::kUint64,  "uint64"},
    {token::Kind::kIntptr,  "intptr"},
    {token::Kind::kFloat,   "float"},
    {token::Kind::kFloat32, "float32"},
    {token::Kind::kFloat64, "float64"},
    {token::Kind::kVoid,    "void"},
};

char const* primitiveNameFor(token::Kind k) {
    for (auto const& m : kPrimitives) {
        if (m.kind == k) return m.name;
    }
    return nullptr;
}

struct Parser {
    token::List const& tokens;
    parse::Tree& out;
    diagnostic::Sink& diag;
    int pos = 0;
    bool fatal = false;
    bool case_label_ = false;        // parsing a switch case-label const-expr: a
                                     // qualified name's trailing `:` is the label
                                     // terminator, not a qualifier separator
    std::string current_func = {};   // enclosing function name, for ##func
    std::string clock_date = {};     // ##date / ##time text, captured once per
    std::string clock_time = {};     // compile (this .sl's compile, not slidsc's)
    bool clock_captured = false;

    token::Token const& peek() const { return tokens.tokens[pos]; }

    token::Kind peekKind(int ahead) const {
        int p = pos + ahead;
        if (p >= static_cast<int>(tokens.tokens.size())) return token::Kind::kEndOfInput;
        return tokens.tokens[p].kind;
    }

    void advance() {
        if (pos + 1 < static_cast<int>(tokens.tokens.size())) pos++;
    }

    void error(std::string const& msg) {
        if (fatal) return;
        fatal = true;
        token::Token const& t = peek();
        diagnostic::report(diag, {t.file_id, pos, msg, {}});
    }

    // Report at an explicit token (caret precision when the offender is not the
    // current position — e.g. an already-consumed macro name).
    void errorAt(int tok, std::string const& msg) {
        if (fatal) return;
        fatal = true;
        diagnostic::report(diag, {tokens.tokens[tok].file_id, tok, msg, {}});
    }

    // ##date / ##time expand to the moment THIS .sl is compiled, captured once so
    // every macro in the file agrees. Format matches v1: "Mmm DD YYYY" (space-
    // padded day) and "HH:MM:SS".
    void captureClock() {
        if (clock_captured) return;
        clock_captured = true;
        std::time_t now = std::time(nullptr);
        std::tm tm_buf{};
        localtime_r(&now, &tm_buf);
        char date_buf[32] = {0};
        char time_buf[32] = {0};
        std::strftime(date_buf, sizeof(date_buf), "%b %e %Y", &tm_buf);
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_buf);
        clock_date = date_buf;
        clock_time = time_buf;
    }

    // Filename without its directory — ##file is the short name (no path).
    static std::string baseName(std::string const& path) {
        auto slash = path.find_last_of('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    bool expect(token::Kind kind, char const* name) {
        if (peek().kind != kind) {
            error(std::string("Expected '") + name + "'.");
            return false;
        }
        advance();
        return true;
    }

    bool isTypeStart(token::Kind k) const {
        return primitiveNameFor(k) != nullptr;
    }

    // Build a parse node stamped with explicit (file_id, tok).
    std::unique_ptr<parse::Node> newNodeAt(parse::Kind kind, int file_id, int tok) {
        auto n = std::make_unique<parse::Node>();
        n->kind = kind;
        n->file_id = file_id;
        n->tok = tok;
        return n;
    }

    // Build a parse node stamped at the current position.
    std::unique_ptr<parse::Node> newNodeHere(parse::Kind kind) {
        return newNodeAt(kind, peek().file_id, pos);
    }

    // Parse the contents of ONE sized-array bracket group — the `[` already consumed,
    // the matching `]` consumed here. A group is one or more comma-separated dims
    // (`[3]`, `[3,5]`). Fills `spell` with each dim's spelling (the literal text, or a
    // provisional "1" for a const-EXPRESSION dim) and `exprs` 1:1 (nullptr for a
    // literal, the parsed expr for a const-expr dim). `allow_expr` gates non-literal
    // dims: a name-dim / dim_sink context allows them; a no-sink type context rejects
    // them. Dims are returned in SOURCE order — the caller applies the comma transpose
    // (`[a,b]` -> `[b][a]`) when appending. The single dim-group parser shared by the
    // core-type suffix chain (parseType) and the name-anchored dims (parseNameDims).
    // Returns false (after error()) on a non-positive / malformed literal dim or a
    // rejected non-literal.
    bool parseBracketGroup(std::vector<std::string>& spell,
                           std::vector<std::unique_ptr<parse::Node>>& exprs,
                           bool allow_expr) {
        while (true) {
            if (peek().kind == token::Kind::kIntLiteral) {
                if (std::strtoll(peek().text.c_str(), nullptr, 10) <= 0) {
                    error("Array size must be a positive integer constant.");
                    return false;
                }
                spell.push_back(peek().text);
                advance();   // size
                exprs.push_back(nullptr);
            } else if (allow_expr) {
                auto dim = parseExpr();
                if (!dim) return false;
                spell.push_back("1");   // provisional; constfold bakes it
                exprs.push_back(std::move(dim));
            } else {
                error("An array type dimension must be an integer literal.");
                return false;
            }
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
        }
        return expect(token::Kind::kRBracket, "]");
    }

    // `seg_toks` (optional out): for a qualified identifier type, the token index
    // of each `:`-separated segment, so resolve can caret the offending segment
    // (a flat spelling string otherwise loses them). Left empty for a primitive.
    // parseType is the internal core-type producer; every type site reaches it through
    // parseDeclarator (which owns the per-site policy), so these flags are no longer a
    // proxy for "is there a name" — NamePolicy is.
    // top_dim: how to treat a TOP-LEVEL sized array dim (`int[3]`, `int^[3]` — a sized dim
    // NOT nested inside a pointer wrapper `int[3]^` / hidden behind an alias `Vec3`).
    //   Consume         — bake it into the type (an anonymous Forbidden / ListSlot site).
    //   RejectToName    — error: the size belongs on the NAME `int x[3]` (a named Required
    //                     / BindSlot declarator).
    //   StopBeforeSized — STOP the wrapper chain before it, leaving the `[` for the caller.
    //                     Used by `new T[n]`, where the trailing sized dim is the RUNTIME
    //                     alloc count, not a type dim. (An `[]` iterator + `^` wrappers are
    //                     still consumed, so `new int^[n]` = n pointers.)
    // dim_sink (out): collects const-EXPRESSION array dims so constfold can fold + bake
    // them. A non-literal dim (`int[N]`) is parsed as an expression, spelled as a
    // provisional `[1]`, and pushed (a literal dim pushes a nullptr) — the sink aligns
    // 1:1 with every type-position array dim, in tree pre-order. parseDeclarator (the
    // sole entry) always supplies it; a null sink (defensive) rejects a non-literal dim.
    enum class TopDim { Consume, RejectToName, StopBeforeSized };
    std::string parseType(std::vector<int>* seg_toks = nullptr,
                          TopDim top_dim = TopDim::Consume,
                          std::vector<std::unique_ptr<parse::Node>>* dim_sink = nullptr) {
        // `mutable` is a PARAMETER qualifier only — parseParamList consumes it before
        // the type, so reaching it here means a non-parameter position (return type,
        // var decl, tuple slot, cast target, ...).
        if (peek().kind == token::Kind::kMutable) {
            error("The 'mutable' qualifier may only appear on a function parameter.");
            return "";
        }
        // A leading `const` qualifies the WHOLE following type (binds loosest), so
        // `const T^` is a const pointer to const data (deep). To const-qualify only a
        // pointee, group it: `(const T)^`. The spelling round-trips: intern() peels a
        // leading `const ` first, and a `(const T)` group is preserved below so a
        // suffix lands OUTSIDE the parens.
        if (peek().kind == token::Kind::kConst) {
            advance();   // const
            std::string rest = parseType(seg_toks, top_dim, dim_sink);
            if (rest.empty()) return "";
            return "const " + rest;
        }
        std::string type;
        if (peek().kind == token::Kind::kLParen) {
            // Anonymous tuple type `(T0, T1, ...)`. A size-1 `(T)` collapses to T
            // (the comma is the tuple marker). Elements recurse, so nested tuples
            // and qualified/aliased element types work.
            advance();   // (
            if (peek().kind == token::Kind::kRParen) {
                error("A tuple type needs at least one element.");
                return "";
            }
            std::vector<std::string> elems;
            while (true) {
                // Each tuple-TYPE slot is a ListSlot declarator: it must be a type and
                // anonymous. A named slot is "too many names" — the name belongs on the
                // variable, not inside the type (at ANY nesting depth).
                Declarator slot;
                if (!parseDeclarator(NamePolicy::ListSlot, /*parse_name_dims=*/false,
                                     /*allow_qualified=*/false, nullptr, slot)) {
                    return "";
                }
                if (!slot.name.empty()) {
                    errorAt(slot.name_tok,
                            "A tuple-type slot cannot be named; move the name to the "
                            "variable (write '(T, T) name', not '(T x, T y) name').");
                    return "";
                }
                elems.push_back(slot.type);
                // Propagate the slot's const-EXPRESSION dims to the outer sink in source
                // order (bakeDimsWalk visits tuple slots left-to-right) — preserving the
                // pre-order contract. With no outer sink, a non-literal slot dim is
                // rejected, exactly as a no-sink type position rejects one.
                if (dim_sink) {
                    for (auto& sd : slot.dim_exprs) dim_sink->push_back(std::move(sd));
                } else if (slot.any_dim_expr) {
                    error("An array type dimension must be an integer literal.");
                    return "";
                }
                if (peek().kind != token::Kind::kComma) break;
                advance();   // ,
            }
            if (!expect(token::Kind::kRParen, ")")) return "";
            if (elems.size() == 1) {
                // A 1-tuple `(T)` collapses to grouping. PRESERVE the parens when the
                // sole element is const-qualified, so a following `^`/`[]` suffix binds
                // OUTSIDE the const — `(const T)^` (shallow), not `const T^` (deep).
                type = (elems[0].compare(0, 6, "const ") == 0)
                       ? "(" + elems[0] + ")" : elems[0];
            } else {
                type = "(";
                for (std::size_t i = 0; i < elems.size(); i++) {
                    if (i) type += ", ";
                    type += elems[i];
                }
                type += ")";
            }
        } else if (char const* name = primitiveNameFor(peek().kind)) {
            type = name;
            advance();
        } else if (peek().kind == token::Kind::kIdentifier
                   || peek().kind == token::Kind::kColonColon) {
            // An identifier type name (alias / class / enum), possibly qualified
            // (`Space:Dir` — a type that is a namespace member). Joined with ':'
            // into one spelling; resolve walks the chain, validates, and
            // substitutes to the underlying before any downstream stage.
            std::vector<std::string> segs;
            std::vector<int> toks;
            bool global = false;
            if (!parseQualifiedName(segs, toks, global)) return "";
            if (global) type = "::";
            for (std::size_t i = 0; i < segs.size(); ++i) {
                if (i > 0) type += ":";
                type += segs[i];
            }
            if (seg_toks) *seg_toks = std::move(toks);
        } else {
            error("Expected type.");
            return "";
        }
        // A CHAIN of postfix wrappers, each applied to the type ACCUMULATED so far,
        // in source order: `T[]` (iterator), sized fixed-array dims `T[N]` / the comma
        // form `T[a,b]` -> `[b][a]`, or `T^` (reference). They chain in ANY order, so
        // `int^[2]` (array of refs) and `int[3]^` (ref to array) are distinct types.
        // A LITERAL dim bakes its size into the spelling; a const-EXPRESSION dim (only
        // when a dim_sink is provided) spells a provisional `[1]` and pushes the expr
        // to the sink for constfold to fold + bake.
        //
        // SINK PUSH ORDER: bakeDimsWalk (constfold) consumes dim_exprs in PRE-ORDER —
        // outermost kArray's dims first, then descend through `^`/`[]` to inner ones.
        // SOURCE order matches that only for a PURE-array chain (one kArray with many
        // dims). When a `^`/`[]` splits the chain into multiple array RUNS, the LAST
        // run in source order corresponds to the OUTERMOST kArray in the tree. So we
        // buffer dim_exprs per run, then push runs into dim_sink in REVERSE order at
        // chain end. Within a run, source order is preserved (the comma transpose has
        // already reversed within a single bracket group).
        bool outermost_sized = false;   // the LAST wrapper emitted is a sized dim
        std::vector<std::vector<std::unique_ptr<parse::Node>>> runs;
        bool in_run = false;
        auto closeRun = [&]() { in_run = false; };
        while (true) {
            if (peek().kind == token::Kind::kLBracket
                && peekKind(1) == token::Kind::kRBracket) {
                advance(); advance();   // `[` `]`
                type += "[]";           // iterator
                outermost_sized = false;
                closeRun();
            } else if (peek().kind == token::Kind::kLBracket) {
                // A SIZED dim `[N]` (the `[]` iterator was handled above). For `new T[n]`
                // this `[` opens the runtime alloc COUNT, not a type dim — stop, leave it.
                if (top_dim == TopDim::StopBeforeSized) break;
                advance();   // [
                std::vector<std::string> bracket_spell;
                std::vector<std::unique_ptr<parse::Node>> bracket_expr;
                // A non-literal dim is allowed only with a dim_sink to fold it.
                if (!parseBracketGroup(bracket_spell, bracket_expr,
                                       /*allow_expr=*/dim_sink != nullptr)) {
                    return "";
                }
                if (dim_sink && !in_run) {
                    runs.emplace_back();
                    in_run = true;
                }
                for (std::size_t k = bracket_spell.size(); k-- > 0; ) {  // comma transpose
                    type += "[" + bracket_spell[k] + "]";
                    if (dim_sink) runs.back().push_back(std::move(bracket_expr[k]));
                }
                outermost_sized = true;
            } else if (peek().kind == token::Kind::kBitXor) {
                advance();
                type += "^";
                outermost_sized = false;
                closeRun();
            } else if (peek().kind == token::Kind::kXorXor) {
                // `^^` lexes as one logical-xor token; in a type-suffix position
                // it is two pointer levels (`int^^`). A second closeRun() on the
                // back-to-back level is a no-op.
                advance();
                type += "^^";
                outermost_sized = false;
                closeRun();
            } else {
                break;
            }
        }
        // TOP-LEVEL SIZED DIM ON THE NAME: at a named declarator (reject_top_dim),
        // the OUTERMOST sized dim must be written on the name, not inline. A dim
        // NESTED inside a pointer/iterator wrapper (`int[3]^ v`) stays in the type —
        // only a top-level one (`int[3] v`, `int^[3] v`) is rejected.
        if (top_dim == TopDim::RejectToName && outermost_sized) {
            error("An array size belongs on the declared name; "
                  "write 'T name[N]', not 'T[N] name'.");
            return "";
        }
        // Emit runs in REVERSE order so the sink's push order matches bakeDimsWalk's
        // pre-order traversal of the interned type tree.
        if (dim_sink) {
            for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                for (auto& d : *it) dim_sink->push_back(std::move(d));
            }
        }
        return type;
    }

    // Parse fixed-size array dims that FOLLOW a declared name (`name[5]`,
    // `grid[3][5]`, comma form `g[3,5]` -> `[5][3]`). Appends each `[dim]` to
    // `type`; a const-EXPRESSION dim bakes a provisional `[1]` and pushes a dim_expr
    // (folded + baked for real in constfold). Returns false (after error()) on a
    // malformed / non-positive literal dim. Shared by var-decls and parameters.
    bool parseNameDims(std::string& type,
                       std::vector<std::unique_ptr<parse::Node>>& dim_exprs,
                       bool& any_dim_expr) {
        while (peek().kind == token::Kind::kLBracket) {
            advance();   // [
            std::vector<std::string> bracket_spell;
            std::vector<std::unique_ptr<parse::Node>> bracket_expr;
            // Name-dims always allow a const-EXPRESSION dim (the dim_exprs sink folds it).
            if (!parseBracketGroup(bracket_spell, bracket_expr, /*allow_expr=*/true)) {
                return false;
            }
            // Append this bracket's dims reversed (the comma transpose).
            for (std::size_t k = bracket_spell.size(); k-- > 0; ) {
                type += "[" + bracket_spell[k] + "]";
                if (bracket_expr[k]) any_dim_expr = true;
                dim_exprs.push_back(std::move(bracket_expr[k]));
            }
        }
        return true;
    }

    // Whether a declarator site requires / forbids a name, or is a list slot.
    // Required: var-decl / param / for-var (a name, with the top-level sized dim moved to
    // it). Forbidden: a nameless type position (return type, alias / cast / conversion
    // target, sizeof operand). The doc's "ListSlot" — a `( declarator-list )` slot — is
    // realized as two parse modes, because a bare identifier is a TYPE in one and a NAME
    // in the other:
    //   ListSlot: a tuple-TYPE slot — the slot IS a type, anonymous (force has_type); a
    //     trailing identifier is parsed only to diagnose it ("too many names"), and a
    //     top-level inline sized dim is allowed (no name to host it).
    //   BindSlot: a destructure slot — a binding; the type is OPTIONAL (a bare identifier
    //     is the bound name, inferred), the name is OPTIONAL (a typed-no-name slot is a
    //     discard), and the top-level sized dim moves to the name as at a Required site.
    enum class NamePolicy { Required, Forbidden, ListSlot, BindSlot };

    // A parsed declarator: `[core-type] [name] [name-dims]` — the unified shape behind
    // every named type site, the nameless type sites, and tuple-type slots. `type` is the full
    // spelling (core + name-dims); `typeless` means no core-type was written (the type
    // is inferred). `dim_exprs` holds const-EXPRESSION dims in name-then-type
    // (pre-order) order for constfold's bake.
    struct Declarator {
        std::string type;
        std::vector<int> type_seg_toks;
        std::string name;
        int name_tok = -1;
        std::vector<std::string> qualifier;   // a qualified declared name (var-decl)
        std::vector<int> qualifier_toks;
        bool global_qualified = false;
        int file_id = -1;
        int tok = -1;
        std::vector<std::unique_ptr<parse::Node>> dim_exprs;
        bool any_dim_expr = false;
        bool typeless = false;
    };

    // Parse a declarator under `policy`. A leading type-start (or qualified-typed-decl
    // / tuple-type shape) is the core-type; otherwise the declarator is TYPELESS and a
    // lone identifier is the name. A Forbidden / ListSlot site has the whole declarator
    // as the core-type (always present); ListSlot additionally parses an optional
    // trailing name so the caller can diagnose it. A BindSlot uses the normal heuristic
    // but the name is OPTIONAL (a typed-no-name slot — the caller treats it as a
    // discard). `parse_name_dims` enables name-anchored array dims after the name
    // (var-decl / param shape; a for-var has none). On a Required site with no name,
    // `missing_name_error` is reported. Returns false on a diagnosed error.
    bool parseDeclarator(NamePolicy policy, bool parse_name_dims, bool allow_qualified,
                         char const* missing_name_error, Declarator& d) {
        d.file_id = peek().file_id;
        d.tok = pos;
        std::vector<std::unique_ptr<parse::Node>> type_dims;
        // A leading `const` (type qualifier) or `mutable` (a misplaced param qualifier,
        // diagnosed by parseType) begins a type. At a statement-level var decl the
        // `const` is the named-constant marker and is already consumed before here, so
        // these only fire for params / class fields / inner type positions.
        // A Forbidden site is a NAMELESS type position (return type, alias / cast /
        // conversion target, sizeof operand) and a ListSlot is a tuple-TYPE slot: in
        // both the whole declarator is the core-type (always present) and any trailing
        // name is not part of the type, so the trailing-name lookaheads don't apply —
        // force has_type. (For ListSlot a trailing identifier is then the to-be-rejected
        // slot name, picked up by have_name below.)
        bool has_type = policy == NamePolicy::Forbidden || policy == NamePolicy::ListSlot
            || isTypeStart(peek().kind) || looksLikeQualifiedTypedDecl()
            || peek().kind == token::Kind::kConst || peek().kind == token::Kind::kMutable
            || (peek().kind == token::Kind::kLParen && looksLikeTupleTypeDecl());
        if (has_type) {
            // A named declarator (Required) or a destructure slot (BindSlot, which binds
            // a name) moves the top-level sized dim to the name; an anonymous Forbidden /
            // ListSlot site has no name to host it, so an inline top-level dim is allowed.
            TopDim top_dim = (policy == NamePolicy::Required
                              || policy == NamePolicy::BindSlot)
                ? TopDim::RejectToName : TopDim::Consume;
            d.type = parseType(&d.type_seg_toks, top_dim, &type_dims);
            if (fatal) return false;
        } else {
            d.typeless = true;   // no core-type; the type is inferred
        }
        bool have_name = policy != NamePolicy::Forbidden
            && (peek().kind == token::Kind::kIdentifier
                || (allow_qualified && peek().kind == token::Kind::kColonColon));
        if (have_name) {
            if (allow_qualified) {
                // The declared name may be qualified (`int Space:kSix = 6` — an inline
                // namespace member). The leaf segment is the name; the rest qualifies.
                std::vector<std::string> segs;
                std::vector<int> toks;
                if (!parseQualifiedName(segs, toks, d.global_qualified)) return false;
                d.name = segs.back();
                d.name_tok = toks.back();
                segs.pop_back();
                toks.pop_back();
                d.qualifier = std::move(segs);
                d.qualifier_toks = std::move(toks);
            } else {
                d.name = peek().text;
                d.name_tok = pos;
                advance();
            }
            if (parse_name_dims
                && !parseNameDims(d.type, d.dim_exprs, d.any_dim_expr)) {
                return false;
            }
        } else if (policy == NamePolicy::Required) {
            error(missing_name_error);
            return false;
        }
        // Name-dims are the OUTER array (already appended to the spelling); the
        // type-position const-dims are inner. Concatenate name-then-type so the flat
        // list matches bakeNodeDims' pre-order tree walk.
        for (auto& t : type_dims) { if (t) d.any_dim_expr = true; d.dim_exprs.push_back(std::move(t)); }
        return true;
    }

    // Pure lookahead: do the tokens from the current position form a (qualified)
    // identifier TYPE spelling, with an optional type-suffix run, immediately
    // followed by an identifier? That is a typed var decl whose type is an
    // identifier type (alias / enum / class), possibly a reference / iterator /
    // sized array: `Space:Dir x`, `Integer^ ref`, `Integer[] iter`, `Vec2[5]^ v`.
    // Opposed to a name leading a call / assignment (`Space:foo()`, `Space:kX = 1`),
    // a deref store (`p^ = v`), or an index store (`arr[i] = v`) — those put an
    // operator, not a name, after the suffix run (see typeSuffixesThenName).
    // Primitive-typed decls don't come here (isTypeStart catches them). Consumes
    // nothing.
    bool looksLikeQualifiedTypedDecl() const {
        int o = 0;
        if (peekKind(o) == token::Kind::kColonColon) o++;
        if (peekKind(o) != token::Kind::kIdentifier) return false;
        o++;
        while (peekKind(o) == token::Kind::kColon) {
            if (peekKind(o + 1) != token::Kind::kIdentifier) return false;
            o += 2;
        }
        // The core type is the qualified name; a type-suffix run then the name
        // decides decl-vs-statement (shared with the tuple-led gate below).
        return typeSuffixesThenName(o);
    }

    // From offset `o` (just past a core type), skip a MAXIMAL run of type-suffix
    // tokens, then require the declared name. The suffix run is `^` / `^^`
    // (reference levels) and bracket groups — empty `[]` (iterator) OR sized
    // `[N]` / `[a,b]` — interleaved in ANY order, mirroring parseType's real
    // suffix chain (`int[3]^`, `int^[3]`, `(const int)[5]^` are all types). The
    // TRAILING IDENTIFIER is the decl-vs-statement discriminator: a store or
    // call puts an operator (`=`, `.`, `(`, `;`, ...) after the suffix run, never
    // a bare name — so `arr[i] = v` and `p^ = v` stay statements. Consumes
    // nothing (operates on a copy of `o`).
    bool typeSuffixesThenName(int o) const {
        while (true) {
            if (peekKind(o) == token::Kind::kBitXor
                || peekKind(o) == token::Kind::kXorXor) {   // ^ / ^^
                o++;
                continue;
            }
            if (peekKind(o) == token::Kind::kLBracket) {    // [] / [N] / [a,b]
                int depth = 1;
                o++;
                while (depth > 0) {
                    token::Kind k = peekKind(o);
                    if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                        return false;
                    if (k == token::Kind::kLBracket) depth++;
                    else if (k == token::Kind::kRBracket) depth--;
                    o++;
                }
                continue;
            }
            break;
        }
        return peekKind(o) == token::Kind::kIdentifier;
    }

    // Pure lookahead: does a leading `(...)` form a tuple-TYPE declaration —
    // `(T0, T1) name` — as opposed to a parenthesized / tuple-literal expression
    // statement? Scan to the matching `)`, skip an optional type-suffix run
    // (`(const int)[5]^ p`), and require a trailing identifier (the var name).
    // Consumes nothing.
    bool looksLikeTupleTypeDecl() const {
        if (peekKind(0) != token::Kind::kLParen) return false;
        int o = 1, depth = 1;
        while (depth > 0) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) depth--;
            o++;
        }
        // The balanced `(...)` is the core type; a type-suffix run then the name
        // decides decl-vs-statement (shared with the ident-led gate above).
        return typeSuffixesThenName(o);
    }

    // After `new`, a leading `(` is ambiguous: a placement address `new (addr) T`, or a
    // `(`-led ELEMENT type (a tuple `(int,int)`, a grouped `(const int)^`). Decide by what
    // FOLLOWS the balanced `(...)`, never by classifying its contents (a bare `(Vec3)` is
    // type-vs-expr-undecidable without name resolution). A placement `(addr)` is ALWAYS
    // followed by an element type, which can only START with a primitive keyword, an
    // identifier, `::`, `const`, or a nested `(`. A `(`-led element type is followed only
    // by a suffix / terminator (`^ ^^ [] [N] ; , ) { EOF`). So a type-start after the `)`
    // => placement; anything else => the `(...)` IS the element type. No backtracking.
    // Corner: `new (Foo)(3)` reads as placement, not `new Foo(3)` — parens around a single
    // class name are noise; write it without them.
    bool newParenStartsPlacement() const {
        if (peekKind(0) != token::Kind::kLParen) return false;
        int o = 1, depth = 1;
        while (depth > 0) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) depth--;
            o++;
        }
        token::Kind after = peekKind(o);
        return isTypeStart(after)
            || after == token::Kind::kIdentifier
            || after == token::Kind::kColonColon
            || after == token::Kind::kConst
            || after == token::Kind::kLParen;
    }

    // Pure lookahead: does a leading `(...)` form a tuple DESTRUCTURE assignment —
    // `(a, b, ) = tuple` — i.e. a `(...)` immediately followed by `=`? (Opposed to
    // a tuple-type decl, which has a trailing name, or a paren-expr statement.)
    bool looksLikeTupleDestructure() const {
        if (peekKind(0) != token::Kind::kLParen) return false;
        int o = 1, depth = 1;
        while (depth > 0) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) depth--;
            o++;
        }
        token::Kind after = peekKind(o);
        return after == token::Kind::kEquals       // copy destructure
            || after == token::Kind::kArrowLeft    // move destructure
            || after == token::Kind::kArrowBoth;   // swap destructure
    }

    // Pure lookahead at parsePrimary's LParen branch — the outer `(` is already
    // consumed, so peek(0) is the FIRST INNER token. Recognizes a `(TARGET = ...)`
    // value-conversion target by its LEAD: either a balanced `(...)` (a tuple-led
    // target — `((char,char)=tpl)`, `((int,int)[2]=arr)`) or a (possibly `::`-global
    // / `:`-member qualified) NAME (an identifier-led target — `(String=x)`,
    // `(Ns:T^=p)`, `(Vec3[2]=a)`); then a chain of type suffixes (`^`, `[]`, sized
    // `[N]`), then `=`. UNAMBIGUOUS: slids has no assignment-EXPRESSION, so a top-level
    // `=` after a type spelling can only open a conversion. Non-matches fall through to
    // the paren-expr path: `(x)` (no `=`), `(x == y)` (a distinct token), `(x.f = e)`
    // (a `.`, not a suffix), `((1,2),(3,4))` (a `,`, not a suffix/`=` after the inner
    // tuple). A PRIMITIVE-keyword lead needs no lookahead — isTypeStart catches it at
    // the call site.
    bool looksLikeConvTarget() const {
        int o = 0;
        if (peekKind(o) == token::Kind::kLParen) {          // tuple-led: skip `(...)`
            int depth = 1;
            o++;
            while (depth > 0) {
                token::Kind k = peekKind(o);
                if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                    return false;
                if (k == token::Kind::kLParen) depth++;
                else if (k == token::Kind::kRParen) depth--;
                o++;
            }
        } else {                                            // identifier-led: a name
            if (peekKind(o) == token::Kind::kColonColon) o++;       // global `::T`
            if (peekKind(o) != token::Kind::kIdentifier) return false;
            o++;
            while (peekKind(o) == token::Kind::kColon) {            // `A:B:C` segments
                o++;                                        // (parseQualifiedName spelling)
                if (peekKind(o) != token::Kind::kIdentifier) return false;
                o++;
            }
        }
        // A chain of type-suffix wrappers between the lead and the `=`: `^`, `[]`,
        // `[ ... ]` (scan past balanced brackets — they carry literal/const dims).
        for (;;) {
            token::Kind k = peekKind(o);
            if (k == token::Kind::kBitXor || k == token::Kind::kXorXor) { o++; continue; }
            if (k == token::Kind::kLBracket) {
                int bd = 1;
                o++;
                while (bd > 0) {
                    token::Kind bk = peekKind(o);
                    if (bk == token::Kind::kEndOfFile
                        || bk == token::Kind::kEndOfInput) return false;
                    if (bk == token::Kind::kLBracket) bd++;
                    else if (bk == token::Kind::kRBracket) bd--;
                    o++;
                }
                continue;
            }
            break;
        }
        return peekKind(o) == token::Kind::kEquals;
    }

    // Parse a for-varlist variable head: an optional type then the variable
    // name. The decl is TYPELESS (vtype left empty -> inferred from the
    // initializer, or a reuse of an enclosing local) when the leading tokens
    // are neither a primitive type-start nor a qualified typed-decl shape
    // (`Ident Ident` / `Ident:Ident... Ident`); then the leading identifier is
    // the variable name. Consumes the type (if any) and the name; never the
    // trailing ':' / '=' / ',' / ')'. Returns false on a diagnosed error.
    bool parseForVarHead(std::string& vtype, std::string& vname,
                         int& v_file, int& v_tok, int& vname_tok,
                         std::vector<std::unique_ptr<parse::Node>>& v_dims) {
        // A for-var is a declarator with no name-dims (the loop var binds one element).
        Declarator d;
        if (!parseDeclarator(NamePolicy::Required, /*parse_name_dims=*/false,
                             /*allow_qualified=*/false,
                             "Expected a variable name in the for-loop.", d)) {
            return false;
        }
        vtype = std::move(d.type);
        vname = std::move(d.name);
        v_file = d.file_id;
        v_tok = d.tok;
        vname_tok = d.name_tok;
        v_dims = std::move(d.dim_exprs);
        return true;
    }

    // Parse a (possibly qualified) name at the current token. Fills `segments`
    // with each `:`-separated identifier and `toks` with each segment's token
    // index (for per-segment carets); sets `global` for a leading `::`.
    // `Space:Nested:kFour` -> {Space, Nested, kFour}; `::kBest` -> global, {kBest}.
    bool parseQualifiedName(std::vector<std::string>& segments,
                            std::vector<int>& toks, bool& global) {
        global = false;
        if (peek().kind == token::Kind::kColonColon) {
            global = true;
            advance();
        }
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a name.");
            return false;
        }
        segments.push_back(peek().text);
        toks.push_back(pos);
        advance();
        // A qualified NAME segment is always an identifier (or `self`), never `op` — so a
        // `:op` here is NOT part of the name: it starts a qualified OPERATOR-definition head
        // (`Class:op+=(...)`), which the caller (parseFunctionDef) owns. Stop before it,
        // leaving the `:op` unconsumed, rather than erroring on `op`.
        while (peek().kind == token::Kind::kColon
               && peekKind(1) != token::Kind::kOp) {
            advance();
            // `Base:self` — the receiver viewed as the base sub-object. `self` (a
            // reserved word) is allowed as a qualified-name segment; resolve reframes
            // `Base:self` to `self._$base`.
            if (peek().kind == token::Kind::kSelf) {
                segments.push_back("self");
                toks.push_back(pos);
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected a name after ':'.");
                return false;
            }
            segments.push_back(peek().text);
            toks.push_back(pos);
            advance();
        }
        return true;
    }

    // A qualified name inside a switch case-label, where the chain's trailing `:`
    // is the label TERMINATOR, not a qualifier separator. Scans the maximal
    // `:`-ident chain; if it is NOT followed by the terminator `:`, the chain
    // over-consumed by one segment (the last ident starts the clause body), so
    // rewind one. This resolves the v1 `case Dir:N:` ambiguity for every
    // realistic body (keyword-, brace-, or identifier-led). Consumes through the
    // last label segment, leaving pos at the terminator `:`.
    bool parseQualifiedNameCaseLabel(std::vector<std::string>& segments,
                                     std::vector<int>& toks, bool& global) {
        global = false;
        if (peek().kind == token::Kind::kColonColon) { global = true; advance(); }
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a name.");
            return false;
        }
        segments.push_back(peek().text);
        toks.push_back(pos);
        advance();
        while (peek().kind == token::Kind::kColon
               && peekKind(1) == token::Kind::kIdentifier) {
            advance();   // ':'
            segments.push_back(peek().text);
            toks.push_back(pos);
            advance();   // ident
        }
        // Over-consumed (no terminator `:` follows) -> the last segment is the
        // body's first identifier; give it back so the preceding `:` terminates.
        if (peek().kind != token::Kind::kColon && segments.size() >= 2) {
            pos -= 2;
            segments.pop_back();
            toks.pop_back();
        }
        return true;
    }

    // One `Type=operand` link of a value conversion. Called positioned at the
    // target type (the leading `(` already consumed; the trailing `)` is the
    // caller's). The operand is another link when it too begins with a type
    // keyword (`(float64 = intptr = ref)`), else a full expression.
    std::unique_ptr<parse::Node> parseConvertChain(int file_id) {
        int op_tok = pos;
        Declarator d;
        if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                             /*allow_qualified=*/true, nullptr, d)) {
            return nullptr;
        }
        std::string target = std::move(d.type);
        std::vector<int> target_seg_toks = std::move(d.type_seg_toks);
        std::vector<std::unique_ptr<parse::Node>> conv_dims = std::move(d.dim_exprs);
        if (!expect(token::Kind::kEquals, "=")) return nullptr;
        std::unique_ptr<parse::Node> operand =
            isTypeStart(peek().kind) ? parseConvertChain(file_id) : parseExpr();
        if (!operand) return nullptr;
        auto node = newNodeAt(parse::Kind::kConvertExpr, file_id, op_tok);
        node->return_type = widen::internOrNone(target);
        node->return_type_seg_toks = std::move(target_seg_toks);
        if (d.any_dim_expr) node->dim_exprs = std::move(conv_dims);
        node->children.push_back(std::move(operand));
        return node;
    }

    std::unique_ptr<parse::Node> parsePrimary() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kStringLiteral) {
            auto node = newNodeHere(parse::Kind::kStringLiteral);
            node->text = t.text;
            advance();
            // adjacent string-literal concat at the token level
            while (peek().kind == token::Kind::kStringLiteral) {
                node->text += peek().text;
                advance();
            }
            return node;
        }
        if (t.kind == token::Kind::kIntLiteral) {
            auto node = newNodeHere(parse::Kind::kIntLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kUintLiteral) {
            auto node = newNodeHere(parse::Kind::kUintLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kCharLiteral) {
            auto node = newNodeHere(parse::Kind::kCharLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kFloatLiteral) {
            auto node = newNodeHere(parse::Kind::kFloatLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kBoolLiteral) {
            auto node = newNodeHere(parse::Kind::kBoolLiteral);
            node->text = t.text;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kNullptr) {
            auto node = newNodeHere(parse::Kind::kNullptrLiteral);
            advance();
            return node;
        }
        if (t.kind == token::Kind::kSelf) {
            // `self` — the receiver OBJECT. Parsed as a kIdentExpr named "self"
            // (a reserved word, so it never collides with a user name); resolve
            // rewrites it to `_$recv^` (the deref of the implicit receiver
            // pointer), so `^self`, `self.field`, and `self.method()` reuse the
            // ordinary deref / field / method-call machinery.
            auto node = newNodeHere(parse::Kind::kIdentExpr);
            node->name = "self";
            node->name_tok = pos;
            advance();
            return node;
        }
        if (t.kind == token::Kind::kIdentifier
            || t.kind == token::Kind::kColonColon) {
            int name_file = t.file_id;
            int start_tok = pos;
            std::vector<std::string> segs;
            std::vector<int> toks;
            bool global = false;
            bool ok = case_label_
                ? parseQualifiedNameCaseLabel(segs, toks, global)
                : parseQualifiedName(segs, toks, global);
            if (!ok) return nullptr;
            auto node = newNodeAt(parse::Kind::kIdentExpr, name_file, start_tok);
            node->name = segs.back();
            node->name_tok = toks.back();
            segs.pop_back();
            toks.pop_back();
            node->qualifier = std::move(segs);
            node->qualifier_toks = std::move(toks);
            node->global_qualified = global;
            return node;
        }
        if (t.kind == token::Kind::kLParen) {
            int lp = pos;
            advance();
            // `(Type=expr)` value conversion. A primitive type-start right after
            // `(` unambiguously marks a conversion (no expr starts with a type
            // keyword). A tuple-led target — inner `(...)` balanced parens then
            // `=` — and an IDENTIFIER-led target (a user-named class / alias, then
            // `=`) are recognized via lookahead and routed through the same chain.
            // Chains `(A=B=expr)` nest right-to-left (one kConvertExpr per `Type=`
            // link).
            if (isTypeStart(peek().kind) || looksLikeConvTarget()) {
                auto conv = parseConvertChain(t.file_id);
                if (!conv) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                return conv;
            }
            auto inner = parseExpr();
            if (!inner) return nullptr;
            if (peek().kind == token::Kind::kComma) {
                // Tuple literal `(e0, e1, ...)` — the comma is the marker; a
                // size-1 `(e)` is just a parenthesized expr (the branch below).
                auto tup = newNodeAt(parse::Kind::kTupleExpr, t.file_id, lp);
                tup->children.push_back(std::move(inner));
                while (peek().kind == token::Kind::kComma) {
                    advance();   // ,
                    auto e = parseExpr();
                    if (!e) return nullptr;
                    tup->children.push_back(std::move(e));
                }
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                return tup;
            }
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return inner;
        }
        if (t.kind == token::Kind::kNew) {
            // new T             -> a single object (T^)
            // new T[n]          -> n objects (T[]); n is a RUNTIME count
            // new T[n][d]...    -> n objects whose element is T[d]... (multi-dim: the
            //                      FIRST dim is the count, the rest are the element's dims)
            // new (addr) T[n]   -> placement: construct at addr, no allocation
            // new T(args)       -> constructor args
            // T may be ANY type — primitive, pointer, iterator, tuple, grouped (`(const
            // int)^`), alias, or a composed chain. children[0] = count expr (or null),
            // [1] = placement addr (or null), [2] = ctor args (or null).
            int new_file = t.file_id;
            int new_tok = pos;
            advance();   // new
            std::unique_ptr<parse::Node> addr;
            // A leading `(` is EITHER a placement address `new (addr) T` OR a `(`-led
            // element type (`new (int,int)`, `new (const int)^`). newParenStartsPlacement
            // decides by the token after the balanced `(...)` (see its comment).
            if (peek().kind == token::Kind::kLParen && newParenStartsPlacement()) {
                advance();   // (
                addr = parseExpr();
                if (!addr) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
            }
            int elem_tok = pos;   // the element-type token, for "Cannot allocate"
            // The element type is a FULL type via the ONE parser, stopping before the
            // trailing `[n]` (the runtime alloc count, read below) — so `new` handles
            // every variable type (`int^`, `int[]`, `(int,int)`, `(const int)^`, `Vec3^`,
            // `int^[n]`, ...), not just a bare primitive / qualified name.
            std::string elem = parseType(nullptr, TopDim::StopBeforeSized);
            if (elem.empty()) return nullptr;
            std::unique_ptr<parse::Node> size;
            if (peek().kind == token::Kind::kLBracket) {
                advance();   // [
                size = parseExpr();
                if (!size) return nullptr;
                if (!expect(token::Kind::kRBracket, "]")) return nullptr;
                // Any FURTHER `[d]` dims are the ELEMENT type's trailing dims:
                // `new int[n][2][2]` = n copies of int[2][2]. Append them to the element
                // spelling (the count `[n]` above stays a separate runtime expr).
                if (peek().kind == token::Kind::kLBracket) {
                    std::vector<std::unique_ptr<parse::Node>> elem_dims;
                    bool any_dim_expr = false;
                    if (!parseNameDims(elem, elem_dims, any_dim_expr)) return nullptr;
                    if (any_dim_expr) {
                        error("A 'new' element array dimension must be a constant.");
                        return nullptr;
                    }
                }
            }
            // new T(args) -> constructor args (a kTupleExpr, like a class var-decl
            // init `Type name(args)`). The trailing `(args)` is distinct from the
            // LEADING new(addr) placement and from `[n]`; classify routes it through
            // the class construction path.
            std::unique_ptr<parse::Node> ctor_args;
            if (peek().kind == token::Kind::kLParen) {
                int a_file = peek().file_id;
                int a_tok = pos;
                advance();   // (
                auto tup = newNodeAt(parse::Kind::kTupleExpr, a_file, a_tok);
                if (!parseCallArgs(*tup)) return nullptr;
                ctor_args = std::move(tup);
            }
            auto node = newNodeAt(parse::Kind::kNewExpr, new_file, new_tok);
            node->return_type = widen::internOrNone(elem);
            node->name_tok = elem_tok;   // caret target for the allocate check
            node->children.push_back(std::move(size));        // [0] (may be null)
            node->children.push_back(std::move(addr));        // [1] (may be null)
            node->children.push_back(std::move(ctor_args));   // [2] (may be null)
            return node;
        }
        if (t.kind == token::Kind::kSizeof) {
            // sizeof(T) or sizeof(expr) — the byte size as an intptr. The paren
            // content is a TYPE when it starts with a type keyword (`int`,
            // `void^`, `int[]`); a bare identifier is ambiguous (alias vs
            // variable) and is parsed as an expression for resolve to dispatch
            // on (the same type-vs-value split as ##type). A string literal and
            // any other expression also take the expression path.
            int sz_file = t.file_id;
            int sz_tok = pos;
            advance();   // sizeof
            if (!expect(token::Kind::kLParen, "(")) return nullptr;
            auto node = newNodeAt(parse::Kind::kSizeofExpr, sz_file, sz_tok);
            if (isTypeStart(peek().kind)) {
                Declarator d;
                if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                     /*allow_qualified=*/false, nullptr, d)) {
                    return nullptr;
                }
                node->return_type = widen::internOrNone(d.type);
                if (d.any_dim_expr) node->dim_exprs = std::move(d.dim_exprs);
            } else {
                auto operand = parseExpr();
                if (!operand) return nullptr;
                node->children.push_back(std::move(operand));
            }
            if (!expect(token::Kind::kRParen, ")")) return nullptr;
            return node;
        }
        if (t.kind == token::Kind::kHashHash) {
            // Compile-time stringify macros. All but ##type resolve to a string
            // literal right here; ##type needs the operand's inferred type, so it
            // becomes a kStringifyType that classify lowers. The node is stamped
            // at the ## token — ##file / ##line carry the call-site location.
            int hh_file = t.file_id;
            int hh_tok = pos;
            int hh_line = t.line;
            advance();   // ##
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected a macro name after '##'.");
                return nullptr;
            }
            std::string kw = peek().text;
            int kw_tok = pos;
            advance();
            if (kw == "type") {
                if (!expect(token::Kind::kLParen, "(")) return nullptr;
                auto operand = parseExpr();
                if (!operand) return nullptr;
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                auto node = newNodeAt(parse::Kind::kStringifyType, hh_file, hh_tok);
                node->children.push_back(std::move(operand));
                return node;
            }
            if (kw == "name") {
                // ##name reproduces its argument's lexed text verbatim — the
                // content is NOT parsed or type-checked. Scan to the matching ')'
                // by bracket depth and concatenate the token values (whitespace
                // already dropped by the lexer).
                if (!expect(token::Kind::kLParen, "(")) return nullptr;
                std::string text;
                int depth = 1;
                while (peek().kind != token::Kind::kEndOfFile
                       && peek().kind != token::Kind::kEndOfInput) {
                    token::Kind k = peek().kind;
                    if (k == token::Kind::kLParen || k == token::Kind::kLBracket
                        || k == token::Kind::kLBrace) {
                        depth++;
                    } else if (k == token::Kind::kRParen
                               || k == token::Kind::kRBracket
                               || k == token::Kind::kRBrace) {
                        if (--depth == 0) break;
                    }
                    text += peek().text;
                    advance();
                }
                if (!expect(token::Kind::kRParen, ")")) return nullptr;
                auto node = newNodeAt(parse::Kind::kStringLiteral, hh_file, hh_tok);
                node->text = std::move(text);
                return node;
            }
            if (kw == "file" || kw == "line" || kw == "func"
                || kw == "date" || kw == "time") {
                auto node = newNodeAt(parse::Kind::kStringLiteral, hh_file, hh_tok);
                if (kw == "file") {
                    node->text = baseName(tokens.files[hh_file].path);
                } else if (kw == "line") {
                    node->text = std::to_string(hh_line);
                } else if (kw == "func") {
                    node->text = current_func;
                } else if (kw == "date") {
                    captureClock();
                    node->text = clock_date;
                } else {  // time
                    captureClock();
                    node->text = clock_time;
                }
                return node;
            }
            errorAt(kw_tok, "Unknown '##' macro '" + kw + "'.");
            return nullptr;
        }
        error("Expected expression.");
        return nullptr;
    }

    // Does this token begin a primary (operand) expression? Used to tell a
    // postfix-deref `x^` (the `^` is followed by a terminator/operator) from a
    // binary XOR `a ^ b` (the `^` is followed by an operand). Both lex to the
    // same `^`; this lookahead is the disambiguator.
    static bool startsPrimary(token::Kind k) {
        return k == token::Kind::kIdentifier
            || k == token::Kind::kColonColon
            || k == token::Kind::kIntLiteral
            || k == token::Kind::kUintLiteral
            || k == token::Kind::kCharLiteral
            || k == token::Kind::kFloatLiteral
            || k == token::Kind::kBoolLiteral
            || k == token::Kind::kStringLiteral
            || k == token::Kind::kNullptr
            || k == token::Kind::kLParen
            || k == token::Kind::kHashHash;
    }

    // Field access, indexing, postfix-^/^^, postfix-++/-- all slot in here as
    // their phases land. Today: postfix-call, postfix-deref, postfix-++/--.
    // Parse one subscript bracket onto `base`. A bracket may hold a comma-list
    // `a[x,y]` — the "natural order" form — and a comma TRANSPOSES
    // (`a[a0,..,z]` -> `a[z]...[a0]`), so the indices apply REVERSED as a chained
    // subscript: `a[x,y]` == `a[y][x]`. Positioned at `[`; consumes through `]`.
    // Shared by the expression-postfix and lvalue-store paths.
    std::unique_ptr<parse::Node> parseSubscript(std::unique_ptr<parse::Node> base) {
        int op_file = peek().file_id;
        int op_tok = pos;
        advance();   // [
        std::vector<std::unique_ptr<parse::Node>> indices;
        while (true) {
            auto index = parseExpr();
            if (!index) return nullptr;
            indices.push_back(std::move(index));
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
        }
        if (!expect(token::Kind::kRBracket, "]")) return nullptr;
        for (std::size_t k = indices.size(); k-- > 0; ) {
            auto node = newNodeAt(parse::Kind::kIndexExpr, op_file, op_tok);
            node->children.push_back(std::move(base));
            node->children.push_back(std::move(indices[k]));
            base = std::move(node);
        }
        return base;
    }

    std::unique_ptr<parse::Node> parsePostfix(std::unique_ptr<parse::Node> base) {
        // Postfix chain: subscript `[i]` and dereference `^`, left to right.
        // A bare `^` whose following token begins an operand is binary XOR, not
        // deref — leave it for parseBitXor.
        while (true) {
            if (peek().kind == token::Kind::kLBracket) {
                base = parseSubscript(std::move(base));
                if (!base) return nullptr;
                continue;
            }
            if (peek().kind == token::Kind::kDot) {
                int op_file = peek().file_id;
                int op_tok = pos;
                advance();   // .
                if (peek().kind != token::Kind::kIdentifier) {
                    error("Expected a field name after '.'.");
                    return nullptr;
                }
                auto node = newNodeAt(parse::Kind::kFieldExpr, op_file, op_tok);
                node->children.push_back(std::move(base));
                node->name = peek().text;
                node->name_tok = pos;
                advance();   // field name
                base = std::move(node);
                continue;
            }
            if (peek().kind == token::Kind::kBitXor
                && !startsPrimary(peekKind(1))) {
                int op_file = peek().file_id;
                int op_tok = pos;
                advance();   // ^
                auto node = newNodeAt(parse::Kind::kDerefExpr, op_file, op_tok);
                node->children.push_back(std::move(base));
                base = std::move(node);
                continue;
            }
            // `^^` is one logical-xor token; when it does NOT lead an operand it is
            // a double dereference (`hdl^^`), emitted as two nested derefs.
            if (peek().kind == token::Kind::kXorXor
                && !startsPrimary(peekKind(1))) {
                int op_file = peek().file_id;
                int op_tok = pos;
                advance();   // ^^
                for (int i = 0; i < 2; i++) {
                    auto node = newNodeAt(parse::Kind::kDerefExpr, op_file, op_tok);
                    node->children.push_back(std::move(base));
                    base = std::move(node);
                }
                continue;
            }
            // A call `(args)` on a bare name — a function call OR a `Class(args)`
            // construction. INSIDE the loop so the chain continues after it:
            // `Class(a).field`, `fn(x)[i]`, `Class(a).method()` (the `.method`
            // becomes a kFieldExpr whose `(` is handled by the FieldExpr arm below).
            if (peek().kind == token::Kind::kLParen
                && base->kind == parse::Kind::kIdentExpr) {
                auto node = newNodeAt(parse::Kind::kCallExpr, base->file_id, base->tok);
                node->name = std::move(base->name);
                node->name_tok = base->name_tok;
                node->qualifier = std::move(base->qualifier);
                node->qualifier_toks = std::move(base->qualifier_toks);
                node->global_qualified = base->global_qualified;
                advance();   // (
                if (!parseCallArgs(*node)) return nullptr;
                base = std::move(node);
                continue;
            }
            // A call on a `recv.method` field access — a method-call EXPRESSION
            // used as a value (`x = obj.method()`, `f(obj.m())`). Build a
            // kMethodCallStmt (receiver = the field base, name = the method);
            // classify binds it against the receiver's class and desugar lowers it
            // to the lifted-symbol call exactly like the statement form.
            if (peek().kind == token::Kind::kLParen
                && base->kind == parse::Kind::kFieldExpr) {
                auto call = newNodeAt(parse::Kind::kMethodCallStmt,
                                      base->file_id, base->tok);
                call->name = std::move(base->name);
                call->name_tok = base->name_tok;
                call->children.push_back(std::move(base->children[0]));   // receiver
                advance();   // (
                if (!parseCallArgs(*call)) return nullptr;                // args -> [1..]
                base = std::move(call);
                continue;
            }
            break;
        }
        if (peek().kind == token::Kind::kPlusPlus
            || peek().kind == token::Kind::kMinusMinus) {
            char const* op = peek().kind == token::Kind::kPlusPlus ? "++" : "--";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto node = newNodeAt(parse::Kind::kPostIncExpr, op_file, op_tok);
            node->text = op;
            node->children.push_back(std::move(base));
            return node;
        }
        return base;
    }

    std::unique_ptr<parse::Node> parseUnary() {
        token::Kind k = peek().kind;
        if (k == token::Kind::kPlusPlus || k == token::Kind::kMinusMinus) {
            char const* pp = k == token::Kind::kPlusPlus ? "++" : "--";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kPreIncExpr, op_file, op_tok);
            node->text = pp;
            node->children.push_back(std::move(operand));
            return node;
        }
        // Prefix `^` is address-of (a reference to the operand lvalue). At the
        // start of a unary, `^` is unambiguous — binary XOR never leads.
        if (k == token::Kind::kBitXor) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();   // ^
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kAddrOfExpr, op_file, op_tok);
            node->children.push_back(std::move(operand));
            return node;
        }
        // Prefix `^^` is two address-of levels. Double address-of is nonsensical
        // (its operand is an rvalue), but parsing it as nested AddrOf lets resolve
        // reject `^^x` with the same diagnostic as the equivalent `^^^x`.
        if (k == token::Kind::kXorXor) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();   // ^^
            auto operand = parseUnary();
            if (!operand) return nullptr;
            for (int i = 0; i < 2; i++) {
                auto node = newNodeAt(parse::Kind::kAddrOfExpr, op_file, op_tok);
                node->children.push_back(std::move(operand));
                operand = std::move(node);
            }
            return operand;
        }
        // Prefix `#x` describes a postfix lvalue: it desugars HERE to the 5-tuple
        // `(##file, ##line, ##type(x), ##name(x), ^x)` — all of which already
        // exist (string-literal macros, kStringifyType, address-of, tuple
        // literal). x is parsed twice (one tree for ##type, one for ^x — there is
        // no parse-node cloner) and its lexed text captured for ##name. ^x
        // enforces that x is an lvalue.
        if (k == token::Kind::kHash) {
            int h_file = peek().file_id;
            int h_tok = pos;
            int h_line = peek().line;
            advance();   // #
            int x_start = pos;
            auto x_type = parseUnary();          // tree for ##type(x)
            if (!x_type) return nullptr;
            int x_end = pos;
            std::string x_text;
            for (int i = x_start; i < x_end; i++) x_text += tokens.tokens[i].text;
            pos = x_start;
            auto x_addr = parseUnary();           // re-parse: tree for ^x
            if (!x_addr) return nullptr;

            auto strlit = [&](std::string s) {
                auto n = newNodeAt(parse::Kind::kStringLiteral, h_file, h_tok);
                n->text = std::move(s);
                return n;
            };
            auto tuple = newNodeAt(parse::Kind::kTupleExpr, h_file, h_tok);
            tuple->children.push_back(strlit(baseName(tokens.files[h_file].path)));  // ##file
            tuple->children.push_back(strlit(std::to_string(h_line)));               // ##line
            auto ty = newNodeAt(parse::Kind::kStringifyType, h_file, h_tok);         // ##type(x)
            ty->quiet_diag = true;   // the sibling ^x reports a bad operand once
            ty->children.push_back(std::move(x_type));
            tuple->children.push_back(std::move(ty));
            tuple->children.push_back(strlit(std::move(x_text)));                    // ##name(x)
            auto addr = newNodeAt(parse::Kind::kAddrOfExpr, h_file, h_tok);          // ^x
            addr->children.push_back(std::move(x_addr));
            tuple->children.push_back(std::move(addr));
            return tuple;
        }
        // Prefix `<Type^>` is a pointer reinterpret cast. A `<` leading a unary
        // operand is unambiguous — binary `<` (less-than) only appears in a
        // comparison, where its right operand never starts with `<`. The cast
        // binds like a prefix unary (precedence 3), so the operand is another
        // unary — chained casts `<A^> <void^> x` nest right-to-left.
        if (k == token::Kind::kLt) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();   // <
            // `<const>` / `<mutable>` — a qualifier-ONLY cast: the angle brackets
            // hold just the keyword (no type), and the result keeps the operand's
            // pointer type with const added / removed. `<const Type^>` (a const-
            // qualified TARGET) is a normal type cast — disambiguated by a `>`
            // immediately after `const`. classify derives the result type from the
            // operand, so no target type is parsed here.
            if (peek().kind == token::Kind::kMutable
                || (peek().kind == token::Kind::kConst
                    && peekKind(1) == token::Kind::kGt)) {
                std::string qual =
                    (peek().kind == token::Kind::kMutable) ? "mutable" : "const";
                advance();   // const / mutable
                if (!expect(token::Kind::kGt, ">")) return nullptr;
                auto operand = parseUnary();
                if (!operand) return nullptr;
                auto node = newNodeAt(parse::Kind::kCastExpr, op_file, op_tok);
                node->text = qual;   // marks a qualifier cast (target derived in classify)
                node->children.push_back(std::move(operand));
                return node;
            }
            Declarator d;
            if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                 /*allow_qualified=*/false, nullptr, d)) {
                return nullptr;
            }
            if (!expect(token::Kind::kGt, ">")) return nullptr;
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kCastExpr, op_file, op_tok);
            node->return_type = widen::internOrNone(d.type);
            node->return_type_seg_toks = std::move(d.type_seg_toks);
            if (d.any_dim_expr) node->dim_exprs = std::move(d.dim_exprs);
            node->children.push_back(std::move(operand));
            return node;
        }
        char const* op = nullptr;
        if      (k == token::Kind::kPlus)   op = "+";
        else if (k == token::Kind::kMinus)  op = "-";
        else if (k == token::Kind::kNot)    op = "!";
        else if (k == token::Kind::kBitNot) op = "~";
        if (op) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = newNodeAt(parse::Kind::kUnaryExpr, op_file, op_tok);
            node->text = op;
            node->children.push_back(std::move(operand));
            return node;
        }
        auto prim = parsePrimary();
        if (!prim) return nullptr;
        return parsePostfix(std::move(prim));
    }

    std::unique_ptr<parse::Node> makeBinary(std::string op,
                                            std::unique_ptr<parse::Node> lhs,
                                            std::unique_ptr<parse::Node> rhs,
                                            int op_file, int op_tok) {
        auto node = newNodeAt(parse::Kind::kBinaryExpr, op_file, op_tok);
        node->text = std::move(op);
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs));
        return node;
    }

    std::unique_ptr<parse::Node> makeIdent(std::string name, int file, int tok) {
        auto node = newNodeAt(parse::Kind::kIdentExpr, file, tok);
        node->name = std::move(name);
        node->name_tok = tok;
        return node;
    }

    std::unique_ptr<parse::Node> parseMulDiv() {
        auto left = parseUnary();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kStar
            || peek().kind == token::Kind::kSlash
            || peek().kind == token::Kind::kPercent) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseUnary();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseAddSub() {
        auto left = parseMulDiv();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kPlus
            || peek().kind == token::Kind::kMinus) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseMulDiv();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseShift() {
        auto left = parseAddSub();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kLShift
            || peek().kind == token::Kind::kRShift) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseAddSub();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseRelational() {
        auto left = parseShift();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kLt
            || peek().kind == token::Kind::kGt
            || peek().kind == token::Kind::kLtEq
            || peek().kind == token::Kind::kGtEq) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseShift();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseEquality() {
        auto left = parseRelational();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kEqEq
            || peek().kind == token::Kind::kNotEq) {
            std::string op = peek().text;
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseRelational();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitAnd() {
        auto left = parseEquality();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitAnd) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseEquality();
            if (!right) return nullptr;
            left = makeBinary("&", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitXor() {
        auto left = parseBitAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitXor) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitAnd();
            if (!right) return nullptr;
            left = makeBinary("^", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseBitOr() {
        auto left = parseBitXor();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kBitOr) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitXor();
            if (!right) return nullptr;
            left = makeBinary("|", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseLogicalAnd() {
        auto left = parseBitOr();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kAnd) {
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseBitOr();
            if (!right) return nullptr;
            left = makeBinary("&&", std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseExpr() {
        auto left = parseLogicalAnd();
        if (!left) return nullptr;
        while (peek().kind == token::Kind::kOr
            || peek().kind == token::Kind::kXorXor) {
            std::string op = (peek().kind == token::Kind::kOr) ? "||" : "^^";
            int op_file = peek().file_id;
            int op_tok = pos;
            advance();
            auto right = parseLogicalAnd();
            if (!right) return nullptr;
            left = makeBinary(std::move(op), std::move(left), std::move(right),
                              op_file, op_tok);
        }
        return left;
    }

    std::unique_ptr<parse::Node> parseVarDeclStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        bool is_const = false;
        if (peek().kind == token::Kind::kConst) {
            is_const = true;
            advance();
        }
        // A typed-or-typeless declarator with a (possibly qualified) name:
        // `const int Space:kSix = 6;` is an inline namespace member; `const name = e`
        // is a typeless const (its type comes from the rhs). parseDeclarator detects
        // typeless (no type-start) and reads the qualified name + name-dims.
        Declarator d;
        if (!parseDeclarator(NamePolicy::Required, /*parse_name_dims=*/true,
                             /*allow_qualified=*/true, "Expected variable name.", d)) {
            return nullptr;
        }
        auto node = newNodeAt(parse::Kind::kVarDeclStmt, stmt_file, stmt_tok);
        node->name = std::move(d.name);
        node->name_tok = d.name_tok;
        node->qualifier = std::move(d.qualifier);
        node->qualifier_toks = std::move(d.qualifier_toks);
        node->global_qualified = d.global_qualified;
        if (d.any_dim_expr) node->dim_exprs = std::move(d.dim_exprs);
        node->return_type = widen::internOrNone(d.type);
        node->return_type_seg_toks = std::move(d.type_seg_toks);
        node->is_const = is_const;
        if (peek().kind == token::Kind::kEquals
            || peek().kind == token::Kind::kArrowLeft
            || peek().kind == token::Kind::kArrowBoth) {
            // `<--` is a default-move-init (the same copy as `=`, then desugar nulls the
            // init's pointer leaves). `<-->` is a swap-init: classify default-constructs
            // the fresh var then swaps it with the rhs (the fresh default flows back into
            // the source — "weird but allowed"). The rhs must be an existing lvalue.
            node->default_move_init = (peek().kind == token::Kind::kArrowLeft);
            node->default_swap_init = (peek().kind == token::Kind::kArrowBoth);
            advance();
            auto init = parseExpr();
            if (!init) return nullptr;
            node->children.push_back(std::move(init));
        } else if (peek().kind == token::Kind::kLParen) {
            // `Type name(args)` — construction init: the arg expressions fill the
            // field tuple left to right (missing fields take their default /
            // zero). Modeled as a kTupleExpr init so classify shares the
            // `= (tuple)` class-construction normalization; `()` is the
            // all-default form.
            int t_file = peek().file_id;
            int t_tok = pos;
            advance();   // (
            auto tup = newNodeAt(parse::Kind::kTupleExpr, t_file, t_tok);
            if (!parseCallArgs(*tup)) return nullptr;
            node->children.push_back(std::move(tup));
        } else if (is_const) {
            error("Constant declaration requires an initializer.");
            return nullptr;
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // `global;` — the statement that opens the global lifetime for its enclosing
    // scope. A block-scope global DECLARATION — `global int x;` or a group inside a
    // function — is delegated to parseGlobal below (a group is a namespace, and a
    // namespace nests in a function), so it needs no separate statement-position path.
    std::unique_ptr<parse::Node> parseGlobalStmt() {
        int g_file = peek().file_id, g_tok = pos;
        // `global;` opens the ONE global scope for the program — only inside `main`
        // (its lifetime is the process lifetime). current_func is the enclosing
        // function throughout its body, incl. nested blocks.
        if (peekKind(1) == token::Kind::kSemicolon) {
            advance();   // global
            if (current_func != "main") {
                errorAt(g_tok, "'global;' may appear only in 'main'.");
                return nullptr;
            }
            advance();   // ;
            return newNodeAt(parse::Kind::kGlobalScopeStmt, g_file, g_tok);
        }
        // Every other `global …` in statement position is the SAME grammar as at file
        // scope — a short form `global [Type] name = init;` (a scoped static) or a
        // group `global [name] (decls) { body }`. A group is a namespace, and a
        // namespace nests inside a function, so parseGlobal + the ordinary scope
        // machinery lower a block-scope group with no special casing.
        return parseGlobal();
    }

    // `global …` — a global variable declaration, in three spellings:
    //   LONG   `global [name] (decls) { body }` — a group; its `decls` become
    //          static members of the namespace `name` (or the unnamed global
    //          namespace, reached bare / via `::`, when anonymous). Group members
    //          are lowered to is_global var-decls; the group node is a is_global
    //          kNamespaceDecl. A non-empty body carries the group's `_()` / `~()`
    //          ctor/dtor (parsed below), which makes the group lazy.
    //   SHORT  `global [Type] name [= init];` — an anonymous single member; reuses
    //          parseVarDeclStmt.
    std::unique_ptr<parse::Node> parseGlobal() {
        int g_file = peek().file_id;
        int g_tok = pos;
        advance();   // global
        bool named = (peek().kind == token::Kind::kIdentifier
                      && peekKind(1) == token::Kind::kLParen);
        // An ANONYMOUS group `global (decls) {}` — its members promote into the
        // ENCLOSING scope (reached bare / via `::` at file scope, `Enclosing:member`
        // in a namespace/class), unlike a named group's `name:member`. Parsed as an
        // EMPTY-name is_global namespace; resolve explodes it into its member
        // var-decls before registration, so they flow through the bare-global path.
        bool anon = (peek().kind == token::Kind::kLParen);
        // Disambiguate an anonymous group `global (decls) {…}` from a TUPLE-TYPED
        // short form `global (T, U) name = …`: scan to the matching `)` — a `{` after
        // it opens a group body; anything else is a tuple type followed by the var
        // name, so it is a plain (tuple-typed) declaration handled by parseVarDeclStmt.
        if (anon) {
            int depth = 0;
            int o = 0;
            for (;; o++) {
                token::Kind k = peekKind(o);
                if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput
                    || k == token::Kind::kError) break;
                if (k == token::Kind::kLParen) depth++;
                else if (k == token::Kind::kRParen && --depth == 0) { o++; break; }
            }
            if (peekKind(o) != token::Kind::kLBrace) anon = false;   // tuple-typed decl
        }
        if (named || anon) {
            auto node = newNodeAt(parse::Kind::kNamespaceDecl, g_file, g_tok);
            node->is_global = true;
            if (named) { node->name = peek().text; node->name_tok = pos; advance(); }
            if (!expect(token::Kind::kLParen, "(")) return nullptr;
            // Reuse the field/param-list parser (typeless-with-init allowed, no
            // mutable), then convert each param into a global var-decl member.
            if (!parseParamList(node.get(), /*allow_mutable=*/false)) return nullptr;
            for (auto& p : node->params) {
                if (!p) continue;
                auto v = newNodeAt(parse::Kind::kVarDeclStmt, p->file_id, p->tok);
                v->name = std::move(p->name);
                v->name_tok = p->name_tok;
                v->return_type = p->return_type;
                v->is_global = true;
                v->dim_exprs = std::move(p->dim_exprs);
                if (!p->children.empty() && p->children[0])
                    v->children.push_back(std::move(p->children[0]));
                node->children.push_back(std::move(v));
            }
            node->params.clear();
            if (!expect(token::Kind::kLBrace, "{")) return nullptr;
            // Body: the group's constructor `_(){…}` and/or destructor `~(){…}`
            // (both together or neither — the pairing rule). A group WITH them is
            // lazily constructed on first access; one WITHOUT is static. Each is a
            // void, receiver-less member function `_$gctor` / `_$gdtor` (resolve
            // resolves its body with the group's namespace open, so members are bare).
            bool ctor_def = false, dtor_def = false;
            while (!fatal && peek().kind != token::Kind::kRBrace) {
                if (peek().kind == token::Kind::kEndOfFile
                    || peek().kind == token::Kind::kEndOfInput) {
                    error("Expected '}'."); return nullptr;
                }
                bool is_ctor = (peek().kind == token::Kind::kIdentifier
                                && peek().text == "_");
                bool is_dtor = (peek().kind == token::Kind::kBitNot);
                if (!is_ctor && !is_dtor) {
                    error("A global group body holds only the constructor '_()' "
                          "and destructor '~()'.");
                    return nullptr;
                }
                int m_file = peek().file_id, m_tok = pos;
                advance();   // _ or ~
                if (!expect(token::Kind::kLParen, "(")) return nullptr;
                if (peek().kind != token::Kind::kRParen) {
                    error("A constructor or destructor takes no parameters.");
                    return nullptr;
                }
                advance();   // )
                if (!expect(token::Kind::kLBrace, "{")) return nullptr;
                if (is_ctor && ctor_def) { errorAt(m_tok, "Duplicate constructor."); return nullptr; }
                if (is_dtor && dtor_def) { errorAt(m_tok, "Duplicate destructor."); return nullptr; }
                auto member = newNodeAt(parse::Kind::kFunctionDef, m_file, m_tok);
                member->name = is_ctor ? "_$gctor" : "_$gdtor";
                member->name_tok = m_tok;
                member->return_type = widen::internOrNone("void");
                if (!parseStmtsThroughRBrace(member->children)) return nullptr;
                if (is_ctor) ctor_def = true; else dtor_def = true;
                node->children.push_back(std::move(member));
            }
            if (!expect(token::Kind::kRBrace, "}")) return nullptr;
            if (ctor_def != dtor_def) {
                // name_tok is set only for a NAMED group; an anon group leaves it -1,
                // so fall back to the `global` keyword tok (always set) — never -1,
                // which would index tokens[-1] and drop the caret sled.
                errorAt(node->name_tok >= 0 ? node->name_tok : node->tok,
                        ctor_def ? "A global constructor requires a matching destructor."
                                 : "A global destructor requires a matching constructor.");
                return nullptr;
            }
            // A hook-bearing group needs a member: the ctor/dtor run on first TOUCH of a
            // member, so a memberless one has no trigger and would silently never run.
            // (ctor_def == dtor_def here, so ctor_def alone means both are present.)
            if (ctor_def) {
                bool has_member = false;
                for (auto const& c : node->children)
                    if (c && c->kind == parse::Kind::kVarDeclStmt) { has_member = true; break; }
                if (!has_member) {
                    errorAt(node->name_tok >= 0 ? node->name_tok : node->tok,
                            "A global group with a constructor/destructor must declare "
                            "at least one member.");
                    return nullptr;
                }
            }
            return node;
        }
        auto d = parseVarDeclStmt();
        if (!d) return nullptr;
        d->is_global = true;
        return d;
    }

    // Build a move (`<--`) or swap (`<-->`) statement from an already-parsed lhs
    // lvalue expression. Positioned at the arrow token; consumes the arrow, the
    // rhs expression, and the trailing `;`. children[0] = lhs, [1] = rhs.
    std::unique_ptr<parse::Node> finishMoveSwap(std::unique_ptr<parse::Node> lhs,
                                                int stmt_file, int stmt_tok) {
        parse::Kind k = (peek().kind == token::Kind::kArrowBoth)
            ? parse::Kind::kSwapStmt : parse::Kind::kMoveStmt;
        advance();   // <-- or <-->
        auto rhs = parseExpr();
        if (!rhs) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto node = newNodeAt(k, stmt_file, stmt_tok);
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs));
        return node;
    }

    // A statement led by a (possibly qualified) name: `name = expr;`,
    // `name op= expr;`, or `name(args);`. The name may be qualified
    // (`Space:foo()`, `Space:kX = 1`) or carry a leading `::`. The single parser
    // for all three forms — qualified and bare alike — so the qualifier fields
    // ride through to resolve identically wherever a name leads a statement.
    std::unique_ptr<parse::Node> parseNameLedStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        std::vector<std::string> segs;
        std::vector<int> toks;
        bool global = false;
        // A `self`-led statement starts from the receiver OBJECT, not a parsed
        // name. `self` is a reserved word (a kIdentExpr named "self"); resolve
        // rewrites it to `_$recv^`. The lvalue/method-call chain below is shared.
        bool is_self = (peek().kind == token::Kind::kSelf);
        std::string name;
        int name_tok = pos;
        if (is_self) {
            name = "self";
            advance();   // self
        } else {
            if (!parseQualifiedName(segs, toks, global)) return nullptr;
            name = segs.back();
            name_tok = toks.back();
            segs.pop_back();
            toks.pop_back();
        }

        auto stamp = [&](parse::Node& n) {
            n.name = std::move(name);
            n.name_tok = name_tok;
            n.qualifier = std::move(segs);
            n.qualifier_toks = std::move(toks);
            n.global_qualified = global;
        };

        // Drive a postfix chain (`[i]`, `.field`, `^`, `^^`) starting from an
        // already-parsed base lvalue/expression, then finish it as a statement: a
        // trailing `.method(args)` -> kMethodCallStmt, `.~()` -> kDtorCallStmt,
        // `<--`/`<-->` -> move/swap, `op=` -> aug-assign, `++`/`--` -> a discarded
        // post-inc kExprStmt, otherwise `= rhs` -> a store (or, for a non-lvalue
        // base such as a `Class(args)` construction temp with no trailing op, a
        // bare kExprStmt — see the construction caller below). Shared by the
        // bare-name lvalue-store entry and the `Class(args).chain` construction
        // entry.
        auto finishLvalueChain =
            [&](std::unique_ptr<parse::Node> lhs,
                bool allow_bare_expr) -> std::unique_ptr<parse::Node> {
            while (peek().kind == token::Kind::kLBracket
                   || peek().kind == token::Kind::kBitXor
                   || peek().kind == token::Kind::kXorXor
                   || peek().kind == token::Kind::kDot
                   || (peek().kind == token::Kind::kLParen
                       && lhs->kind == parse::Kind::kFieldExpr)) {
                if (peek().kind == token::Kind::kLParen) {
                    // `recv.method(args)` — a method call. INSIDE the loop so the
                    // by-value call RESULT keeps chaining (`d.next().get();`): the
                    // method name is the field step, its base is the receiver.
                    advance();   // (
                    auto call = newNodeAt(parse::Kind::kMethodCallStmt,
                                          stmt_file, stmt_tok);
                    call->name = lhs->name;
                    call->name_tok = lhs->name_tok;
                    call->children.push_back(std::move(lhs->children[0]));  // recv
                    if (!parseCallArgs(*call)) return nullptr;     // args -> [1..]
                    lhs = std::move(call);
                    continue;
                }
                if (peek().kind == token::Kind::kLBracket) {
                    lhs = parseSubscript(std::move(lhs));   // comma-aware (transposes)
                    if (!lhs) return nullptr;
                } else if (peek().kind == token::Kind::kDot) {
                    int op_file = peek().file_id;
                    int op_tok = pos;
                    advance();   // .
                    if (peek().kind == token::Kind::kBitNot) {
                        // `.~()` — explicit destructor call (placement cleanup, no
                        // free). A STATEMENT, not an lvalue: the receiver so far is
                        // the object whose dtor runs.
                        advance();   // ~
                        if (!expect(token::Kind::kLParen, "(")) return nullptr;
                        if (!expect(token::Kind::kRParen, ")")) return nullptr;
                        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
                        auto node = newNodeAt(parse::Kind::kDtorCallStmt,
                                              stmt_file, stmt_tok);
                        node->children.push_back(std::move(lhs));
                        return node;
                    }
                    if (peek().kind != token::Kind::kIdentifier) {
                        error("Expected a field name after '.'.");
                        return nullptr;
                    }
                    auto f = newNodeAt(parse::Kind::kFieldExpr, op_file, op_tok);
                    f->children.push_back(std::move(lhs));
                    f->name = peek().text;
                    f->name_tok = pos;
                    advance();   // field name
                    lhs = std::move(f);
                } else {
                    // `^` is one deref, `^^` (one logical-xor token) is two.
                    int levels = peek().kind == token::Kind::kXorXor ? 2 : 1;
                    int op_file = peek().file_id;
                    int op_tok = pos;
                    advance();   // ^ or ^^
                    for (int i = 0; i < levels; i++) {
                        auto d = newNodeAt(parse::Kind::kDerefExpr, op_file, op_tok);
                        d->children.push_back(std::move(lhs));
                        lhs = std::move(d);
                    }
                }
            }
            // The chain ended in a method call (`obj.method(args);` or a chained
            // `d.next().get();`) — a statement on its own. A call result is not an
            // lvalue, so no trailing store/op follows; demand the `;`.
            if (lhs->kind == parse::Kind::kMethodCallStmt) {
                if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
                return lhs;
            }
            if (peek().kind == token::Kind::kArrowLeft
                || peek().kind == token::Kind::kArrowBoth) {
                return finishMoveSwap(std::move(lhs), stmt_file, stmt_tok);
            }
            if (char const* aug = augAssignOp(peek().kind)) {
                // `lvalue op= rhs;` — augmented assign on a complex lvalue. The
                // target rides as children[0] (a chain expr) + the rhs as
                // children[1]; the bare-name form (below) keeps the name on the
                // node with children[0] = rhs instead.
                advance();   // op=
                auto rhs = parseExpr();
                if (!rhs) return nullptr;
                if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
                auto node = newNodeAt(parse::Kind::kAugAssignStmt, stmt_file,
                                      stmt_tok);
                node->text = aug;
                node->children.push_back(std::move(lhs));
                node->children.push_back(std::move(rhs));
                return node;
            }
            if (peek().kind == token::Kind::kPlusPlus
                || peek().kind == token::Kind::kMinusMinus) {
                // `lvalue++;` / `lvalue--;` on a complex lvalue — the same PPID
                // path as the expression form. Build a kPostIncExpr over the chain
                // wrapped in a kExprStmt; desugar lowers it (the value is
                // discarded, so only the bump survives).
                char const* op =
                    peek().kind == token::Kind::kPlusPlus ? "++" : "--";
                int op_tok = pos;
                advance();   // ++ / --
                if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
                auto inc = newNodeAt(parse::Kind::kPostIncExpr, stmt_file, op_tok);
                inc->text = op;
                inc->children.push_back(std::move(lhs));
                auto node = newNodeAt(parse::Kind::kExprStmt, stmt_file, stmt_tok);
                node->children.push_back(std::move(inc));
                return node;
            }
            if (allow_bare_expr && peek().kind == token::Kind::kSemicolon) {
                // `Class(args).field;` — a discarded chain value off a construction
                // temp (no trailing store/op). The construction has no lvalue to
                // assign to, so the whole chain is an expression statement.
                advance();   // ;
                auto node = newNodeAt(parse::Kind::kExprStmt, stmt_file, stmt_tok);
                node->children.push_back(std::move(lhs));
                return node;
            }
            if (!expect(token::Kind::kEquals, "=")) return nullptr;
            auto rhs = parseExpr();
            if (!rhs) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kStoreStmt, stmt_file, stmt_tok);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            return node;
        };

        if (is_self) {
            // `self.field = rhs;` / `self.method();` / `self.field op= rhs;` —
            // drive the lvalue/method-call chain off the receiver object.
            auto lhs = newNodeAt(parse::Kind::kIdentExpr, stmt_file, stmt_tok);
            lhs->name = "self";
            lhs->name_tok = name_tok;
            return finishLvalueChain(std::move(lhs), /*allow_bare_expr=*/false);
        }

        token::Kind next = peek().kind;
        if (next == token::Kind::kBitXor || next == token::Kind::kXorXor
            || next == token::Kind::kLBracket || next == token::Kind::kDot) {
            // Lvalue-expression store: `name[i]... = rhs`, `name.field = rhs`, or
            // `name^ = rhs`. The bare name becomes a kIdentExpr, then the postfix
            // chain (subscripts, field accesses, and derefs, left to right) wraps
            // it into the store target. (A `^` here is unambiguously deref — a
            // trailing operand would be XOR, not a statement.)
            auto lhs = newNodeAt(parse::Kind::kIdentExpr, stmt_file, stmt_tok);
            lhs->name = name;
            lhs->name_tok = name_tok;
            lhs->qualifier = segs;
            lhs->qualifier_toks = toks;
            lhs->global_qualified = global;
            return finishLvalueChain(std::move(lhs), /*allow_bare_expr=*/false);
        }
        if (next == token::Kind::kLParen) {
            advance();   // (
            // Parse the call args once into a temporary kCallExpr. If a postfix
            // continuation follows the `)` (`.method()`, `.field`, `[i]`, `^`), the
            // name is a `Class(args)` construction used as the receiver/base of a
            // chain (nameless temporary form) — drive the chain off it. Otherwise a
            // bare `name(args);` stays a kCallStmt (a function call OR a statement-
            // form construction — resolve decides which).
            auto call = newNodeAt(parse::Kind::kCallExpr, stmt_file, stmt_tok);
            stamp(*call);
            if (!parseCallArgs(*call)) return nullptr;
            token::Kind after = peek().kind;
            if (after == token::Kind::kDot || after == token::Kind::kLBracket
                || after == token::Kind::kBitXor || after == token::Kind::kXorXor) {
                return finishLvalueChain(std::move(call), /*allow_bare_expr=*/true);
            }
            call->kind = parse::Kind::kCallStmt;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            return call;
        }
        if (next == token::Kind::kArrowLeft || next == token::Kind::kArrowBoth) {
            // `name <-- rhs;` / `name <--> rhs;` — move / swap with a bare-name
            // lhs. The lhs is an lvalue EXPRESSION (a kIdentExpr), uniform with
            // the indexed/deref store path, so desugar treats every lhs alike.
            auto lhs = newNodeAt(parse::Kind::kIdentExpr, stmt_file, stmt_tok);
            lhs->name = name;
            lhs->name_tok = name_tok;
            lhs->qualifier = segs;
            lhs->qualifier_toks = toks;
            lhs->global_qualified = global;
            return finishMoveSwap(std::move(lhs), stmt_file, stmt_tok);
        }
        if (next == token::Kind::kEquals) {
            advance();   // =
            auto expr = parseExpr();
            if (!expr) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kAssignStmt, stmt_file, stmt_tok);
            stamp(*node);
            node->children.push_back(std::move(expr));
            return node;
        }
        if (char const* op = augAssignOp(next)) {
            advance();   // op=
            auto expr = parseExpr();
            if (!expr) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kAugAssignStmt, stmt_file, stmt_tok);
            stamp(*node);
            node->text = op;
            node->children.push_back(std::move(expr));
            return node;
        }
        if (next == token::Kind::kSemicolon) {
            // A bare `Name;` statement — a parenless default construction (`Name`
            // == `Name()`) when Name is a class. Build a zero-arg kCallStmt flagged
            // parenless; resolve turns a class into a construction and rejects a
            // non-class (so a bare function/variable name is not silently called).
            advance();   // ;
            auto call = newNodeAt(parse::Kind::kCallStmt, stmt_file, stmt_tok);
            call->name = name;
            call->name_tok = name_tok;
            call->qualifier = segs;
            call->qualifier_toks = toks;
            call->global_qualified = global;
            call->parenless = true;
            stamp(*call);
            return call;
        }
        error("Expected '=' or '(' after a name.");
        return nullptr;
    }

    // Map an augmented-assign token kind to its op string. Returns nullptr
    // for tokens that aren't augmented-assigns.
    static char const* augAssignOp(token::Kind k) {
        if (k == token::Kind::kPlusEq)    return "+";
        if (k == token::Kind::kMinusEq)   return "-";
        if (k == token::Kind::kStarEq)    return "*";
        if (k == token::Kind::kSlashEq)   return "/";
        if (k == token::Kind::kPercentEq) return "%";
        if (k == token::Kind::kBitAndEq)  return "&";
        if (k == token::Kind::kBitOrEq)   return "|";
        if (k == token::Kind::kBitXorEq)  return "^";
        if (k == token::Kind::kLShiftEq)  return "<<";
        if (k == token::Kind::kRShiftEq)  return ">>";
        if (k == token::Kind::kAndEq)     return "&&";
        if (k == token::Kind::kOrEq)      return "||";
        if (k == token::Kind::kXorXorEq)  return "^^";
        return nullptr;
    }

    // Parses arguments into node->children, starting just past the '(' and
    // consuming the closing ')'. Shared by statement-form (parseNameLedStmt) and
    // expression-form (parsePostfix) calls. Returns false on error.
    bool parseCallArgs(parse::Node& node) {
        while (peek().kind != token::Kind::kRParen) {
            auto arg = parseExpr();
            if (!arg) return false;
            node.children.push_back(std::move(arg));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in argument list.");
                return false;
            }
        }
        return expect(token::Kind::kRParen, ")");
    }

    std::unique_ptr<parse::Node> parseDeleteStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // delete
        auto node = newNodeAt(parse::Kind::kDeleteStmt, stmt_file, stmt_tok);
        auto operand = parseExpr();   // the pointer lvalue; resolve checks it
        if (!operand) return nullptr;
        node->children.push_back(std::move(operand));
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    std::unique_ptr<parse::Node> parseReturnStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // return
        auto node = newNodeAt(parse::Kind::kReturnStmt, stmt_file, stmt_tok);
        // Bare `return;` (no value) — valid in a void function; classify rejects
        // it in a non-void one. Otherwise a returned expression.
        if (peek().kind != token::Kind::kSemicolon) {
            auto expr = parseExpr();
            if (!expr) return nullptr;
            node->children.push_back(std::move(expr));
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // A bare inc/dec statement (`++e;` `--e;` `e++;` `e--;`). Build the
    // inc/dec expression and wrap it in an expression-statement; resolve /
    // classify check the operand like any inc/dec, and desugar lowers it to a
    // bump (the discarded value needs no read).
    std::unique_ptr<parse::Node> parseIncDecStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        bool prefix = peek().kind == token::Kind::kPlusPlus
                   || peek().kind == token::Kind::kMinusMinus;
        std::unique_ptr<parse::Node> inc;
        if (prefix) {
            // `++<lvalue>;` — parse the full operand chain via parseUnary, which
            // consumes the leading `++` and builds a kPreIncExpr over a bare ident
            // OR a complex lvalue (`++arr[i];`, `++b.f;`, `++p^;`), identical to
            // the expression form. resolve / classify check the operand.
            inc = parseUnary();
            if (!inc) return nullptr;
        } else {
            // `<ident>++;` — dispatched only for a bare ident followed by ++/--.
            int name_tok = pos;
            std::string name = peek().text;
            advance();   // ident
            token::Kind opk = peek().kind;
            int op_tok = pos;
            advance();   // ++ / --
            auto operand = newNodeAt(parse::Kind::kIdentExpr, stmt_file, name_tok);
            operand->name = std::move(name);
            inc = newNodeAt(parse::Kind::kPostIncExpr, stmt_file, op_tok);
            inc->text = opk == token::Kind::kPlusPlus ? "++" : "--";
            inc->children.push_back(std::move(operand));
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        auto stmt = newNodeAt(parse::Kind::kExprStmt, stmt_file, stmt_tok);
        stmt->children.push_back(std::move(inc));
        return stmt;
    }

    // Two forms:
    //   value:  alias Name = Type;   (return_type = target spelling)
    //   bare:   alias Ns;            (return_type empty; qualifier/global mark the
    //                                 namespace whose members to import unqualified)
    std::unique_ptr<parse::Node> parseAliasDecl() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // alias
        if (peek().kind != token::Kind::kIdentifier
            && peek().kind != token::Kind::kColonColon) {
            error("Expected an alias name after 'alias'.");
            return nullptr;
        }
        std::vector<std::string> segs;
        std::vector<int> toks;
        bool global = false;
        if (!parseQualifiedName(segs, toks, global)) return nullptr;
        auto node = newNodeAt(parse::Kind::kAliasDecl, stmt_file, stmt_tok);
        node->name = segs.back();
        node->name_tok = toks.back();
        segs.pop_back();
        toks.pop_back();
        node->qualifier = std::move(segs);
        node->qualifier_toks = std::move(toks);
        node->global_qualified = global;
        if (peek().kind == token::Kind::kEquals) {
            advance();   // =
            Declarator d;
            if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                 /*allow_qualified=*/false, nullptr, d)) {
                return nullptr;
            }
            node->return_type = widen::internOrNone(d.type);
            // A const-expr dim in the alias TARGET (`alias V = int[N]`): the alias
            // entry's slids_type IS the target, so bakeNodeDims bakes it; constfold's
            // alias-refresh then re-propagates the baked underlying to every use
            // (resolve expanded the alias eagerly, capturing the provisional [1]).
            if (d.any_dim_expr) node->dim_exprs = std::move(d.dim_exprs);
        }
        // else: bare import form — return_type left empty.
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // enum [type] [Name] ( m1 [= expr], m2, ... );
    //   type-first, optional, default int. Name optional: present -> a named
    //   enum (alias + namespace of typed consts); absent -> anonymous (consts
    //   land in the enclosing scope). Each member is a const-shaped decl so it
    //   rides the existing const machinery; resolve fills auto-increment values.
    std::unique_ptr<parse::Node> parseEnumDecl() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // enum
        auto node = newNodeAt(parse::Kind::kEnumDecl, stmt_file, stmt_tok);
        // Optional underlying type. A type precedes the name / `(`; it is a
        // primitive keyword, or an identifier that is NOT immediately the enum
        // name followed by `(` and NOT the `(` itself. Distinguish structurally:
        //   enum (             -> anonymous, default int
        //   enum Name (        -> named, default int
        //   enum type (        -> anonymous, explicit type
        //   enum type Name (   -> named, explicit type
        std::string underlying = "int";
        // An explicit underlying type: point the node's error-attribution token at it
        // (the enum node's tok is read ONLY by registerEnum's underlying-type validation),
        // so "Unknown type" carets the type name, not the `enum` kw. The underlying is a
        // nameless type position — a Forbidden declarator (the enum's bespoke type-vs-name
        // detection stays here; only the type parse funnels).
        // We consume ONLY d.type: any const-EXPRESSION dim (d.dim_exprs) or per-segment
        // tokens are intentionally DROPPED. An array underlying (`enum int[N] X`) is never
        // valid, so registerEnum rejects it regardless — dropping the dim cannot turn an
        // invalid enum into an accepted one (a literal `enum int[3]` likewise still fails
        // there). If enum ever admits an array underlying, revisit: the dim would matter.
        if (isTypeStart(peek().kind)) {
            node->tok = pos;
            Declarator d;
            if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                 /*allow_qualified=*/false, nullptr, d)) {
                return nullptr;
            }
            underlying = std::move(d.type);
            assert(!underlying.empty() && "Forbidden declarator yields a non-empty type");
        } else if (peek().kind == token::Kind::kIdentifier
                   && peekKind(1) == token::Kind::kIdentifier) {
            // `ident ident (` -> first ident is an (identifier) type spelling.
            node->tok = pos;
            Declarator d;
            if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                 /*allow_qualified=*/false, nullptr, d)) {
                return nullptr;
            }
            underlying = std::move(d.type);
            assert(!underlying.empty() && "Forbidden declarator yields a non-empty type");
        }
        node->return_type = widen::internOrNone(underlying);
        // Optional name — possibly QUALIFIED (`Class:Enum`, `A:B:Enum`): the external
        // out-of-line form. Leading segments become the qualifier path; the LAST is the
        // enum name. relocateOutOfLineMembers moves the node into the target scope, where
        // it registers exactly as an in-block `enum int Enum ( … )`.
        if (peek().kind == token::Kind::kIdentifier) {
            node->name = peek().text;
            node->name_tok = pos;
            advance();
            while (peek().kind == token::Kind::kColon
                   && peekKind(1) == token::Kind::kIdentifier) {
                node->qualifier.push_back(node->name);
                node->qualifier_toks.push_back(node->name_tok);
                advance();   // :
                node->name = peek().text;
                node->name_tok = pos;
                advance();
            }
        }
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        // Member list: ident [= expr], comma-separated.
        while (peek().kind != token::Kind::kRParen) {
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected an enum member name.");
                return nullptr;
            }
            auto m = newNodeAt(parse::Kind::kVarDeclStmt, peek().file_id, pos);
            m->name = peek().text;
            m->name_tok = pos;
            m->return_type = widen::internOrNone(underlying);
            m->is_const = true;
            advance();
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto init = parseExpr();
                if (!init) return nullptr;
                m->children.push_back(std::move(init));
            }
            node->children.push_back(std::move(m));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in enum member list.");
                return nullptr;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // The shared definition-member dispatch for BOTH namespace and class bodies —
    // the universal vocabulary: const, alias, enum, nested class, nested namespace,
    // and a function. The function arm is the only context-varying part: a free
    // function in a namespace, a receiver-bound method in a class (the implicit
    // `_$recv` of type `recv_type` is spliced in when in_class). ctor/dtor are
    // method-shaped and class-only; the class body loop peels them off BEFORE
    // calling here, and parseNamespaceMember rejects them. Returns nullptr on error.
    std::unique_ptr<parse::Node> parseDefinitionMember(bool in_class,
                                                       std::string const& recv_type) {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kConst) return parseVarDeclStmt();
        if (t.kind == token::Kind::kAlias) return parseAliasDecl();
        if (t.kind == token::Kind::kEnum)  return parseEnumDecl();
        if (t.kind == token::Kind::kGlobal) return parseGlobal();
        // A qualified external scope def — `Class:Namespace { }` (a namespace member
        // of the class) or `Class:Reopen() { }` (an external re-open of a hoisted
        // class, empty parens). Checked before looksLikeClassDef, which would grab
        // the empty-parens form as an (empty-field) derived class. Resolve relocates
        // the node into its target scope's children.
        if (looksLikeQualifiedScopeDef()) return parseQualifiedScopeDef();
        if (looksLikeClassDef())           return parseClassDef();   // nested class
        if (t.kind == token::Kind::kIdentifier
            && peekKind(1) == token::Kind::kLBrace) return parseNamespaceDecl();
        if (in_class) {
            // A class body: a method is a function-shaped member (receiver-injected).
            // looksLikeFunctionDef gates here — anything not function-shaped is a
            // naked statement and rejected. (A class has no forward-decl members; the
            // ctor/dtor `_();`/`~();` decls are handled separately by the class loop.)
            if (!looksLikeFunctionDef()) {
                error("A class body holds the constructor '_()', the destructor "
                      "'~()', member definitions (aliases, constants, enums, "
                      "classes, namespaces), and methods.");
                return nullptr;
            }
            auto m = parseFunctionDef();
            if (!m) return nullptr;
            // A QUALIFIED method (`int Sib:go()`) is an EXTERNAL def targeting ANOTHER
            // class — relocateOutOfLineMembers splices the receiver for its TARGET, so
            // this class's receiver must NOT be added here (else a doubled `_$recv`).
            // Only a plain same-class method gets this class's receiver.
            if (m->qualifier.empty()) {
                m->params.insert(m->params.begin(),
                    parse::makeReceiverParam(widen::internOrNone(recv_type),
                                             m->file_id, m->name_tok));
            }
            return m;
        }
        // A bare var-decl (no `global` keyword) at namespace / file scope IS a global
        // — the short form `int x = 0;` desugars to `global int x = 0;`. Detected
        // before the function fallback and only for the DECLARATION shape (name not
        // followed by `(`), so function defs and `name();` forward decls fall through.
        if (looksLikeBareVarDecl()) {
            auto d = parseVarDeclStmt();
            if (!d) return nullptr;
            d->is_global = true;
            return d;
        }
        // A namespace / file-scope body has no call statements, so anything not
        // matched above is a function DEFINITION or DECLARATION — parseFunctionDef
        // parses both and diagnoses a malformed one. looksLikeFunctionDef must NOT
        // gate here: it is a statement-context disambiguator that wrongly rejects a
        // `name();` forward declaration (reading it as a construction call).
        return parseFunctionDef();
    }

    // A namespace member: the universal definition vocabulary, minus ctor/dtor
    // (method-shaped, legal only in a class body).
    std::unique_ptr<parse::Node> parseNamespaceMember() {
        if (peek().kind == token::Kind::kBitNot
            || (peek().kind == token::Kind::kIdentifier && peek().text == "_"
                && peekKind(1) == token::Kind::kLParen)) {
            error("A constructor or destructor may only appear in a class body.");
            return nullptr;
        }
        return parseDefinitionMember(/*in_class=*/false, /*recv_type=*/"");
    }

    // Name { members } — open or reopen a namespace. No trailing semicolon.
    std::unique_ptr<parse::Node> parseNamespaceDecl() {
        int ns_file = peek().file_id;
        int ns_tok = pos;
        std::string name = peek().text;
        int name_tok = pos;
        advance();   // name
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        auto node = newNodeAt(parse::Kind::kNamespaceDecl, ns_file, ns_tok);
        node->name = std::move(name);
        node->name_tok = name_tok;
        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
                return nullptr;
            }
            auto m = parseNamespaceMember();
            if (!m) return nullptr;
            node->children.push_back(std::move(m));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    // A qualified external scope def — the abbreviated re-open form for a namespace
    // or hoisted class. Consume the qualifier prefix `A:B:…:` (all but the final
    // segment), leaving the final segment for parseNamespaceDecl / parseClassDef to
    // parse as it normally would, then hang the qualifier path on the result. Resolve
    // relocates the node under the scope named by the qualifier. Gated by
    // looksLikeQualifiedScopeDef, so the shape is already known valid.
    std::unique_ptr<parse::Node> parseQualifiedScopeDef() {
        std::vector<std::string> qualifier;
        std::vector<int> qualifier_toks;
        // Count segments so we can stop one short of the final one.
        int nsegs = 1, o = 1;
        while (peekKind(o) == token::Kind::kColon
               && peekKind(o + 1) == token::Kind::kIdentifier) { nsegs++; o += 2; }
        for (int i = 0; i < nsegs - 1; i++) {
            qualifier.push_back(peek().text);
            qualifier_toks.push_back(pos);
            advance();   // segment identifier
            advance();   // :
        }
        // `pos` is now at the final segment; `{` -> namespace, `(` -> class re-open.
        std::unique_ptr<parse::Node> node =
            (peekKind(1) == token::Kind::kLBrace) ? parseNamespaceDecl()
                                                  : parseClassDef();
        if (!node) return nullptr;
        node->qualifier = std::move(qualifier);
        node->qualifier_toks = std::move(qualifier_toks);
        return node;
    }

    // The shared statement-body loop: parse statements up to and including the
    // closing `}`. The opening `{` must ALREADY be consumed by the caller (each
    // call site distinguishes `{` from `;`/custom errors before reaching here).
    // Appends each statement to `out`; returns false on error (already reported).
    bool parseStmtsThroughRBrace(std::vector<std::unique_ptr<parse::Node>>& out) {
        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
                return false;
            }
            auto stmt = parseStmt();
            if (!stmt) return false;
            out.push_back(std::move(stmt));
        }
        return expect(token::Kind::kRBrace, "}");
    }

    // { stmts } — a nested lexical scope. A bare `{` (no leading ident) is a
    // block; `ident {` is a namespace decl, dispatched separately in parseStmt.
    std::unique_ptr<parse::Node> parseBlock() {
        int blk_file = peek().file_id;
        int blk_tok = pos;
        advance();   // {
        auto node = newNodeAt(parse::Kind::kBlockStmt, blk_file, blk_tok);
        if (!parseStmtsThroughRBrace(node->children)) return nullptr;
        return node;
    }

    // if ( cond ) { then } [ else { else } | else if ... ]. Both branches are
    // blocks (a nested scope, like every flow body); `else if` recurses so the
    // else-branch is another kIfStmt. children[0] = condition, [1] = then-block,
    // [2] = optional else-branch.
    std::unique_ptr<parse::Node> parseIfStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // if
        auto cond = parseParenCondition();   // empty `()` -> always-true literal
        if (!cond) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' after if condition.");
            return nullptr;
        }
        auto then_blk = parseBlock();
        if (!then_blk) return nullptr;
        auto node = newNodeAt(parse::Kind::kIfStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(then_blk));
        if (peek().kind == token::Kind::kElse) {
            advance();   // else
            std::unique_ptr<parse::Node> else_branch;
            if (peek().kind == token::Kind::kIf) {
                else_branch = parseIfStmt();
            } else if (peek().kind == token::Kind::kLBrace) {
                else_branch = parseBlock();
            } else {
                error("Expected '{' or 'if' after 'else'.");
                return nullptr;
            }
            if (!else_branch) return nullptr;
            node->children.push_back(std::move(else_branch));
        }
        return node;
    }

    // Parse `( [cond] )`. An empty condition is the always-true literal (a slids
    // convention); numeric already ran, so the post-numeric canonical text "1"
    // is synthesized directly. Returns the condition node, nullptr on error.
    std::unique_ptr<parse::Node> parseParenCondition() {
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        std::unique_ptr<parse::Node> cond;
        if (peek().kind == token::Kind::kRParen) {
            cond = newNodeHere(parse::Kind::kBoolLiteral);
            cond->text = "1";
        } else {
            cond = parseExpr();
            if (!cond) return nullptr;
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        return cond;
    }

    // Pre-condition  `while ( cond ) { body }`  -> kWhileStmt.
    // A loop's optional `:label` after its body (labels go on loops only this
    // round — never a switch). Returns the label name, or "" if none. The
    // label name is a plain identifier (the for/while keyword DEFAULT names are
    // only for referencing in break/continue, not for declaring).
    std::string parseOptionalLabel() {
        if (peek().kind != token::Kind::kColon) return "";
        advance();   // :
        if (peek().kind != token::Kind::kIdentifier) {
            error("Expected a label name after ':'.");
            return "";
        }
        std::string name = peek().text;
        advance();   // name
        return name;
    }

    // Post-condition `while { body } ( cond ) ;` -> kDoWhileStmt (body runs once).
    // Dispatched on the token after `while`: '{' opens the post-condition body,
    // anything else begins the pre-condition '(' clause. Both nodes store
    // children[0] = condition, [1] = body-block (a nested scope).
    std::unique_ptr<parse::Node> parseWhileStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // while
        if (peek().kind == token::Kind::kLBrace) {
            auto body = parseBlock();
            if (!body) return nullptr;
            std::string label = parseOptionalLabel();   // between `}` and `(cond)`
            if (fatal) return nullptr;
            auto cond = parseParenCondition();
            if (!cond) return nullptr;
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            auto node = newNodeAt(parse::Kind::kDoWhileStmt, stmt_file, stmt_tok);
            node->children.push_back(std::move(cond));
            node->children.push_back(std::move(body));
            node->label = std::move(label);
            return node;
        }
        auto cond = parseParenCondition();
        if (!cond) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' after while condition.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        auto node = newNodeAt(parse::Kind::kWhileStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(body));
        node->label = parseOptionalLabel();              // after the body `}`
        if (fatal) return nullptr;
        if (!node->label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        return node;
    }

    // Build a long-for's `( cond ) { update } { body }` tail and assemble the
    // node from an already-parsed varlist. children[0]=cond, [1]=update-block,
    // [2]=body-block, [3..]=varlist decls.
    std::unique_ptr<parse::Node> finishLongFor(int stmt_file, int stmt_tok,
            std::vector<std::unique_ptr<parse::Node>> varlist) {
        auto cond = parseParenCondition();   // `( cond )`, empty -> true
        if (!cond) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop update clause.");
            return nullptr;
        }
        auto update = parseBlock();
        if (!update) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        auto node = newNodeAt(parse::Kind::kForLongStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(cond));
        node->children.push_back(std::move(update));
        node->children.push_back(std::move(body));
        for (auto& d : varlist) node->children.push_back(std::move(d));
        node->label = parseOptionalLabel();              // after the body `}`
        if (fatal) return nullptr;
        if (!node->label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        return node;
    }

    // Ranged-for: `for (type var : start .. [cmp] end [op step]) {body}`.
    // Builds a kForRangedStmt holding the raw clauses; resolve/constfold/classify
    // understand it in scope and desugar lowers it to the canonical kForLongStmt
    // (synthesizing the `_$end`/`_$step` bound/step locals there, with fresh ids).
    // The `..` token rides along for the empty-range "Invalid range." check.
    std::unique_ptr<parse::Node> parseRangeFor(int stmt_file, int stmt_tok,
            std::string vtype, std::string vname, int v_file, int v_tok,
            int vname_tok, std::vector<std::unique_ptr<parse::Node>> v_dims,
            std::unique_ptr<parse::Node> start) {
        // `start` is the operand before `..`, already parsed by the caller.
        int dotdot_tok = pos;
        advance();   // ..
        std::string cmp = "<";
        token::Kind ck = peek().kind;
        if      (ck == token::Kind::kLt)    { cmp = "<";  advance(); }
        else if (ck == token::Kind::kLtEq)  { cmp = "<="; advance(); }
        else if (ck == token::Kind::kGt)    { cmp = ">";  advance(); }
        else if (ck == token::Kind::kGtEq)  { cmp = ">="; advance(); }
        else if (ck == token::Kind::kNotEq) { cmp = "!="; advance(); }
        else if (ck == token::Kind::kEqEq)  {
            // `==` is not a range comparator (a range is half-open / ordered).
            error("Invalid range comparator; use '<', '<=', '>', '>=', or '!='.");
            return nullptr;
        }
        auto end = parseUnary();
        if (!end) return nullptr;
        std::string op;
        token::Kind ok = peek().kind;
        if      (ok == token::Kind::kPlus)   op = "+";
        else if (ok == token::Kind::kMinus)  op = "-";
        else if (ok == token::Kind::kStar)   op = "*";
        else if (ok == token::Kind::kSlash)  op = "/";
        else if (ok == token::Kind::kLShift) op = "<<";
        else if (ok == token::Kind::kRShift) op = ">>";
        // A recognized-but-unsupported operator in the step position (`%`, `&`,
        // `|`, `^`, `&&`, `||`, `^^`) is rejected clearly rather than left to a
        // misleading "Expected ')'". (A modulus etc. INSIDE a parenthesized bound
        // is fine — parseUnary consumed it, so this only sees a BARE operator.)
        else if (ok == token::Kind::kPercent || ok == token::Kind::kBitAnd
              || ok == token::Kind::kBitOr   || ok == token::Kind::kBitXor
              || ok == token::Kind::kAnd     || ok == token::Kind::kOr
              || ok == token::Kind::kXorXor) {
            error("Invalid range step operator; use '+', '-', '*', '/', "
                  "'<<', or '>>'.");
            return nullptr;
        }
        std::unique_ptr<parse::Node> step;
        if (!op.empty()) {
            advance();   // op
            step = parseUnary();
            if (!step) return nullptr;
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        std::string label = parseOptionalLabel();        // after the body `}`
        if (fatal) return nullptr;
        if (!label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }

        // Short form: the loop-var decl (init = start), then the raw end / step
        // clauses. The `_$end`/`_$step` bound and step locals are NOT synthesized
        // here — desugar mints them with fresh ids while lowering to kForLongStmt,
        // so the old name-reuse `_$end` clobber can't arise. A missing step op
        // defaults to `+` (a +1 step); a null step child carries that default.
        auto vd = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
        vd->name = vname; vd->name_tok = vname_tok;
        vd->return_type = widen::internOrNone(vtype);
        { bool any = false; for (auto& d : v_dims) if (d) any = true;
          if (any) vd->dim_exprs = std::move(v_dims); }
        vd->children.push_back(std::move(start));

        auto node = newNodeAt(parse::Kind::kForRangedStmt, stmt_file, stmt_tok);
        node->text = cmp;                          // comparison operator
        node->name = op.empty() ? "+" : op;        // step operator (default +)
        node->children.push_back(std::move(vd));   // [0] loop-var decl (init=start)
        node->children.push_back(std::move(end));  // [1] end expr
        node->children.push_back(std::move(step)); // [2] step expr (null => +1)
        node->children.push_back(std::move(body)); // [3] body block
        node->range_dotdot_tok = dotdot_tok;
        node->label = std::move(label);
        return node;
    }

    // for-enum: `for (var : Enum) {body}`. `enum_ref` is the operand after ':'
    // (must be a bare/qualified identifier — the enum name). Builds a kForEnumStmt
    // {loop-var decl, enum-ref, body}; resolve lowers it once the enum is known.
    std::unique_ptr<parse::Node> parseEnumFor(int stmt_file, int stmt_tok,
            std::string vtype, std::string vname, int v_file, int v_tok,
            int vname_tok, std::vector<std::unique_ptr<parse::Node>> v_dims,
            std::unique_ptr<parse::Node> enum_ref) {
        // The operand after ':' is any expression (parseUnary already parsed it):
        // an enum NAME, an array/tuple VARIABLE, a tuple LITERAL, or any other
        // expression resolving to a homogeneous tuple (a deref `ref^`, an index,
        // a function call, ...). resolve dispatches on its resolved TYPE and
        // rejects a non-iterable there.
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (peek().kind != token::Kind::kLBrace) {
            error("Expected '{' for the for-loop body.");
            return nullptr;
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        std::string label = parseOptionalLabel();        // after the body `}`
        if (fatal) return nullptr;
        if (!label.empty() && !expect(token::Kind::kSemicolon, ";")) {
            return nullptr;
        }
        auto vd = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
        vd->name = vname; vd->name_tok = vname_tok; vd->return_type = widen::internOrNone(vtype);
        { bool any = false; for (auto& d : v_dims) if (d) any = true;
          if (any) vd->dim_exprs = std::move(v_dims); }
        auto node = newNodeAt(parse::Kind::kForEnumStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(vd));        // [0] loop-var decl
        node->children.push_back(std::move(enum_ref));  // [1] enum-ref
        node->children.push_back(std::move(body));      // [2] body
        node->label = std::move(label);
        return node;
    }

    // for — dispatches between the colon (ranged/enum) form and the long form on
    // the token after the first `[type] var`: ':' opens a range or enum,
    // anything else makes that decl varlist[0] of a long-for. An empty `()` is an
    // empty-varlist long form. A var type is optional (parseForVarHead); a
    // typeless var is inferred from its initializer or reuses an enclosing local.
    std::unique_ptr<parse::Node> parseForStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // for
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (peek().kind == token::Kind::kRParen) {
            advance();   // )  — empty varlist long-for
            return finishLongFor(stmt_file, stmt_tok, {});
        }
        // First decl's `[type] name` (type optional — typeless infers / reuses).
        int v_file, v_tok, vname_tok;
        std::string vtype, vname;
        std::vector<std::unique_ptr<parse::Node>> v_dims;
        if (!parseForVarHead(vtype, vname, v_file, v_tok, vname_tok, v_dims)) {
            return nullptr;
        }
        if (peek().kind == token::Kind::kColon) {
            advance();   // :
            auto operand = parseUnary();   // start (range) or the enum name
            if (!operand) return nullptr;
            if (peek().kind == token::Kind::kDotDot) {
                return parseRangeFor(stmt_file, stmt_tok, std::move(vtype),
                                     std::move(vname), v_file, v_tok, vname_tok,
                                     std::move(v_dims), std::move(operand));
            }
            return parseEnumFor(stmt_file, stmt_tok, std::move(vtype),
                                std::move(vname), v_file, v_tok, vname_tok,
                                std::move(v_dims), std::move(operand));
        }
        // Long form: varlist[0] is the decl just parsed; gather any more.
        std::vector<std::unique_ptr<parse::Node>> varlist;
        while (true) {
            auto decl = newNodeAt(parse::Kind::kVarDeclStmt, v_file, v_tok);
            decl->name = vname;
            decl->name_tok = vname_tok;
            decl->return_type = widen::internOrNone(vtype);
            { bool any = false; for (auto& d : v_dims) if (d) any = true;
              if (any) decl->dim_exprs = std::move(v_dims); }
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto init = parseExpr();
                if (!init) return nullptr;
                decl->children.push_back(std::move(init));
            } else if (peek().kind == token::Kind::kLParen) {
                // `Type name(args)` construction form — modeled as a kTupleExpr
                // init, exactly like parseVarDeclStmt, so it converges on the same
                // class-construction normalization as `= (tuple)`.
                int t_file = peek().file_id;
                int t_tok = pos;
                advance();   // (
                auto tup = newNodeAt(parse::Kind::kTupleExpr, t_file, t_tok);
                if (!parseCallArgs(*tup)) return nullptr;
                decl->children.push_back(std::move(tup));
            }
            varlist.push_back(std::move(decl));
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
            if (!parseForVarHead(vtype, vname, v_file, v_tok, vname_tok, v_dims)) {
                return nullptr;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        return finishLongFor(stmt_file, stmt_tok, std::move(varlist));
    }

    // break / continue, with an optional argument: an integer (the Nth enclosing
    // loop, stored in `text`) or a name (a loop label, stored in `name` — incl.
    // the `for` / `while` keyword default names). No argument = naked.
    std::unique_ptr<parse::Node> parseBreakContinue(parse::Kind kind) {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // break / continue
        auto node = newNodeAt(kind, stmt_file, stmt_tok);
        token::Kind k = peek().kind;
        if (k == token::Kind::kIntLiteral) {
            node->text = peek().text;        // numbered
            node->name_tok = pos;            // the count token (for the caret)
            advance();
        } else if (k == token::Kind::kIdentifier
                   || k == token::Kind::kFor || k == token::Kind::kWhile) {
            node->name = peek().text;        // named (label or for/while default)
            node->name_tok = pos;
            advance();
        }
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // One switch clause: a label-list, a mandatory `{ }` body block, and an
    // optional trailing `continue;`. children = [label0, .., labelK, body]: each
    // label is a const-expr (nullptr => a `default` label); the LAST child is the
    // body kBlockStmt. text == "continue" marks a trailing fall-through into the
    // next clause. A label-list (`1: 2: 3: { }`) shares one body. There is no
    // implicit fall-through — a clause exits the switch at its body's `}` unless
    // the trailing `continue` carries it into the next clause.
    std::unique_ptr<parse::Node> parseCaseClause() {
        int clause_file = peek().file_id;
        int clause_tok = pos;
        auto clause = newNodeAt(parse::Kind::kCaseClause, clause_file, clause_tok);
        // Label list: one or more `(const-expr | default) :`, up to the body `{`.
        for (;;) {
            std::unique_ptr<parse::Node> label;
            if (peek().kind == token::Kind::kDefault) {
                advance();   // default — label stays null
            } else {
                case_label_ = true;
                label = parseExpr();
                case_label_ = false;
                if (!label) return nullptr;
            }
            if (!expect(token::Kind::kColon, ":")) return nullptr;
            clause->children.push_back(std::move(label));
            if (peek().kind == token::Kind::kLBrace) break;   // body follows
            // Otherwise another label must follow; a token that cannot begin a
            // label means the mandatory `{ }` body is missing.
            token::Kind nk = peek().kind;
            bool starts_label =
                nk == token::Kind::kDefault
                || nk == token::Kind::kIntLiteral || nk == token::Kind::kUintLiteral
                || nk == token::Kind::kCharLiteral || nk == token::Kind::kFloatLiteral
                || nk == token::Kind::kBoolLiteral || nk == token::Kind::kStringLiteral
                || nk == token::Kind::kNullptr
                || nk == token::Kind::kIdentifier || nk == token::Kind::kColonColon
                || nk == token::Kind::kLParen
                || nk == token::Kind::kPlus || nk == token::Kind::kMinus
                || nk == token::Kind::kNot  || nk == token::Kind::kBitNot;
            if (!starts_label) {
                error("Expected '{' to open the clause body.");
                return nullptr;
            }
            // Another label in the list — loop.
        }
        auto body = parseBlock();
        if (!body) return nullptr;
        clause->children.push_back(std::move(body));   // last child = body block
        if (peek().kind == token::Kind::kContinue) {
            advance();   // continue — fall through to the next clause
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            clause->text = "continue";
        }
        return clause;
    }

    // switch ( value ) { clause* } — value required; clauses are case/default.
    std::unique_ptr<parse::Node> parseSwitchStmt() {
        int stmt_file = peek().file_id;
        int stmt_tok = pos;
        advance();   // switch
        if (!expect(token::Kind::kLParen, "(")) return nullptr;
        if (peek().kind == token::Kind::kRParen) {
            error("A switch value is required.");
            return nullptr;
        }
        auto scrutinee = parseExpr();
        if (!scrutinee) return nullptr;
        if (!expect(token::Kind::kRParen, ")")) return nullptr;
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;
        auto node = newNodeAt(parse::Kind::kSwitchStmt, stmt_file, stmt_tok);
        node->children.push_back(std::move(scrutinee));   // [0]
        while (peek().kind != token::Kind::kRBrace
               && peek().kind != token::Kind::kEndOfFile
               && peek().kind != token::Kind::kEndOfInput) {
            auto clause = parseCaseClause();
            if (!clause) return nullptr;
            node->children.push_back(std::move(clause));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        return node;
    }

    std::unique_ptr<parse::Node> parseStmt() {
        token::Token const& t = peek();
        if (t.kind == token::Kind::kLBrace) return parseBlock();
        if (t.kind == token::Kind::kIf) return parseIfStmt();
        if (t.kind == token::Kind::kWhile) return parseWhileStmt();
        if (t.kind == token::Kind::kFor) return parseForStmt();
        if (t.kind == token::Kind::kSwitch) return parseSwitchStmt();
        if (t.kind == token::Kind::kBreak)
            return parseBreakContinue(parse::Kind::kBreakStmt);
        if (t.kind == token::Kind::kContinue)
            return parseBreakContinue(parse::Kind::kContinueStmt);
        if (t.kind == token::Kind::kReturn) return parseReturnStmt();
        if (t.kind == token::Kind::kDelete) return parseDeleteStmt();
        if (t.kind == token::Kind::kConst) return parseVarDeclStmt();
        if (t.kind == token::Kind::kAlias) return parseAliasDecl();
        if (t.kind == token::Kind::kEnum) return parseEnumDecl();
        if (t.kind == token::Kind::kGlobal) return parseGlobalStmt();
        // An external qualified SCOPE def — `C:Ns { }` (a namespace member) or
        // `C:R() { }` (a hoisted-class re-open, EMPTY parens) — of a class declared in
        // THIS scope, out of line. Checked before the function / class / name-led
        // dispatches (its `ident:` lead would otherwise route to a qualified var-decl
        // or a call); resolve relocates it into the target.
        if (looksLikeQualifiedScopeDef()) return parseQualifiedScopeDef();
        // A nested function definition (`type name (params) {body}`) — checked
        // before the var-decl / name-led dispatches it would otherwise hit.
        if (looksLikeFunctionDef()) return parseFunctionDef();
        // A local class definition (`Name(fields) {body}`, no return type) — the
        // body `{` after the matching `)` distinguishes it from a call
        // (`name(args);`, which ends in `;`). Checked after the function-def look
        // (a function has a leading return type, so the shapes don't overlap).
        if (looksLikeClassDef()) return parseClassDef();
        if (isTypeStart(t.kind)) return parseVarDeclStmt();
        if (t.kind == token::Kind::kPlusPlus
            || t.kind == token::Kind::kMinusMinus) return parseIncDecStmt();
        // A `self`-led statement (`self.field = rhs;`, `self.method();`) — drive
        // the same name-led lvalue/method-call path off the receiver object.
        if (t.kind == token::Kind::kSelf) return parseNameLedStmt();
        if (t.kind == token::Kind::kIdentifier) {
            token::Kind next = peekKind(1);
            if (next == token::Kind::kLBrace) return parseNamespaceDecl();
            if (next == token::Kind::kPlusPlus
                || next == token::Kind::kMinusMinus) return parseIncDecStmt();
            // `<ident> <ident>` is a declaration with an identifier type
            // (alias / class / enum), e.g. `Integer x = 42;`.
            if (next == token::Kind::kIdentifier) return parseVarDeclStmt();
            // A qualified (`Space:Dir x`) or pointer-suffixed (`Integer^ ref`,
            // `Integer[] iter`) identifier-typed decl. looksLikeQualifiedTypedDecl
            // accepts an optional `^` / `[]` suffix before the var name.
            if ((next == token::Kind::kColon
                 || next == token::Kind::kBitXor
                 || next == token::Kind::kLBracket)
                && looksLikeQualifiedTypedDecl()) return parseVarDeclStmt();
            // ident, `ident:...` (qualified) -> assign / aug-assign / call.
            return parseNameLedStmt();
        }
        // A leading `::` is a global-qualified name: a typed decl (`::A:T x`) or
        // a name leading a call / assign.
        if (t.kind == token::Kind::kColonColon) {
            if (looksLikeQualifiedTypedDecl()) return parseVarDeclStmt();
            return parseNameLedStmt();
        }
        // A leading `(...)` is a tuple-typed var decl when a name follows the
        // `)` (`(Dir, bool) pair = ...`), or a destructure when `=` follows
        // (`(a, b) = tuple`); otherwise it's an expression statement.
        if (t.kind == token::Kind::kLParen) {
            if (looksLikeTupleTypeDecl()) return parseVarDeclStmt();
            if (looksLikeTupleDestructure()) return parseDestructureStmt();
        }
        error("Expected statement.");
        return nullptr;
    }

    // `(a, b, ) = tuple;` — a tuple destructure. children[0] = rhs tuple expr;
    // [1..] = target lvalues in slot order, a NULL child for an empty/skipped
    // slot. Targets are parsed first (syntactic order) into [1..], then the rhs
    // is filled into [0].
    // Parse `( slot, ... )` (the leading `(` consumed here) and append the slots to
    // `node->children` (after children[0], the rhs placeholder). A slot is a discard (a
    // bare `,`, OR a TYPED-no-name slot `int` — both drop their tuple position, recorded
    // as a NULL child), a binding DECLARATOR (`[type] name` -> a kVarDeclStmt; `int x`
    // declares, a bare `x` reuses-or-declares per resolve), or a NESTED `( ... )`
    // destructure (recurse — a kDestructureStmt slot with no rhs of its own). Slots are
    // BindSlot declarators: type optional (a bare name infers), name optional (no name +
    // a type is the discard).
    bool parseDestructureSlots(parse::Node* node) {
        advance();   // (
        while (true) {
            if (peek().kind == token::Kind::kComma
                || peek().kind == token::Kind::kRParen) {
                node->children.push_back(nullptr);   // empty discard slot
            } else if (peek().kind == token::Kind::kLParen) {
                auto sub = newNodeAt(parse::Kind::kDestructureStmt, peek().file_id, pos);
                sub->children.push_back(nullptr);    // [0] = rhs (none for a nested slot)
                if (!parseDestructureSlots(sub.get())) return false;
                node->children.push_back(std::move(sub));
            } else {
                Declarator d;
                if (!parseDeclarator(NamePolicy::BindSlot, /*parse_name_dims=*/false,
                                     /*allow_qualified=*/false, nullptr, d)) {
                    return false;
                }
                if (!d.name.empty()) {
                    auto slot = newNodeAt(parse::Kind::kVarDeclStmt, d.file_id, d.tok);
                    slot->name = std::move(d.name);
                    slot->name_tok = d.name_tok;
                    slot->return_type = widen::internOrNone(d.type);
                    node->children.push_back(std::move(slot));
                } else if (!d.typeless) {
                    // A TYPED slot with NO name (`int`) drops its position — a documented
                    // discard, recorded as a null child exactly like an empty slot. The
                    // spelled type is NOT validated against the dropped tuple slot (it is
                    // documentation only); classify/resolve skip a null child entirely.
                    assert(!d.type.empty() && "a typed-no-name slot has a type spelling");
                    node->children.push_back(nullptr);
                } else {
                    error("Expected a destructure target name or type.");
                    return false;
                }
            }
            if (peek().kind != token::Kind::kComma) break;
            advance();   // ,
        }
        return expect(token::Kind::kRParen, ")");
    }

    std::unique_ptr<parse::Node> parseDestructureStmt() {
        int lp = pos;
        auto node = newNodeAt(parse::Kind::kDestructureStmt, peek().file_id, lp);
        node->children.push_back(nullptr);   // [0] = rhs, filled below
        if (!parseDestructureSlots(node.get())) return nullptr;
        // `=` copy, `<--` per-slot move, `<-->` per-slot swap. classify desugars the
        // move/swap forms into per-slot kMoveStmt/kSwapStmt against the source.
        if (peek().kind == token::Kind::kArrowLeft) {
            node->default_move_init = true; advance();
        } else if (peek().kind == token::Kind::kArrowBoth) {
            node->default_swap_init = true; advance();
        } else if (!expect(token::Kind::kEquals, "=")) {
            return nullptr;
        }
        auto rhs = parseExpr();
        if (!rhs) return nullptr;
        node->children[0] = std::move(rhs);
        if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
        return node;
    }

    // Pure lookahead: `Name ( ... ) {` with no leading return type — a class
    // definition in statement position. The body `{` after the matching `)` is
    // what separates it from a call `name(...);` (which ends in `;`). Consumes
    // nothing.
    bool looksLikeClassDef() const {
        if (peekKind(0) != token::Kind::kIdentifier) return false;
        // A derived class: `Base : Derived(field-list) { body }` — the field list
        // starts after `Base : Derived` (offset 3). A non-derived class starts at 1.
        int start = 1;
        if (peekKind(1) == token::Kind::kColon
            && peekKind(2) == token::Kind::kIdentifier) {
            start = 3;
        }
        if (peekKind(start) != token::Kind::kLParen) return false;
        int depth = 0;
        for (int i = start; ; i++) {
            token::Kind k = peekKind(i);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput)
                return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen) {
                if (--depth == 0)
                    return peekKind(i + 1) == token::Kind::kLBrace;
            }
        }
    }

    // Pure lookahead: does a bare-identifier head form a QUALIFIED external scope
    // def — `A:B:…:X { }` (a namespace member of the qualified scope) or
    // `A:B:…:X() { }` (an external re-open of a hoisted class, EMPTY parens)? At
    // least one qualifier segment is required (`A:X`), which distinguishes it from
    // a plain namespace (`A { }`, no colon). A non-empty param list (`A:X(fields)`)
    // is NOT matched — that is inheritance (`Base:Derived(fields)`), left to
    // looksLikeClassDef. Consumes nothing.
    bool looksLikeQualifiedScopeDef() const {
        if (peekKind(0) != token::Kind::kIdentifier) return false;
        int o = 1;
        if (peekKind(o) != token::Kind::kColon
            || peekKind(o + 1) != token::Kind::kIdentifier) return false;
        while (peekKind(o) == token::Kind::kColon
               && peekKind(o + 1) == token::Kind::kIdentifier) o += 2;
        // `o` now indexes the token after the final segment.
        if (peekKind(o) == token::Kind::kLBrace) return true;           // namespace
        return peekKind(o) == token::Kind::kLParen                       // re-open
            && peekKind(o + 1) == token::Kind::kRParen
            && peekKind(o + 2) == token::Kind::kLBrace;
    }

    // Pure lookahead: do the tokens form `<return-type> <name> (` — a (nested)
    // function definition — as opposed to a var decl (`type name = / ;`) or a
    // call (`name(...)`, no leading type)? Consumes nothing.
    // The operator tokens that may follow `op` to form an operator-method name.
    // The two-token `[]` (index) is recognized separately by its bracket shape.
    static bool isOperatorSymbolKind(token::Kind k) {
        return k == token::Kind::kEquals
            || k == token::Kind::kArrowLeft   || k == token::Kind::kArrowBoth
            || k == token::Kind::kPlus         || k == token::Kind::kMinus
            || k == token::Kind::kStar         || k == token::Kind::kSlash
            || k == token::Kind::kPercent
            || k == token::Kind::kBitAnd       || k == token::Kind::kBitOr
            || k == token::Kind::kBitXor       || k == token::Kind::kBitNot
            || k == token::Kind::kLShift       || k == token::Kind::kRShift
            || k == token::Kind::kAnd          || k == token::Kind::kOr
            || k == token::Kind::kNot          || k == token::Kind::kXorXor
            || k == token::Kind::kEqEq         || k == token::Kind::kNotEq
            || k == token::Kind::kLt           || k == token::Kind::kGt
            || k == token::Kind::kLtEq         || k == token::Kind::kGtEq
            || k == token::Kind::kPlusEq       || k == token::Kind::kMinusEq
            || k == token::Kind::kStarEq       || k == token::Kind::kSlashEq
            || k == token::Kind::kPercentEq
            || k == token::Kind::kBitAndEq     || k == token::Kind::kBitOrEq
            || k == token::Kind::kBitXorEq
            || k == token::Kind::kLShiftEq     || k == token::Kind::kRShiftEq
            || k == token::Kind::kAndEq        || k == token::Kind::kOrEq
            || k == token::Kind::kXorXorEq;
    }

    static bool isCompoundAssignSym(std::string const& s) {
        return s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%="
            || s == "&=" || s == "|=" || s == "^=" || s == "<<=" || s == ">>="
            || s == "&&=" || s == "||=" || s == "^^=";
    }
    static bool isComparisonSym(std::string const& s) {
        return s == "==" || s == "!=" || s == "<" || s == ">"
            || s == "<=" || s == ">=";
    }
    static std::string operatorArityText(std::string const& sym) {
        if (sym == "+" || sym == "-") return "0, 1, or 2 parameters";
        if (sym == "~" || sym == "!") return "0 or 1 parameters";
        if (sym == "^")               return "0 or 2 parameters";
        if (sym == "=" || sym == "<--" || sym == "<-->" || sym == "[]"
            || isCompoundAssignSym(sym) || isComparisonSym(sym))
            return "exactly 1 parameter";
        return "exactly 2 parameters";
    }

    // Consume `op<sym>` — the `op` keyword is the current token — and return the
    // method name ("op+", "op<--", "op[]", …). Returns "" on error (reported).
    std::string parseOperatorName() {
        advance();   // op
        if (peek().kind == token::Kind::kLBracket) {
            advance();   // [
            if (peek().kind != token::Kind::kRBracket) {
                error("Expected ']' to complete the 'op[]' operator name.");
                return "";
            }
            advance();   // ]
            return "op[]";
        }
        if (isOperatorSymbolKind(peek().kind)) {
            std::string name = "op" + peek().text;
            advance();
            return name;
        }
        error("Expected an operator symbol after 'op'.");
        return "";
    }

    // Enforce the SYNTACTIC operator-signature rules: arity, no default parameter
    // values, and no misplaced `mutable`. `node.params` holds the EXPLICIT parameters
    // only (the implicit `_$recv` is spliced in after parseFunctionDef returns), so
    // params.size() is the operator's arity. Type-dependent rules (built-in return,
    // `Type^` reference return, the same-class swap parameter, primitive/const-pointer
    // parameters, no naked operators) are enforced later, where types are resolved.
    bool validateOperatorSignature(parse::Node& node, int name_tok) {
        std::string const& name = node.name;         // "op<sym>"
        std::string sym = name.substr(2);
        size_t arity = node.params.size();

        for (auto const& p : node.params) {
            if (!p->children.empty()) {              // children[0] is a default value
                errorAt(p->name_tok,
                        "An operator parameter may not have a default value.");
                return false;
            }
        }

        // Allowed arity per operator (explicit parameters; receiver implicit).
        bool arity_ok;
        if (sym == "+" || sym == "-") {
            arity_ok = (arity <= 2);
        } else if (sym == "~" || sym == "!") {
            arity_ok = (arity == 0 || arity == 1);
        } else if (sym == "^") {
            arity_ok = (arity == 0 || arity == 2);   // deref (0) or binary xor (2)
        } else if (sym == "=" || sym == "<--" || sym == "<-->" || sym == "[]"
                   || isCompoundAssignSym(sym) || isComparisonSym(sym)) {
            arity_ok = (arity == 1);
        } else {
            arity_ok = (arity == 2);                 // the remaining binary operators
        }
        if (!arity_ok) {
            errorAt(name_tok, "The '" + name + "' operator takes "
                    + operatorArityText(sym) + ", not "
                    + std::to_string(arity) + ".");
            return false;
        }

        // `mutable` is a move/swap-only qualifier; every other operator forbids it.
        if (sym != "<--" && sym != "<-->") {
            for (auto const& p : node.params) {
                if (p->is_mutable) {
                    errorAt(p->name_tok, "The '" + name
                            + "' operator parameter may not be 'mutable'.");
                    return false;
                }
            }
        }
        return true;
    }

    bool looksLikeFunctionDef() const {
        int o = 0;
        // An operator method may omit the return type (`op+(...)`); when a return
        // type is present it is scanned exactly as for a named function, and the
        // name slot then holds `op<sym>` instead of an identifier.
        if (peekKind(o) != token::Kind::kOp) {
            if (isTypeStart(peekKind(o))) {
                o++;
            } else if (peekKind(o) == token::Kind::kColonColon
                       || peekKind(o) == token::Kind::kIdentifier) {
                if (peekKind(o) == token::Kind::kColonColon) o++;
                if (peekKind(o) != token::Kind::kIdentifier) return false;
                o++;
                while (peekKind(o) == token::Kind::kColon
                       && peekKind(o + 1) == token::Kind::kIdentifier) o += 2;
            } else {
                return false;
            }
            // A pointer/iterator return-type suffix: `T^` / `T^^` (reference) or
            // `T[]` (iterator). Without this a method/function returning a pointer
            // (`Self^ me()`) is not recognized as a function def.
            if (peekKind(o) == token::Kind::kBitXor
                || peekKind(o) == token::Kind::kXorXor) o++;
            else if (peekKind(o) == token::Kind::kLBracket
                && peekKind(o + 1) == token::Kind::kRBracket) o += 2;
        }
        if (peekKind(o) == token::Kind::kOp) {
            // `op<sym>`: `op` plus one operator token, or the two-token `[]`. An
            // `op`-led shape with no return type reaches here with o == 0.
            o++;
            if (peekKind(o) == token::Kind::kLBracket
                && peekKind(o + 1) == token::Kind::kRBracket) o += 2;
            else if (isOperatorSymbolKind(peekKind(o))) o++;
            else return false;
        } else {
            if (peekKind(o) != token::Kind::kIdentifier) return false;   // fn name
            o++;
            // A QUALIFIED method name (`int Class:m(`, `void A:B:m(`) — the external
            // out-of-line form. Consume the `:segment` chain after the first name
            // segment so the shape reaches parseFunctionDef (which parses the
            // qualifier itself).
            while (peekKind(o) == token::Kind::kColon
                   && peekKind(o + 1) == token::Kind::kIdentifier) o += 2;
        }
        if (peekKind(o) != token::Kind::kLParen) return false;
        // `Type name (` is shared with a variable CONSTRUCTION `Type name(args);`.
        // Scan to the matching `)` and look past it: a `{` body is always a
        // function def; a `;` is a function (forward decl) only when the parens
        // hold a PARAMETER list (type-led), not construction arguments
        // (expressions) or an empty `()`.
        int open = o;
        int depth = 0;
        int close = -1;
        for (int i = open; ; i++) {
            token::Kind k = peekKind(i);
            if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput
                || k == token::Kind::kError) return false;
            if (k == token::Kind::kLParen) depth++;
            else if (k == token::Kind::kRParen && --depth == 0) { close = i; break; }
        }
        token::Kind after = peekKind(close + 1);
        if (after == token::Kind::kLBrace) return true;        // definition
        if (after == token::Kind::kEquals) return true;        // `= delete` pure virtual
        if (after != token::Kind::kSemicolon) return false;
        token::Kind first = peekKind(open + 1);
        if (first == token::Kind::kRParen) return false;       // `()` -> construction
        if (isTypeStart(first)) return true;                   // `(int ...)` -> params
        if (first == token::Kind::kLParen) {
            // `( ( ... ) ...` is a tuple-TYPE param `((int,int) name)` — followed
            // by a param name — or a tuple-EXPR construction arg `((13,17))` —
            // followed by `,` / `)`. Scan the inner tuple to its matching `)`.
            int d = 0, inner_close = -1;
            for (int i = open + 1; ; i++) {
                token::Kind k = peekKind(i);
                if (k == token::Kind::kEndOfFile || k == token::Kind::kEndOfInput
                    || k == token::Kind::kError) break;
                if (k == token::Kind::kLParen) d++;
                else if (k == token::Kind::kRParen && --d == 0) { inner_close = i; break; }
            }
            return inner_close >= 0
                && peekKind(inner_close + 1) == token::Kind::kIdentifier;
        }
        if (first == token::Kind::kIdentifier
            && peekKind(open + 2) == token::Kind::kIdentifier) return true;  // `Type name`
        return false;                                          // expression args
    }

    // At a namespace / file-scope body (where there are no statements), a var-decl
    // shape with no `global` keyword IS a bare global (`int x = 0;` desugars to
    // `global int x = 0;`). True for the DECLARATION shapes only — a name NOT
    // followed by `(` — so function defs and `name();` forward decls still route to
    // parseFunctionDef. Mirrors looksLikeFunctionDef's type scan.
    bool looksLikeBareVarDecl() const {
        // Inferred: `name = init` — a lone identifier assigned. (No assignment
        // statements exist at namespace/file scope, so this is always a decl.)
        if (peekKind(0) == token::Kind::kIdentifier
            && peekKind(1) == token::Kind::kEquals) return true;
        int o = 0;
        if (isTypeStart(peekKind(o))) {
            o++;
        } else if (peekKind(o) == token::Kind::kColonColon
                   || peekKind(o) == token::Kind::kIdentifier) {
            if (peekKind(o) == token::Kind::kColonColon) o++;
            if (peekKind(o) != token::Kind::kIdentifier) return false;
            o++;
            while (peekKind(o) == token::Kind::kColon
                   && peekKind(o + 1) == token::Kind::kIdentifier) o += 2;
        } else {
            return false;
        }
        if (peekKind(o) == token::Kind::kBitXor
            || peekKind(o) == token::Kind::kXorXor) o++;
        else if (peekKind(o) == token::Kind::kLBracket
                 && peekKind(o + 1) == token::Kind::kRBracket) o += 2;
        if (peekKind(o) != token::Kind::kIdentifier) return false;   // var name
        token::Kind after = peekKind(o + 1);
        return after == token::Kind::kEquals || after == token::Kind::kSemicolon;
    }

    // Parse a parenthesized parameter / field list — the `(` already consumed —
    // filling node->params with kParam nodes (each `[type] name [= default]`,
    // default in children[0]). Consumes the closing `)`. Shared by a function
    // def's parameters and a class def's field list (a class field is a slid
    // param). Returns false on a parse error.
    // `allow_mutable` distinguishes a function/method parameter list (where the
    // `mutable` pointer-qualifier is legal) from a class FIELD list (where it is not —
    // a field is a non-parameter; parseType then diagnoses the misplaced keyword).
    bool parseParamList(parse::Node* node, bool allow_mutable = true,
                        bool allow_incomplete = false) {
        while (peek().kind != token::Kind::kRParen) {
            // A trailing `...` marks an INCOMPLETE class field tuple (a later same-scope
            // re-open may append fields). Only a CLASS field list allows it, and only as
            // the LAST item — v2 has no leading/interior ellipsis (unlike v1).
            if (allow_incomplete && peek().kind == token::Kind::kEllipsis) {
                node->is_incomplete = true;
                int ell_tok = pos;   // caret the ellipsis itself, not the token after it
                advance();   // ...
                if (peek().kind != token::Kind::kRParen) {
                    errorAt(ell_tok, "'...' must be the last item in a class field list.");
                    return false;
                }
                break;
            }
            // `[mutable] [type] name [= constexpr]` — the type is optional; a typeless
            // param infers its type from its default value. For a FUNCTION param
            // list, resolve/classify enforce that a typeless param HAS a default
            // and that a required param cannot follow an optional one; a CLASS
            // field list has NEITHER restriction — any field may omit its default
            // (it then zero-fills) regardless of position. Name-anchored array
            // dims (`int f(int a[3])`) ride on the name, same form as a var decl.
            bool is_mutable = false;
            if (allow_mutable && peek().kind == token::Kind::kMutable) {
                is_mutable = true;
                advance();   // mutable
            }
            Declarator pd;
            if (!parseDeclarator(NamePolicy::Required, /*parse_name_dims=*/true,
                                 /*allow_qualified=*/false,
                                 "Expected parameter name.", pd)) {
                return false;
            }
            auto p = newNodeAt(parse::Kind::kParam, pd.file_id, pd.tok);
            p->name = std::move(pd.name);
            p->name_tok = pd.name_tok;
            p->is_mutable = is_mutable;
            p->return_type = widen::internOrNone(pd.type);
            if (pd.any_dim_expr) p->dim_exprs = std::move(pd.dim_exprs);
            if (peek().kind == token::Kind::kEquals) {
                advance();   // =
                auto def = parseExpr();
                if (!def) return false;
                p->children.push_back(std::move(def));   // children[0] = default
            }
            node->params.push_back(std::move(p));
            if (peek().kind == token::Kind::kComma) {
                advance();
                continue;
            }
            if (peek().kind != token::Kind::kRParen) {
                error("Expected ',' or ')' in parameter list.");
                return false;
            }
        }
        if (!expect(token::Kind::kRParen, ")")) return false;
        return true;
    }

    // `Name(field-list) { body }` — a class definition. The field list is the
    // named tuple (parsed as params); the body holds member definitions. Today
    // the only members are the constructor `_(){...}` and destructor `~(){...}`
    // (no author params; they must appear together). Each is parsed as a
    // kFunctionDef carrying an implicit `self` (`Name^`) param, stored in
    // node->children; resolve binds bare field names to self.
    std::unique_ptr<parse::Node> parseClassDef() {
        int cls_file = peek().file_id;
        int cls_tok = pos;
        std::string name = peek().text;
        int name_tok = pos;
        advance();   // class (or base) name
        // `Base : Derived(...)` — the leading name was the BASE; the real class name
        // follows the `:`. Store the base name on the node (text); resolve prepends
        // it as the unnamed first field.
        std::string base_name;
        if (peek().kind == token::Kind::kColon) {
            advance();   // :
            base_name = name;
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected the derived class name after 'Base :'.");
                return nullptr;
            }
            name = peek().text;
            name_tok = pos;
            advance();   // derived class name
        }
        if (!expect(token::Kind::kLParen, "(")) return nullptr;

        auto node = newNodeAt(parse::Kind::kClassDef, cls_file, cls_tok);
        node->name = name;
        node->name_tok = name_tok;
        node->text = base_name;          // "" if not derived

        if (!parseParamList(node.get(), /*allow_mutable=*/false,
                            /*allow_incomplete=*/true)) return nullptr;
        // A derived class carries its base as the UNNAMED FIRST FIELD `_$base` of
        // type Base: the layout becomes [Base, own fields...], so construction,
        // ctor/dtor hooks, the needs-fixpoint, and the by-value cycle check all reuse
        // the class-typed-field machinery. `Base:` resolution reframes `self` to this
        // slot 0.
        if (!base_name.empty()) {
            auto bp = newNodeAt(parse::Kind::kParam, cls_file, name_tok);
            bp->name = "_$base";
            bp->name_tok = name_tok;
            bp->return_type = widen::internOrNone(base_name);
            node->params.insert(node->params.begin(), std::move(bp));
        }
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;

        std::string recv_type = name + "^";   // the implicit receiver param's type
        bool ctor_decl = false, ctor_def = false;
        bool dtor_decl = false, dtor_def = false;
        bool saw_any_virtual = false;        // any `virtual` member -> a virtual class
        while (!fatal && peek().kind != token::Kind::kRBrace) {
            if (peek().kind == token::Kind::kEndOfFile
                || peek().kind == token::Kind::kEndOfInput) {
                error("Expected '}'.");
                return nullptr;
            }
            // A leading `virtual` modifies the method or the destructor that follows.
            bool saw_virtual = false;
            int virt_tok = pos;
            if (peek().kind == token::Kind::kVirtual) {
                saw_virtual = true;
                saw_any_virtual = true;
                advance();   // virtual
            }
            // ctor/dtor are method-shaped and class-only — peel them off first
            // (a `_()` / `~()` also matches looksLikeClassDef). Everything else is
            // the shared definition vocabulary, parsed exactly as a namespace member
            // except functions become methods here (in_class injects `_$recv`).
            bool is_ctor = (peek().kind == token::Kind::kIdentifier
                            && peek().text == "_");
            bool is_dtor = (peek().kind == token::Kind::kBitNot);
            if (!is_ctor && !is_dtor) {
                auto m = parseDefinitionMember(/*in_class=*/true, recv_type);
                if (!m) return nullptr;
                if (saw_virtual) {
                    // `virtual` only decorates a method (a function member).
                    if (m->kind != parse::Kind::kFunctionDef
                        && m->kind != parse::Kind::kFunctionDecl) {
                        errorAt(virt_tok, "'virtual' may modify only a method or the "
                                          "destructor.");
                        return nullptr;
                    }
                    m->is_virtual = true;
                }
                // A pure method (`= delete`) only exists as a virtual — it is the empty
                // vtable slot of an abstract class, so it is meaningless without dispatch.
                if (m->is_pure && !m->is_virtual) {
                    errorAt(m->name_tok, "A pure method ('= delete') must be virtual.");
                    return nullptr;
                }
                node->children.push_back(std::move(m));
                continue;
            }
            if (saw_virtual && is_ctor) {
                errorAt(virt_tok, "A constructor cannot be virtual.");
                return nullptr;
            }
            int m_file = peek().file_id;
            int m_tok = pos;
            advance();   // `_` or `~`
            if (!expect(token::Kind::kLParen, "(")) return nullptr;
            if (peek().kind != token::Kind::kRParen) {
                error("A constructor or destructor takes no parameters.");
                return nullptr;
            }
            advance();   // )
            // `_();` is a forward declaration; `_(){...}` is the definition. A
            // declaration emits no member node — it only obligates a definition.
            if (peek().kind == token::Kind::kSemicolon) {
                advance();   // ;
                if (is_ctor) ctor_decl = true; else dtor_decl = true;
                continue;
            }
            if (peek().kind != token::Kind::kLBrace) {
                error("Expected ';' (forward declaration) or '{' (definition) "
                      "after a constructor or destructor.");
                return nullptr;
            }
            advance();   // {
            // Caret the duplicate `_`/`~` (m_tok), not the body just consumed.
            if (is_ctor && ctor_def) { errorAt(m_tok, "Duplicate constructor."); return nullptr; }
            if (is_dtor && dtor_def) { errorAt(m_tok, "Duplicate destructor."); return nullptr; }

            auto member = newNodeAt(parse::Kind::kFunctionDef, m_file, m_tok);
            member->name = is_ctor ? "_$ctor" : "_$dtor";
            member->name_tok = m_tok;
            member->is_virtual = saw_virtual;   // `virtual ~()` — a virtual destructor
            member->return_type = widen::internOrNone("void");
            member->params.push_back(
                parse::makeReceiverParam(widen::internOrNone(recv_type),
                                         m_file, m_tok));

            std::string saved_func = current_func;
            current_func = name + (is_ctor ? "._" : ".~");
            if (!parseStmtsThroughRBrace(member->children)) return nullptr;
            current_func = std::move(saved_func);

            if (is_ctor) ctor_def = true; else dtor_def = true;
            node->children.push_back(std::move(member));
        }
        if (!expect(token::Kind::kRBrace, "}")) return nullptr;
        // A constructor and destructor are hooks for the same scope boundary;
        // one without the other is a contract error. Caret the class name (the
        // `}` is already consumed, so the current position is the next token).
        bool has_ctor = ctor_decl || ctor_def;
        bool has_dtor = dtor_decl || dtor_def;
        if (has_ctor != has_dtor) {
            errorAt(name_tok,
                    has_ctor ? "A constructor requires a matching destructor."
                             : "A destructor requires a matching constructor.");
            return nullptr;
        }
        // A forward declaration obligates a definition later in the body.
        if (ctor_decl && !ctor_def) {
            errorAt(name_tok, "A forward-declared constructor must be defined.");
            return nullptr;
        }
        if (dtor_decl && !dtor_def) {
            errorAt(name_tok, "A forward-declared destructor must be defined.");
            return nullptr;
        }
        // A virtual class carries a vtable pointer at OFFSET 0 (C++ ABI). A ROOT
        // virtual class (>=1 `virtual` member, no base) gets a hidden `_$vptr` field as
        // its UNNAMED FIRST FIELD — like `_$base`, it flows into the class layout so the
        // vptr occupies real storage (sizeof grows) and lands at offset 0. A DERIVED
        // virtual class does NOT: its `_$base` (slot 0) already carries the inherited
        // vptr, so offset 0 stays the vptr transitively. The two are mutually exclusive.
        // Construction skips `_$vptr` (the ctor stamps the real vtable); it is never a
        // constructor argument.
        if (saw_any_virtual && base_name.empty()) {
            auto vp = newNodeAt(parse::Kind::kParam, cls_file, name_tok);
            vp->name = "_$vptr";
            vp->name_tok = name_tok;
            vp->return_type = widen::internPointer(widen::intern("int"));
            node->params.insert(node->params.begin(), std::move(vp));
        }
        return node;
    }

    std::unique_ptr<parse::Node> parseFunctionDef() {
        int fn_file = peek().file_id;
        int fn_tok = pos;
        // An operator method (`op<sym>(...)`) may omit the return type; a leading
        // `op` means there is no return-type declarator to parse.
        bool is_op = (peek().kind == token::Kind::kOp);
        // A qualified member defined out of line with NO return type — `Class:op+=(...)`
        // (canon: external reopen syntax for a produce-self operator) — leads with the
        // qualifier class where a return type would sit. Record whether the leading token
        // is a bare identifier and how many tokens the return-type declarator consumes, so
        // a lone identifier followed by `:` can be reinterpreted as the qualifier below.
        bool lead_ident = (!is_op && peek().kind == token::Kind::kIdentifier);
        int type_start = pos;
        Declarator d;
        if (!is_op) {
            if (!parseDeclarator(NamePolicy::Forbidden, /*parse_name_dims=*/false,
                                 /*allow_qualified=*/false, nullptr, d)) {
                return nullptr;
            }
        }
        std::string ret_type = std::move(d.type);
        std::string name;
        int name_tok;
        if (lead_ident && pos == type_start + 1
            && peek().kind == token::Kind::kColon) {
            // The bare-identifier "return type" is really the qualifier's FIRST segment and
            // there is NO return type (`Class:op+=` / `Class:method`): parseDeclarator ate
            // the class where a return type would be, and a `:` (with no name) follows.
            // Hand it to the name slot; the qualifier loop consumes the rest (incl. `:op`).
            name = std::move(ret_type);
            ret_type.clear();
            name_tok = type_start;
        } else if (peek().kind == token::Kind::kOp) {
            is_op = true;                       // `Ret op<sym>` — return type parsed above
            name_tok = pos;
            name = parseOperatorName();
            if (name.empty()) return nullptr;
        } else {
            if (peek().kind != token::Kind::kIdentifier) {
                error("Expected function name.");
                return nullptr;
            }
            name = peek().text;
            name_tok = pos;
            advance();
        }
        // A QUALIFIED head `Ret Class:method(...)` (or `A:B:m`) defines a member OUT OF
        // LINE: the leading segments are the qualifier (the target class / namespace),
        // the last is the member name. resolve routes it into that frame — for a class
        // target it becomes a method (receiver-injected). The return-type prefix
        // distinguishes this from an inheritance head (`Base : Derived(...)`).
        std::vector<std::string> qualifier;
        std::vector<int> qualifier_toks;
        while (peek().kind == token::Kind::kColon) {
            advance();   // :
            qualifier.push_back(std::move(name));
            qualifier_toks.push_back(name_tok);
            // The qualified member may itself be an OPERATOR (`Ret Class:op+(...)`, or a
            // produce-self `Class:op+=(...)` whose qualifier was reinterpreted above) — an
            // operator is just a method, so the out-of-line member form accepts an
            // `op<sym>` name after ':' exactly as the inline head does above.
            if (peek().kind == token::Kind::kOp) {
                is_op = true;
                name_tok = pos;
                name = parseOperatorName();
                if (name.empty()) return nullptr;
            } else if (peek().kind == token::Kind::kIdentifier) {
                name = peek().text;
                name_tok = pos;
                advance();
            } else {
                error("Expected a member name after ':' in a qualified definition.");
                return nullptr;
            }
        }
        // An operator that "produces self" writes no return type; it mutates the receiver
        // and returns nothing at the ABI level, so it lowers as `void`. (A value-yielding
        // operator — comparison, unary arity-0, index/deref — spells its return type, so
        // it is already set.) Applied AFTER the qualifier loop so it also covers an out-of-
        // line op whose `is_op` is only discovered there (`Class:op+=`).
        if (is_op && ret_type.empty()) ret_type = "void";
        if (!expect(token::Kind::kLParen, "(")) return nullptr;

        auto node = newNodeAt(parse::Kind::kFunctionDef, fn_file, fn_tok);
        node->name = std::move(name);
        node->name_tok = name_tok;
        node->qualifier = std::move(qualifier);
        node->qualifier_toks = std::move(qualifier_toks);
        node->return_type = widen::internOrNone(ret_type);
        // A const-expr dim in the RETURN type (`(int[N],int) f()`): a function's
        // entry slids_type IS its return type, so bakeNodeDims bakes node->dim_exprs
        // against node->return_type exactly as for a var-decl.
        if (d.any_dim_expr) node->dim_exprs = std::move(d.dim_exprs);

        if (!parseParamList(node.get())) return nullptr;

        if (is_op && !validateOperatorSignature(*node, name_tok)) return nullptr;

        // A PURE virtual: `virtual T m(...) = delete;` — no body. Only valid on a virtual
        // method (resolve enforces that + rejects it on a free function); parse just
        // records is_pure and leaves it bodyless like a forward decl.
        if (peek().kind == token::Kind::kEquals) {
            advance();   // =
            if (peek().kind != token::Kind::kDelete) {
                error("Expected 'delete' after '=' in a pure virtual method.");
                return nullptr;
            }
            advance();   // delete
            if (!expect(token::Kind::kSemicolon, ";")) return nullptr;
            node->kind = parse::Kind::kFunctionDecl;
            node->is_pure = true;
            return node;
        }
        if (peek().kind == token::Kind::kSemicolon) {
            advance();
            node->kind = parse::Kind::kFunctionDecl;
            return node;
        }
        if (!expect(token::Kind::kLBrace, "{")) return nullptr;

        std::string saved_func = current_func;
        current_func = node->name;   // ##func in the body names this function
        if (!parseStmtsThroughRBrace(node->children)) return nullptr;
        current_func = std::move(saved_func);
        return node;
    }

    void parseProgram() {
        auto prog = std::make_unique<parse::Node>();
        prog->kind = parse::Kind::kProgram;
        while (!fatal) {
            while (peek().kind == token::Kind::kEndOfFile) advance();
            if (peek().kind == token::Kind::kEndOfInput) break;
            // File scope IS the global namespace body — bounded by end-of-input
            // instead of braces, framed by the pre-existing kGlobalFrame instead of
            // an opened one. Its members are exactly namespace members, so the same
            // dispatch applies (const/alias/enum/class/namespace/function; ctor/dtor
            // rejected). The two file-scope-only differences (terminator, frame) live
            // in this loop and in resolve, not in what a member may be.
            auto child = parseNamespaceMember();
            if (!child) return;
            prog->children.push_back(std::move(child));
        }
        if (!fatal) out.nodes.push_back(std::move(prog));
    }
};

}  // namespace

void run(token::List const& in, parse::Tree& out, diagnostic::Sink& diag) {
    Parser p{in, out, diag};
    p.parseProgram();
}

}  // namespace grammar
