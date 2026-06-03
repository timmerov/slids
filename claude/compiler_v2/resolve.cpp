#include "resolve.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic.h"
#include "parse.h"
#include "widen.h"

namespace resolve {

namespace {

bool isPrintIntrinsic(std::string const& name) {
    return name == "__println" || name == "__print";
}

// Reject the declared / return / parameter type if it's not a known spelling.
// Caret points at the construct's own tok (the type-name token's position),
// which together with surrounding source context tells the user where the
// unknown name appears.
void requireKnownType(std::string const& t, int file_id, int tok,
                      diagnostic::Sink& diag) {
    if (widen::isKnownType(t)) return;
    diagnostic::report(diag, {file_id, tok,
        "Unknown type '" + t + "'.", {}});
}

// Resolve a namespace-qualified type spelling (`Space:Dir`) to its underlying.
std::string resolveQualifiedType(parse::Tree& tree, std::string const& base,
                                 int file_id, int tok, bool& reported,
                                 diagnostic::Sink& diag);

// Substitute a type-alias spelling to its underlying type, following chains
// (`alias A = B; alias B = int` → `int`) and detecting cycles. A namespace-
// qualified spelling (`Space:Dir`) resolves through the namespace chain. The
// `[]` suffix rides along. A spelling that isn't an alias / enum / qualified
// type is returned unchanged. `reported` is set when a diagnostic was already
// emitted (a cycle, or a failed qualified resolution) so the caller skips the
// redundant "Unknown type".
std::string resolveTypeSpelling(parse::Tree& tree, std::string const& spelling,
                                std::set<std::string>& visiting, bool& reported,
                                int file_id, int tok, diagnostic::Sink& diag) {
    std::string base = spelling;
    std::string suffix;
    while (base.size() >= 2 && base.compare(base.size() - 2, 2, "[]") == 0) {
        suffix += "[]";
        base.resize(base.size() - 2);
    }
    if (base.find(':') != std::string::npos) {
        std::string under =
            resolveQualifiedType(tree, base, file_id, tok, reported, diag);
        if (under.empty()) return base + suffix;   // error already reported
        return under + suffix;
    }
    int id = parse::findInLiveScopes(tree, base);
    // A type alias substitutes to its target. An enum's name doubles as a
    // namespace AND a transparent type alias to the underlying (carried on the
    // namespace entry's slids_type); treat that like an alias here. A plain
    // namespace (empty slids_type) is not a type and falls through unchanged →
    // requireKnownType then rejects it.
    bool is_alias = id >= 0 && tree.entries[id].kind == parse::EntryKind::kAlias;
    bool is_enum_type = id >= 0
        && tree.entries[id].kind == parse::EntryKind::kNamespace
        && !tree.entries[id].slids_type.empty();
    if (!is_alias && !is_enum_type) {
        return spelling;
    }
    if (is_enum_type) {
        // The underlying is already a primitive spelling (validated at decl);
        // no further chain to chase.
        return tree.entries[id].slids_type + suffix;
    }
    if (visiting.count(base)) {
        reported = true;
        diagnostic::report(diag, {file_id, tok,
            "Type alias '" + base + "' is part of a cycle.", {}});
        return base;
    }
    visiting.insert(base);
    std::string resolved = resolveTypeSpelling(
        tree, tree.entries[id].slids_type, visiting, reported, file_id, tok, diag);
    visiting.erase(base);
    return resolved + suffix;
}

// Rewrite a declared type spelling in place to its underlying type, then
// require the result to be a known type. A cycle has already been reported, so
// skip the redundant "Unknown type" that the broken chain would otherwise emit.
void resolveDeclType(parse::Tree& tree, std::string& spelling,
                     int file_id, int tok, diagnostic::Sink& diag) {
    std::set<std::string> visiting;
    bool reported = false;
    spelling = resolveTypeSpelling(tree, spelling, visiting, reported,
                                   file_id, tok, diag);
    if (!reported) requireKnownType(spelling, file_id, tok, diag);
}

// Register `alias Name = Type;` as a kAlias entry in the current frame.
void registerAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
    if (existing >= 0) {
        parse::Entry const& prev = tree.entries[existing];
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.",
            {{prev.file_id, prev.tok, "first declared here"}}});
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kAlias;
    e.name = s.name;
    e.slids_type = s.return_type;   // target spelling; resolved at use
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag);
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag);

// ---- Namespace lookup --------------------------------------------------------
// The global root is namespace frame 0 (the program frame). Its members are the
// file-scope entries (parent_frame_id == 0, not themselves namespace members).
constexpr int kGlobalFrame = 0;

int findMemberLive(parse::Tree const& tree, int ns_frame,
                   std::string const& name);

// Bare-name resolution. Qualifiers are always optional, so a bare name resolves
// against (1) the open-namespace chain, innermost first — this is the current
// member's siblings and its enclosing namespaces, plus any `alias Ns;` imports;
// then (2) the ordinary lexical scope (locals, params, file-scope entries),
// innermost first. (1) before (2) means a sibling member shadows an outer name;
// reaching the shadowed outer needs `::`. Returns entry id or -1.
// Like resolveName, but reports an open-namespace-import collision: if the name
// matches distinct members in two different open namespaces (e.g. `alias Bonk1;
// alias Bonk2;` where both define `kOops`), `other` is set to the second match
// so the caller can emit an ambiguity diagnostic. Returns the first match (or
// the lexical match if no namespace match). `other` is -1 when unambiguous.
int resolveNameDetail(parse::Tree const& tree, std::string const& name,
                      int& other) {
    other = -1;
    int first = -1;
    for (auto it = tree.open_ns_frames.rbegin();
         it != tree.open_ns_frames.rend(); ++it) {
        int id = findMemberLive(tree, *it, name);
        if (id < 0) continue;
        if (first < 0) {
            first = id;
        } else if (id != first) {
            other = id;
            return first;
        }
    }
    if (first >= 0) return first;
    for (auto it = tree.live_entry_ids.rbegin();
         it != tree.live_entry_ids.rend(); ++it) {
        parse::Entry const& e = tree.entries[*it];
        if (e.name == name && e.owner_ns_frame < 0) return *it;
    }
    return -1;
}

int resolveName(parse::Tree const& tree, std::string const& name) {
    int other;
    return resolveNameDetail(tree, name, other);
}

// Does any entry exist as a namespace member of the given name? Used to choose
// "needs a qualifier" over "unresolved" when a bare lookup fails.
bool namespaceMemberExists(parse::Tree const& tree, std::string const& name) {
    for (parse::Entry const& e : tree.entries) {
        if (e.owner_ns_frame >= 0 && e.name == name) return true;
    }
    return false;
}

// Find a member `name` in namespace frame `ns_frame` among LIVE entries (so a
// member that has dropped out of scope is not found). Returns entry id or -1.
int findMemberLive(parse::Tree const& tree, int ns_frame,
                   std::string const& name) {
    for (int id : tree.live_entry_ids) {
        parse::Entry const& e = tree.entries[id];
        if (e.name != name) continue;
        if (ns_frame == kGlobalFrame) {
            if (e.parent_frame_id == kGlobalFrame && e.owner_ns_frame < 0) return id;
        } else if (e.owner_ns_frame == ns_frame) {
            return id;
        }
    }
    return -1;
}

// Find a member `name` declared in `ns_frame` among ALL entries (live or not).
// Used to distinguish "declared but not visible" from "no such member".
int findMemberDeclared(parse::Tree const& tree, int ns_frame,
                       std::string const& name) {
    for (std::size_t id = 0; id < tree.entries.size(); ++id) {
        parse::Entry const& e = tree.entries[id];
        if (e.name != name) continue;
        if (ns_frame == kGlobalFrame) {
            if (e.parent_frame_id == kGlobalFrame && e.owner_ns_frame < 0)
                return static_cast<int>(id);
        } else if (e.owner_ns_frame == ns_frame) {
            return static_cast<int>(id);
        }
    }
    return -1;
}

// Human-readable form of a qualifier chain (e.g. "Space:Nested", or "::" for a
// bare global qualifier) for the leaf "has no member" diagnostic.
std::string qualPrefixText(std::vector<std::string> const& segments, bool global,
                           std::size_t count) {
    std::string s;
    for (std::size_t i = 0; i < count; ++i) {
        if (!s.empty()) s += ":";
        s += segments[i];
    }
    if (s.empty()) return global ? "::" : "";
    return s;
}

// Walk a chain of namespace segments to the frame it names. `segments`/`toks`
// are parallel; each segment must resolve to a namespace. The caret lands on the
// offending segment's token, and the message names the chain resolved so far —
// the SOLE chain walker, shared by qualified refs, inline member decls, and
// bare aliases, so all three word identically. Returns -1 on a diagnosed error.
int resolveNamespaceSegments(parse::Tree const& tree,
                             std::vector<std::string> const& segments,
                             std::vector<int> const& toks, bool global,
                             int file_id, diagnostic::Sink& diag) {
    int cur;
    std::size_t i;
    if (global) {
        cur = kGlobalFrame;
        i = 0;
    } else {
        int id = resolveName(tree, segments[0]);
        if (id < 0 || tree.entries[id].kind != parse::EntryKind::kNamespace) {
            diagnostic::report(diag, {file_id, toks[0],
                "'" + segments[0] + "' is not a namespace.", {}});
            return -1;
        }
        cur = tree.entries[id].ns_frame_id;
        i = 1;
    }
    for (; i < segments.size(); ++i) {
        int id = findMemberLive(tree, cur, segments[i]);
        if (id < 0 || tree.entries[id].kind != parse::EntryKind::kNamespace) {
            diagnostic::report(diag, {file_id, toks[i],
                "'" + segments[i] + "' is not a namespace member of '"
                + qualPrefixText(segments, global, i) + "'.", {}});
            return -1;
        }
        cur = tree.entries[id].ns_frame_id;
    }
    return cur;
}

// Resolve a qualified reference (ident or call) to its target entry id, or -1
// on a diagnosed error. The qualifier names the namespace; node.name is the
// member looked up within it.
int resolveQualifiedRef(parse::Tree& tree, parse::Node& node,
                        diagnostic::Sink& diag) {
    int frame = resolveNamespaceSegments(tree, node.qualifier, node.qualifier_toks,
                                         node.global_qualified, node.file_id, diag);
    if (frame < 0) return -1;
    int id = findMemberLive(tree, frame, node.name);
    if (id >= 0) return id;
    if (findMemberDeclared(tree, frame, node.name) >= 0) {
        diagnostic::report(diag, {node.file_id, node.name_tok,
            "'" + node.name + "' is not visible from this scope.", {}});
        return -1;
    }
    std::string prefix = qualPrefixText(node.qualifier, node.global_qualified,
                                        node.qualifier.size());
    diagnostic::report(diag, {node.file_id, node.name_tok,
        "'" + prefix + "' has no member '" + node.name + "'.", {}});
    return -1;
}

bool isQualified(parse::Node const& n) {
    return n.global_qualified || !n.qualifier.empty();
}

// Resolve a namespace-qualified type spelling (`Space:Dir`, `::A:B:Type`) to its
// underlying type. The leading segments name a namespace path; the final segment
// must be a type living there — today an enum (a kNamespace carrying a non-empty
// slids_type underlying). Returns the underlying, or "" with `reported` set after
// a diagnostic. Carets land at `tok` (the decl's type position) for every
// segment, since a type spelling carries no per-segment tokens.
std::string resolveQualifiedType(parse::Tree& tree, std::string const& base,
                                 int file_id, int tok, bool& reported,
                                 diagnostic::Sink& diag) {
    std::vector<std::string> segs;
    bool global = false;
    std::size_t i = 0;
    if (base.size() >= 2 && base[0] == ':' && base[1] == ':') {
        global = true;
        i = 2;
    }
    std::string cur;
    for (; i < base.size(); ++i) {
        if (base[i] == ':') { segs.push_back(cur); cur.clear(); }
        else cur.push_back(base[i]);
    }
    segs.push_back(cur);
    std::vector<int> toks(segs.size(), tok);
    // Walk all but the last segment as a namespace path; look the last up there.
    std::vector<std::string> path(segs.begin(), segs.end() - 1);
    std::vector<int> ptoks(toks.begin(), toks.end() - 1);
    int frame = resolveNamespaceSegments(tree, path, ptoks, global, file_id, diag);
    if (frame < 0) { reported = true; return ""; }
    int id = findMemberLive(tree, frame, segs.back());
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kNamespace
        || tree.entries[id].slids_type.empty()) {
        diagnostic::report(diag, {file_id, tok,
            "'" + segs.back() + "' is not a type in '"
            + qualPrefixText(segs, global, segs.size() - 1) + "'.", {}});
        reported = true;
        return "";
    }
    return tree.entries[id].slids_type;
}

// ---- Namespace registration -------------------------------------------------
void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag);
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag);
void resolveNamespaceBodies(parse::Tree& tree, parse::Node& node,
                            int parent_ns, diagnostic::Sink& diag);
void registerEnum(parse::Tree& tree, parse::Node& node, int parent_ns,
                  diagnostic::Sink& diag);
void resolveEnumMemberInits(parse::Tree& tree, parse::Node& node,
                            diagnostic::Sink& diag);

// Report any local in `body_locals` never read: "Unused local variable 'x'." if
// it was never written (a1 — declared and ignored), else "Local variable 'x'
// set but never used." (a2 — a value was computed and discarded). Caret at the
// decl. Gated by the caller on hasErrors so a value-before-init / dup diagnostic
// isn't trailed by spurious unused reports. Shared by the per-function and
// per-block scope exits.
void sweepUnusedLocals(parse::Tree& tree, diagnostic::Sink& diag) {
    if (diagnostic::hasErrors(diag)) return;
    for (int id : tree.body_locals) {
        if (tree.read_locals.count(id) > 0) continue;
        parse::Entry const& e = tree.entries[id];
        bool was_set = tree.initialized_locals.count(id) > 0;
        diagnostic::report(diag, {e.file_id, e.tok,
            was_set ? "Local variable '" + e.name + "' set but never used."
                    : "Unused local variable '" + e.name + "'.", {}});
    }
}

// Find an existing namespace named `name` to reopen. For a top-level namespace
// (parent_ns == global) this is a lexical lookup so a locally-opened namespace
// reopens correctly; for a nested namespace it is a member lookup in the parent.
// Returns its ns_frame_id, or -1 if none exists (caller creates one). Sets
// `collision` if a non-namespace of that name blocks the open.
int findExistingNamespace(parse::Tree const& tree, std::string const& name,
                          int parent_ns, bool& collision) {
    collision = false;
    int id;
    if (parent_ns == kGlobalFrame) {
        id = -1;
        for (auto it = tree.live_entry_ids.rbegin();
             it != tree.live_entry_ids.rend(); ++it) {
            parse::Entry const& e = tree.entries[*it];
            if (e.name == name && e.owner_ns_frame < 0) { id = *it; break; }
        }
    } else {
        id = findMemberDeclared(tree, parent_ns, name);
    }
    if (id < 0) return -1;
    if (tree.entries[id].kind != parse::EntryKind::kNamespace) {
        collision = true;
        return -1;
    }
    return tree.entries[id].ns_frame_id;
}

// Open (or reopen) the namespace `name` whose parent namespace is `parent_ns`.
// Returns its ns_frame_id. A fresh open allocates an id and adds a kNamespace
// name entry at the current lexical frame (so a locally-opened namespace pops
// with its scope). Members added under the returned frame inherit the current
// lexical lifetime.
int openNamespace(parse::Tree& tree, std::string const& name, int name_tok,
                  int file_id, int parent_ns, diagnostic::Sink& diag,
                  std::string const& enum_underlying = "") {
    bool collision = false;
    int existing = findExistingNamespace(tree, name, parent_ns, collision);
    if (existing >= 0) return existing;
    if (collision) {
        diagnostic::report(diag, {file_id, name_tok,
            "'" + name + "' is already declared and is not a namespace.", {}});
        return -1;
    }
    int fid = parse::allocFrameId(tree);
    parse::Entry e;
    e.kind = parse::EntryKind::kNamespace;
    e.name = name;
    e.ns_frame_id = fid;
    e.owner_ns_frame = (parent_ns == kGlobalFrame) ? -1 : parent_ns;
    e.slids_type = enum_underlying;   // non-empty → enum (doubles as a type)
    e.file_id = file_id;
    e.tok = name_tok;
    parse::addEntry(tree, std::move(e));
    return fid;
}

// Register one namespace member's signature (no body): a const pre-creates its
// entry, a function records its signature, a nested namespace recurses.
void registerMemberSignature(parse::Tree& tree, parse::Node& m, int ns_frame,
                             diagnostic::Sink& diag) {
    if (m.kind == parse::Kind::kNamespaceDecl) {
        registerNamespaceTree(tree, m, ns_frame, diag);
        return;
    }
    if (m.kind == parse::Kind::kEnumDecl) {
        registerEnum(tree, m, ns_frame, diag);
        return;
    }
    if (m.kind == parse::Kind::kVarDeclStmt && m.is_const) {
        resolveDeclType(tree, m.return_type, m.file_id, m.tok, diag);
        if (findMemberDeclared(tree, ns_frame, m.name) >= 0) {
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.", {}});
            return;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = m.name;
        e.slids_type = m.return_type;
        e.file_id = m.file_id;
        e.tok = m.name_tok;
        e.owner_ns_frame = ns_frame;
        m.resolved_entry_id = parse::addEntry(tree, std::move(e));
        return;
    }
    if (m.kind == parse::Kind::kFunctionDef
     || m.kind == parse::Kind::kFunctionDecl) {
        resolveDeclType(tree, m.return_type, m.file_id, m.tok, diag);
        std::vector<std::string> param_types;
        for (auto& p : m.params) {
            if (!p) continue;
            resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
            param_types.push_back(p->return_type);
        }
        if (findMemberDeclared(tree, ns_frame, m.name) >= 0) {
            diagnostic::report(diag, {m.file_id, m.name_tok,
                "Duplicate declaration of '" + m.name + "'.", {}});
            return;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kFunction;
        e.name = m.name;
        e.slids_type = m.return_type;
        e.param_types = std::move(param_types);
        e.file_id = m.file_id;
        e.tok = m.name_tok;
        e.defined = (m.kind == parse::Kind::kFunctionDef);
        e.owner_ns_frame = ns_frame;
        m.resolved_entry_id = parse::addEntry(tree, std::move(e));
        return;
    }
    diagnostic::report(diag, {m.file_id, m.tok,
        "Only constants, functions, enums, and namespaces may appear in a "
        "namespace.", {}});
}

// Register a namespace and all its members' signatures (no bodies). Recurses
// into nested namespaces. Members inherit the current lexical lifetime.
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag) {
    int ns = openNamespace(tree, node.name, node.name_tok, node.file_id,
                           parent_ns, diag);
    if (ns < 0) return;
    node.resolved_entry_id = ns;   // stash the ns frame for the body pass
    // Register type-introducing members (enums, nested namespaces) first, so a
    // const / function member's type may name a sibling enum regardless of
    // declaration order — mirrors the file-scope pass ordering.
    for (auto& m : node.children) {
        if (m && (m->kind == parse::Kind::kEnumDecl
                  || m->kind == parse::Kind::kNamespaceDecl)) {
            registerMemberSignature(tree, *m, ns, diag);
        }
    }
    for (auto& m : node.children) {
        if (m && m->kind != parse::Kind::kEnumDecl
              && m->kind != parse::Kind::kNamespaceDecl) {
            registerMemberSignature(tree, *m, ns, diag);
        }
    }
}

// Resolve member bodies of an already-registered namespace: const inits,
// function bodies (which see the open-namespace chain), and nested namespaces.
void resolveNamespaceBodies(parse::Tree& tree, parse::Node& node,
                            int parent_ns, diagnostic::Sink& diag) {
    (void)parent_ns;
    int ns = node.resolved_entry_id;   // stashed by registerNamespaceTree
    if (ns < 0) return;   // registration already diagnosed any problem
    tree.open_ns_frames.push_back(ns);
    for (auto& m : node.children) {
        if (!m) continue;
        if (m->kind == parse::Kind::kNamespaceDecl) {
            resolveNamespaceBodies(tree, *m, ns, diag);
        } else if (m->kind == parse::Kind::kEnumDecl) {
            // The namespace frame is open; resolveEnumMemberInits opens the
            // enum's own frame on top, so member inits see siblings + the
            // enclosing namespace's members.
            resolveEnumMemberInits(tree, *m, diag);
        } else if (m->kind == parse::Kind::kVarDeclStmt && m->is_const) {
            for (auto& init : m->children) {
                if (init) resolveExpr(tree, *init, diag);
            }
        } else if (m->kind == parse::Kind::kFunctionDef) {
            resolveFunctionBody(tree, *m, diag);
        }
    }
    tree.open_ns_frames.pop_back();
}

// `alias Ns;` / `alias A:B;` — import a namespace's members into the current
// scope (push its frame onto the open chain for the rest of this lexical scope).
void resolveBareAlias(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    // The whole path (qualifier + name) names a namespace here.
    std::vector<std::string> segs = s.qualifier;
    segs.push_back(s.name);
    std::vector<int> toks = s.qualifier_toks;
    toks.push_back(s.name_tok);
    int frame = resolveNamespaceSegments(tree, segs, toks, s.global_qualified,
                                         s.file_id, diag);
    if (frame < 0) return;
    tree.open_ns_frames.push_back(frame);
}

// An inline qualified member declaration: `const int Space:kSix = 6;` defines a
// const member of an existing namespace by qualified name. Registers the entry
// (lifetime = current lexical frame) and resolves its init.
void resolveInlineQualifiedDecl(parse::Tree& tree, parse::Node& s,
                                diagnostic::Sink& diag) {
    if (!s.is_const) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Only constant members may be defined by qualified name.", {}});
        return;
    }
    int frame = resolveNamespaceSegments(tree, s.qualifier, s.qualifier_toks,
                                         s.global_qualified, s.file_id, diag);
    if (frame < 0) return;
    resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
    if (findMemberDeclared(tree, frame, s.name) >= 0) {
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Duplicate declaration of '" + s.name + "'.", {}});
        return;
    }
    parse::Entry e;
    e.kind = parse::EntryKind::kConst;
    e.name = s.name;
    e.slids_type = s.return_type;
    e.file_id = s.file_id;
    e.tok = s.name_tok;
    e.owner_ns_frame = frame;
    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
    for (auto& init : s.children) {
        if (init) resolveExpr(tree, *init, diag);
    }
}

// ---- Enums ------------------------------------------------------------------
// An enum lowers (at resolve, not desugar — members must be kConst by constfold
// so `Enum:member` folds) to: a namespace of the members (named enum), which
// doubles as a transparent type alias to the underlying; or bare consts in the
// enclosing scope (anonymous enum). Member values auto-increment by 1 (int) or
// 1.0 (float) from 0, with an explicit literal resetting the running counter
// (C rules).

bool isFloatUnderlying(std::string const& t) {
    return t == "float" || t == "float32" || t == "float64";
}

std::string enumFloatText(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    return buf;
}

// Deep-copy an expression subtree (member init exprs: literals + unary/binary).
// Needed because an implicit enum member's value is synthesized as
// `clone(last-explicit-init) + offset`, and the explicit init may be an
// arbitrary constant expression not yet folded at resolve time.
std::unique_ptr<parse::Node> cloneExpr(parse::Node const& src) {
    auto n = std::make_unique<parse::Node>();
    n->kind = src.kind;
    n->name = src.name;
    n->text = src.text;
    n->return_type = src.return_type;
    n->file_id = src.file_id;
    n->tok = src.tok;
    n->name_tok = src.name_tok;
    // A qualified reference (`Other:kX`, `::g`) carries its qualifier on these
    // fields, not in children — drop them and the clone re-resolves as a bare
    // name. (See feedback_cloner_pos_propagation: cloners must copy every field
    // resolution depends on.)
    n->qualifier = src.qualifier;
    n->qualifier_toks = src.qualifier_toks;
    n->global_qualified = src.global_qualified;
    for (auto const& c : src.children) {
        if (c) n->children.push_back(cloneExpr(*c));
    }
    return n;
}

// Synthesize auto-increment literal inits for members lacking one, then create a
// kConst entry per member. ns_frame >= 0: named enum (members owned by that
// namespace). ns_frame < 0: anonymous (members land in the current lexical
// frame, bare-visible).
void registerEnumMembers(parse::Tree& tree, parse::Node& node, int ns_frame,
                         diagnostic::Sink& diag) {
    bool is_float = isFloatUnderlying(node.return_type);
    // Auto-increment, C rules. A member with no init takes the previous value
    // plus one; an explicit init resets the run. Because an explicit init may
    // be a not-yet-folded expression (`kB = 1 + 2`), we synthesize an implicit
    // member's value as `clone(anchor) + steps` where `anchor` is the last
    // explicit init and `steps` counts members since it (constfold then folds
    // the whole thing). Before any explicit init, the value is just the member
    // ordinal. The offset literal matches the underlying family so constfold's
    // no-int/float-mix rule is satisfied.
    auto makeOffsetLiteral = [&](long long v, int file_id, int tok) {
        auto lit = std::make_unique<parse::Node>();
        if (is_float) {
            lit->kind = parse::Kind::kFloatLiteral;
            lit->text = enumFloatText(static_cast<double>(v));
        } else {
            lit->kind = parse::Kind::kIntLiteral;
            lit->text = std::to_string(v);
        }
        lit->file_id = file_id;
        lit->tok = tok;
        return lit;
    };
    parse::Node const* anchor = nullptr;  // last explicit init expr
    int steps = 0;                        // members since the anchor
    long long ordinal = 0;                // member index (used before any anchor)
    for (auto& m : node.children) {
        if (!m) continue;
        if (!m->children.empty()) {
            anchor = m->children[0].get();
            steps = 0;
        } else if (anchor == nullptr) {
            m->children.push_back(makeOffsetLiteral(ordinal, m->file_id, m->tok));
        } else {
            auto plus = std::make_unique<parse::Node>();
            plus->kind = parse::Kind::kBinaryExpr;
            plus->text = "+";
            plus->file_id = m->file_id;
            plus->tok = m->tok;
            plus->children.push_back(cloneExpr(*anchor));
            plus->children.push_back(makeOffsetLiteral(steps, m->file_id, m->tok));
            m->children.push_back(std::move(plus));
        }
        steps++;
        ordinal++;

        bool dup = (ns_frame >= 0)
            ? (findMemberDeclared(tree, ns_frame, m->name) >= 0)
            : (parse::findInFrame(tree, parse::currentFrameId(tree), m->name) >= 0);
        if (dup) {
            diagnostic::report(diag, {m->file_id, m->name_tok,
                "Duplicate declaration of '" + m->name + "'.", {}});
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = m->name;
        e.slids_type = node.return_type;
        e.file_id = m->file_id;
        e.tok = m->name_tok;
        e.owner_ns_frame = (ns_frame >= 0) ? ns_frame : -1;
        m->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
}

// Resolve enum member init expressions. Split from registerEnumMembers so a
// file-scope enum's inits resolve in a later pass — after every file-scope
// entry is collected — letting a member init reference a file-scope const
// (`const int kG = 6; enum E ( e = kG );`). At block scope all enclosing-scope
// entries already exist, so the body pass resolves inits right after register.
// For a named enum, open its own frame first so a member init can reference a
// sibling member bare (`enum E ( a, b = a )`); members already exist (registered
// above). The frame chain still falls back to the enclosing scope, so file-scope
// refs keep resolving. Anonymous enums need no push — their members are already
// bare in the enclosing frame.
void resolveEnumMemberInits(parse::Tree& tree, parse::Node& node,
                            diagnostic::Sink& diag) {
    bool named = !node.name.empty() && node.resolved_entry_id >= 0;
    if (named) tree.open_ns_frames.push_back(node.resolved_entry_id);
    for (auto& m : node.children) {
        if (!m) continue;
        for (auto& init : m->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }
    if (named) tree.open_ns_frames.pop_back();
}

// `parent_ns` is the enclosing namespace frame, or kGlobalFrame for an enum at
// file or function (lexical) scope. A named enum becomes a namespace member of
// parent_ns (or a lexical namespace entry at global scope); an anonymous enum's
// members land as namespace members of parent_ns, or — at lexical scope — as
// bare consts in the current frame.
void registerEnum(parse::Tree& tree, parse::Node& node, int parent_ns,
                  diagnostic::Sink& diag) {
    // Validate the underlying type spelling once (members inherit it).
    resolveDeclType(tree, node.return_type, node.file_id, node.tok, diag);
    if (node.name.empty()) {
        int owner = (parent_ns == kGlobalFrame) ? -1 : parent_ns;
        registerEnumMembers(tree, node, owner, diag);
        return;
    }
    int ns = openNamespace(tree, node.name, node.name_tok, node.file_id,
                           parent_ns, diag, node.return_type);
    if (ns < 0) return;
    node.resolved_entry_id = ns;
    registerEnumMembers(tree, node, ns, diag);
}

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIdentExpr: {
            int id;
            if (isQualified(e)) {
                id = resolveQualifiedRef(tree, e, diag);
                if (id < 0) return;
            } else {
                int other = -1;
                id = resolveNameDetail(tree, e.name, other);
                if (other >= 0) {
                    parse::Entry const& a = tree.entries[id];
                    parse::Entry const& b = tree.entries[other];
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Reference to '" + e.name + "' is ambiguous.",
                        {{a.file_id, a.tok, "could be this one"},
                         {b.file_id, b.tok, "or this one"}}});
                    return;
                }
                if (id < 0) {
                    if (namespaceMemberExists(tree, e.name)) {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "'" + e.name + "' needs a namespace qualifier.", {}});
                    } else {
                        diagnostic::report(diag, {e.file_id, e.tok,
                            "Unresolved identifier '" + e.name + "'.", {}});
                    }
                    return;
                }
            }
            if (tree.entries[id].kind == parse::EntryKind::kAlias) {
                parse::Entry const& a = tree.entries[id];
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a type, not a value.",
                    {{a.file_id, a.tok, "alias declared here"}}});
                return;
            }
            if (tree.entries[id].kind == parse::EntryKind::kNamespace) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' is a namespace, not a value.", {}});
                return;
            }
            // Definite-assignment: reading a local before it is written is a
            // compile error (a value-before-init footgun). Only kLocalVar is
            // tracked — consts are substituted away, params are pre-seeded.
            if (tree.entries[id].kind == parse::EntryKind::kLocalVar) {
                if (tree.initialized_locals.count(id) == 0) {
                    diagnostic::report(diag, {e.file_id, e.tok,
                        "Use of uninitialized variable '" + e.name + "'.", {}});
                    return;
                }
                // This is a value-position read; record it for the unused sweep.
                tree.read_locals.insert(id);
            }
            e.resolved_entry_id = id;
            return;
        }
        case parse::Kind::kCallExpr: {
            if (isPrintIntrinsic(e.name)) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "'" + e.name + "' cannot be used as an expression.", {}});
                for (auto& ch : e.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
                return;
            }
            resolveUserCall(tree, e, diag);
            return;
        }
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr: {
            // Operand must be an assignable variable. Phase 1: a bare ident
            // resolving to a LocalVar (no field / index lvalues yet).
            parse::Node& operand = *e.children[0];
            if (operand.kind != parse::Kind::kIdentExpr) {
                diagnostic::report(diag, {e.file_id, e.tok,
                    "The operand of '" + e.text + "' must be a variable.", {}});
                return;
            }
            resolveExpr(tree, operand, diag);
            if (operand.resolved_entry_id >= 0) {
                parse::Entry const& entry = tree.entries[operand.resolved_entry_id];
                if (entry.kind == parse::EntryKind::kFunction) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "'" + operand.name + "' is a function, not a variable.",
                        {{entry.file_id, entry.tok, "function declared here"}}});
                } else if (entry.kind == parse::EntryKind::kConst) {
                    diagnostic::report(diag, {operand.file_id, operand.tok,
                        "Constant '" + operand.name + "' cannot be incremented.",
                        {{entry.file_id, entry.tok, "constant declared here"}}});
                }
            }
            return;
        }
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kStringifyType:   // resolve the ##type operand
            for (auto& ch : e.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
            return;
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kVarDeclStmt:
        case parse::Kind::kAssignStmt:
        case parse::Kind::kAugAssignStmt:
        case parse::Kind::kCallStmt:
        case parse::Kind::kExprStmt:
        case parse::Kind::kAliasDecl:
        case parse::Kind::kNamespaceDecl:
        case parse::Kind::kEnumDecl:
        case parse::Kind::kReturnStmt:
        case parse::Kind::kBlockStmt:
        case parse::Kind::kIfStmt:
        case parse::Kind::kWhileStmt:
        case parse::Kind::kDoWhileStmt:
        case parse::Kind::kForLongStmt:
        case parse::Kind::kForEnumStmt:
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt:
        case parse::Kind::kParam:
            assert(false && "resolveExpr: not an expression kind");
            return;
    }
}

// Write target (assign / aug-assign lhs) — must point at a LocalVar entry.
bool resolveAssignTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id = isQualified(s) ? resolveQualifiedRef(tree, s, diag)
                            : resolveName(tree, s.name);
    if (id < 0) {
        if (!isQualified(s)) {
            diagnostic::report(diag, {s.file_id, s.tok,
                "Cannot assign to undeclared variable '" + s.name + "'.", {}});
        }
        return false;
    }
    parse::Entry const& entry = tree.entries[id];
    // Allowlist: only a local variable (incl. a param) is an assignable lvalue.
    // Every other kind rejects — an explicit arm per kind, no silent default
    // (a fall-through used to set resolved_entry_id and crash codegen with no
    // alloca). "variable" is unreachable here (kLocalVar is the accepted case);
    // it mirrors resolveCallTarget's `what` so the wording stays identical.
    if (entry.kind != parse::EntryKind::kLocalVar) {
        char const* what = entry.kind == parse::EntryKind::kAlias ? "type"
                         : entry.kind == parse::EntryKind::kConst ? "constant"
                         : entry.kind == parse::EntryKind::kNamespace ? "namespace"
                         : entry.kind == parse::EntryKind::kFunction ? "function"
                         : "variable";
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "Cannot assign to " + std::string(what) + " '" + s.name + "'.",
            {{entry.file_id, entry.tok, std::string(what) + " declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// Call target — must point at a Function entry.
bool resolveCallTarget(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    int id;
    if (isQualified(s)) {
        id = resolveQualifiedRef(tree, s, diag);
        if (id < 0) return false;
    } else {
        id = resolveName(tree, s.name);
        if (id < 0) {
            if (namespaceMemberExists(tree, s.name)) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "'" + s.name + "' needs a namespace qualifier.", {}});
            } else {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "Unknown function '" + s.name + "'.", {}});
            }
            return false;
        }
    }
    parse::Entry const& entry = tree.entries[id];
    if (entry.kind != parse::EntryKind::kFunction) {
        char const* what = entry.kind == parse::EntryKind::kAlias ? "type"
                         : entry.kind == parse::EntryKind::kConst ? "constant"
                         : entry.kind == parse::EntryKind::kNamespace ? "namespace"
                         : "variable";
        diagnostic::report(diag, {s.file_id, s.name_tok,
            "'" + s.name + "' is a " + what + ", not a function.",
            {{entry.file_id, entry.tok, std::string(what) + " declared here"}}});
        return false;
    }
    s.resolved_entry_id = id;
    return true;
}

// User-function call (statement or expression form): resolve the target,
// check arity against the entry's param list, and cache return + param types
// for downstream stages. Then recurse into the argument expressions.
void resolveUserCall(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    if (resolveCallTarget(tree, s, diag)) {
        parse::Entry const& entry = tree.entries[s.resolved_entry_id];
        if (s.children.size() != entry.param_types.size()) {
            diagnostic::report(diag, {s.file_id, s.tok,
                "Function '" + s.name + "' expects "
                + std::to_string(entry.param_types.size())
                + " arguments, got "
                + std::to_string(s.children.size()) + ".", {}});
        } else {
            s.return_type = entry.slids_type;
            s.param_types = entry.param_types;
        }
    }
    for (auto& ch : s.children) {
        if (ch) resolveExpr(tree, *ch, diag);
    }
}

// Whether a statement (or statement list) can fall through to its successor.
// Abrupt = it always transfers control elsewhere (return / break / continue).
// Drives the if/else join (an abrupt arm doesn't constrain the post-merge
// init-set) and 2A unreachable-statement detection.
enum class Completion { Normal, Abrupt };

// Intersection of two definite-assignment init-sets (a "must" join).
std::set<int> intersectInit(std::set<int> const& a, std::set<int> const& b) {
    std::set<int> out;
    for (int id : a) {
        if (b.count(id) > 0) out.insert(id);
    }
    return out;
}

// Fold one break/continue point's init-set into a loop-frame accumulator. The
// first fold seeds it (top ∩ X = X); later folds intersect. `seen` carries the
// top sentinel so "no break/continue yet" stays distinct from "intersected to {}".
void foldLoopExit(std::set<int>& accum, bool& seen, std::set<int> const& cur) {
    if (!seen) { accum = cur; seen = true; }
    else        accum = intersectInit(accum, cur);
}

// Resolve a statement list, threading Completion: once a statement is Abrupt,
// the next reachable statement is flagged "Unreachable statement." (2A) and the
// rest of the list is skipped (dead code declares no locals). Returns Abrupt if
// any statement in the list completes abruptly.
Completion resolveStmtList(parse::Tree& tree,
                           std::vector<std::unique_ptr<parse::Node>>& stmts,
                           diagnostic::Sink& diag);

Completion resolveStmt(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A qualified name defines a namespace member, not a local.
            if (isQualified(s)) {
                resolveInlineQualifiedDecl(tree, s, diag);
                return Completion::Normal;
            }
            // Consts in function bodies are pre-created in the forward-decl
            // pre-pass (resolveFunctionBody). If resolved_entry_id is set,
            // entry already exists; skip creation and dup-check.
            if (s.resolved_entry_id < 0) {
                std::string declared = s.return_type;   // pre-erasure spelling
                // A typeless const (block scope) defers type inference to constfold.
                if (!s.return_type.empty()) {
                    resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
                }
                int existing = parse::findInFrame(tree, parse::currentFrameId(tree), s.name);
                if (existing >= 0) {
                    parse::Entry const& prev = tree.entries[existing];
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Duplicate declaration of '" + s.name + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                } else {
                    parse::Entry e;
                    e.kind = s.is_const ? parse::EntryKind::kConst
                                        : parse::EntryKind::kLocalVar;
                    e.name = s.name;
                    e.slids_type = s.return_type;
                    // A named type (alias / enum / qualified) erased to a different
                    // underlying — keep the as-declared spelling as the ##type label.
                    if (declared != s.return_type) e.alias_label = declared;
                    e.file_id = s.file_id;
                    e.tok = s.name_tok;   // caret at the ident, not at 'const'/type
                    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                    // Track body-declared locals for the unused sweep. Consts
                    // are substituted away and never "unused" in this sense.
                    if (!s.is_const) {
                        tree.body_locals.push_back(s.resolved_entry_id);
                    }
                }
            }
            // Resolve the initializer (if any) BEFORE marking the local
            // initialized, so a self-reference reads as uninitialized
            // (`int x = x;` fires on the rhs). The kLocalVar guard intentionally
            // skips a kConst target (no definite-assignment tracking — consts
            // are required-init by grammar and substituted away) and a qualified
            // namespace-member decl (handled above, never reaches here).
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            if (!s.children.empty() && s.resolved_entry_id >= 0
                && tree.entries[s.resolved_entry_id].kind
                       == parse::EntryKind::kLocalVar) {
                tree.initialized_locals.insert(s.resolved_entry_id);
            }
            return Completion::Normal;
        }
        case parse::Kind::kAssignStmt: {
            // Inferred-init: a typeless assign to an UNDECLARED bare name declares
            // a fresh local whose type classify infers from the rhs (write-back).
            // A reassign (target already a local) and a wrong-kind target (const /
            // function / ...) both fall through to resolveAssignTarget below.
            if (!isQualified(s) && resolveName(tree, s.name) < 0) {
                parse::Entry e;
                e.kind = parse::EntryKind::kLocalVar;
                e.name = s.name;
                e.slids_type = "";          // classify stamps it from the rhs
                e.file_id = s.file_id;
                e.tok = s.name_tok;
                s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                tree.body_locals.push_back(s.resolved_entry_id);
                s.kind = parse::Kind::kVarDeclStmt;   // alloca + classify infer
                // rhs BEFORE marking initialized, so `x = x` reads x uninitialized.
                for (auto& ch : s.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
                tree.initialized_locals.insert(s.resolved_entry_id);
                return Completion::Normal;
            }
            resolveAssignTarget(tree, s, diag);
            // rhs BEFORE marking, so `x = x;` with x uninitialized still fires.
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            if (s.resolved_entry_id >= 0
                && tree.entries[s.resolved_entry_id].kind
                       == parse::EntryKind::kLocalVar) {
                tree.initialized_locals.insert(s.resolved_entry_id);
            }
            return Completion::Normal;
        }
        case parse::Kind::kAugAssignStmt: {
            if (resolveAssignTarget(tree, s, diag)) {
                // Cache lvalue type on the stmt so desugar's synthesized
                // IdentExpr inherits it without re-walking entries.
                s.return_type = parse::entryType(tree, s.resolved_entry_id);
                // An aug-assign READS the target before writing it, so it must
                // already be initialized (`x += 1` on an uninitialized x is an
                // error). resolveAssignTarget doesn't route through resolveExpr,
                // so check here, then mark written.
                int id = s.resolved_entry_id;
                if (tree.entries[id].kind == parse::EntryKind::kLocalVar
                    && tree.initialized_locals.count(id) == 0) {
                    diagnostic::report(diag, {s.file_id, s.name_tok,
                        "Use of uninitialized variable '" + s.name + "'.", {}});
                } else if (tree.entries[id].kind
                               == parse::EntryKind::kLocalVar) {
                    // An aug-assign reads the target's value (like `x++`), so it
                    // counts as a use for the unused sweep, then re-writes it.
                    tree.read_locals.insert(id);
                    tree.initialized_locals.insert(id);
                }
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kCallStmt: {
            if (isPrintIntrinsic(s.name)) {
                if (s.children.size() != 1) {
                    diagnostic::report(diag, {s.file_id, s.tok,
                        "'" + s.name + "' takes exactly one argument.", {}});
                }
                for (auto& ch : s.children) {
                    if (ch) resolveExpr(tree, *ch, diag);
                }
            } else {
                resolveUserCall(tree, s, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kAliasDecl: {
            // Bare `alias Ns;` imports a namespace's members into this scope.
            if (s.return_type.empty()) {
                resolveBareAlias(tree, s, diag);
                return Completion::Normal;
            }
            // Function-scope value alias: register in the body frame, then
            // validate the target (forward refs within a body aren't pre-scanned).
            registerAlias(tree, s, diag);
            resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
            return Completion::Normal;
        }
        case parse::Kind::kNamespaceDecl: {
            // A namespace opened in a function body: register its members
            // (lifetime = this body frame) then resolve their bodies. Parent is
            // the global namespace — a function body is not itself a namespace.
            registerNamespaceTree(tree, s, kGlobalFrame, diag);
            resolveNamespaceBodies(tree, s, kGlobalFrame, diag);
            return Completion::Normal;
        }
        case parse::Kind::kEnumDecl: {
            // An enum opened in a function body: registers its alias+namespace+
            // members (named) or bare consts (anonymous) in the current frame.
            // All enclosing-scope entries already exist here, so resolve the
            // member inits right away.
            registerEnum(tree, s, kGlobalFrame, diag);
            resolveEnumMemberInits(tree, s, diag);
            return Completion::Normal;
        }
        case parse::Kind::kExprStmt: {
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return Completion::Normal;
        }
        case parse::Kind::kReturnStmt: {
            // A long-for's update clause may not return (the spec; it runs at the
            // tail of each iteration). Banned transitively through the update.
            if (tree.in_for_update) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    "A 'return' statement is not allowed in a for-loop update "
                    "clause.", {}});
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            // A return transfers control out of the function — it never falls
            // through to its successor.
            return Completion::Abrupt;
        }
        case parse::Kind::kBlockStmt: {
            // A nested lexical scope. Push a frame so block-local decls live and
            // die with the block (and may shadow an outer name via the
            // innermost-first lookup). Definite-assignment state FLOWS THROUGH:
            // initialized_locals / read_locals are scoped, not isolated — an
            // assignment or read inside the block affects the enclosing local.
            // Only body_locals (declaration tracking for the unused sweep) is
            // block-scoped: save/clear on entry, sweep at block exit, restore.
            parse::pushFrame(tree);
            std::vector<int> saved_body_locals = std::move(tree.body_locals);
            tree.body_locals.clear();
            Completion c = resolveStmtList(tree, s.children, diag);
            sweepUnusedLocals(tree, diag);   // this block's declarations
            tree.body_locals = std::move(saved_body_locals);
            parse::popFrame(tree);
            // A block is Abrupt iff its statement list is.
            return c;
        }
        case parse::Kind::kIfStmt: {
            // children[0] = condition, [1] = then-block, [2] = optional else.
            // THE JOIN: snapshot the init-set S, resolve the then-arm from S,
            // restore S, resolve the else-arm from S; the post-merge set is the
            // INTERSECTION of the arms' contributions. An abrupt arm (one that
            // returns) never reaches the merge, so it contributes top (the
            // universal set) and is skipped from the ∩. A missing else arm
            // contributes S unchanged (T ∩ S = S — an else-less if never adds an
            // initialization). read_locals is NOT snapshotted: a read on any path
            // is a use (monotonic union).
            assert(s.children.size() >= 2 && "kIfStmt needs condition + then");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;

            Completion then_c = resolveStmt(tree, *s.children[1], diag);
            std::set<int> then_out = std::move(tree.initialized_locals);

            tree.initialized_locals = entry;   // restore S for the else arm
            bool has_else = s.children.size() > 2 && s.children[2];
            Completion else_c = Completion::Normal;
            std::set<int> else_out;
            if (has_else) {
                else_c = resolveStmt(tree, *s.children[2], diag);
                else_out = std::move(tree.initialized_locals);
            } else {
                else_out = entry;   // no-else: the fall-through path keeps S
            }

            // Join: ∩ of normally-completing arms; abrupt arms skipped (top).
            std::set<int> joined;
            bool then_in = (then_c == Completion::Normal);
            bool else_in = (else_c == Completion::Normal);
            if (then_in && else_in) {
                for (int id : then_out) {
                    if (else_out.count(id) > 0) joined.insert(id);
                }
            } else if (then_in) {
                joined = std::move(then_out);
            } else if (else_in) {
                joined = std::move(else_out);
            } else {
                // Both arms abrupt: the merge is unreachable. The init-set is
                // moot; keep S so an erroneous downstream read still attributes.
                joined = std::move(entry);
            }
            tree.initialized_locals = std::move(joined);

            // The if is Abrupt iff every path through it is — both arms abrupt,
            // and an else exists (an else-less if always has a fall-through).
            if (has_else && then_c == Completion::Abrupt
                && else_c == Completion::Abrupt) {
                return Completion::Abrupt;
            }
            return Completion::Normal;
        }
        case parse::Kind::kWhileStmt: {
            // children[0] = condition, [1] = body block. Per 3B every pre-
            // condition loop is possibly-zero: the condition is tested before the
            // body, so its reads must be satisfied by the entry set S; the body
            // resolves from S but its inits do NOT escape (the loop may run zero
            // times), so the post-loop set is S again — regardless of any break
            // (the zero-iteration exit always contributes S, which dominates the
            // ∩). A while always completes Normally — it can fall through to its
            // exit (even an apparent while(true) is treated as possibly-zero; no
            // constant-true special case). The loop frame is pushed only so a
            // break/continue has a target + legality; its accumulators are unused.
            assert(s.children.size() == 2 && "kWhileStmt needs condition + body");
            resolveExpr(tree, *s.children[0], diag);
            std::set<int> entry = tree.initialized_locals;
            tree.loop_stack.push_back({});
            resolveStmt(tree, *s.children[1], diag);   // body block, from S
            tree.loop_stack.pop_back();
            tree.initialized_locals = std::move(entry);   // after = S
            return Completion::Normal;
        }
        case parse::Kind::kDoWhileStmt: {
            // children[0] = condition, [1] = body block. The body runs AT LEAST
            // once, so its inits escape — but a break can cut the after-set short
            // and a continue can reach the condition with fewer inits. Resolve the
            // body from S (the first iteration's entry, which is also the
            // must-analysis fixpoint for the back-edge); the body's normal-
            // completion out-set is B. The condition is tested after a normal
            // fall-through OR a continue, so its reads must hold on B ∩
            // continue_accum. The loop exits via the condition (init-set =
            // cond_in, since the test adds nothing) or via a break, so
            // after = cond_in ∩ break_accum.
            assert(s.children.size() == 2 && "kDoWhileStmt needs condition + body");
            // No S snapshot: the body resolves from the current init-set (= S)
            // and a do-while never restores S (its inits escape), unlike if/while.
            tree.loop_stack.push_back({});
            resolveStmt(tree, *s.children[1], diag);   // body block, from S
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();

            std::set<int> body_out = tree.initialized_locals;       // B
            std::set<int> cond_in = lf.continue_seen
                ? intersectInit(body_out, lf.continue_init)         // B ∩ continues
                : body_out;
            tree.initialized_locals = cond_in;
            resolveExpr(tree, *s.children[0], diag);   // condition reads from cond_in

            std::set<int> after = lf.break_seen
                ? intersectInit(cond_in, lf.break_init)             // ∩ break exits
                : cond_in;
            tree.initialized_locals = std::move(after);
            // Like a pre-condition while, treated as Normal-completing (3B): even
            // a body that always returns is conservatively assumed to be able to
            // reach its exit, so a trailing return is still demanded after it.
            return Completion::Normal;
        }
        case parse::Kind::kForLongStmt: {
            // children[0]=cond, [1]=update block, [2]=body block, [3..]=varlist.
            // Three scope frames: a for-scope holds the varlist, with the update
            // and body as sibling nested blocks (each pushes its own frame, so the
            // body may shadow a for-var). Pre-condition / possibly-zero, so the
            // post-loop init-set is the entry set S — body/update inits don't
            // escape, and the for-vars leave scope (like a pre-condition while).
            assert(s.children.size() >= 3 && "kForLongStmt needs cond+update+body");
            std::set<int> entry = tree.initialized_locals;   // S (before varlist)
            parse::pushFrame(tree);                            // for-scope
            std::vector<int> saved_body_locals = std::move(tree.body_locals);
            tree.body_locals.clear();
            // varlist decls live in the for-scope (explicit type each). S -> S'.
            for (std::size_t i = 3; i < s.children.size(); i++) {
                if (s.children[i]) resolveStmt(tree, *s.children[i], diag);
            }
            // The condition is tested before the body, so its reads must hold on
            // the post-varlist set S' (the first iteration's entry, also the
            // back-edge fixpoint).
            resolveExpr(tree, *s.children[0], diag);
            // Body FIRST (execution is body-then-update): break/continue target
            // the for; continue folds into the loop frame's continue accumulator.
            tree.loop_stack.push_back({});
            resolveStmt(tree, *s.children[2], diag);   // body block, from S'
            parse::Tree::LoopFrame lf = std::move(tree.loop_stack.back());
            tree.loop_stack.pop_back();
            // The update runs after the body completes normally OR after a
            // continue, so its reads must hold on body-out ∩ continue_accum (this
            // lets the update see body-assigned vars, but not ones a continue
            // skipped). break_accum is moot — possibly-zero, so after = S anyway.
            std::set<int> body_out = tree.initialized_locals;
            if (lf.continue_seen) {
                tree.initialized_locals = intersectInit(body_out, lf.continue_init);
            }
            // Update clause: restricted (no break/continue/return) and NOT a loop
            // target — resolved with the for's loop frame already popped.
            bool saved_in_update = tree.in_for_update;
            int saved_floor = tree.for_update_floor;
            tree.in_for_update = true;
            tree.for_update_floor = (int)tree.loop_stack.size();
            resolveStmt(tree, *s.children[1], diag);   // update block
            tree.in_for_update = saved_in_update;
            tree.for_update_floor = saved_floor;
            // Sweep the for-scope-declared locals (the varlist) for unused.
            sweepUnusedLocals(tree, diag);
            tree.body_locals = std::move(saved_body_locals);
            parse::popFrame(tree);
            tree.initialized_locals = std::move(entry);   // after = S
            return Completion::Normal;
        }
        case parse::Kind::kForEnumStmt: {
            // children[0]=loop-var decl, [1]=enum-ref, [2]=body. Rewrite IN PLACE
            // into a kForLongStmt over the enum's first..last defined members,
            // then resolve that (so the whole for-long pipeline is reused). The
            // first/last kConst members are referenced by qualified name, so the
            // normal resolve+constfold path fills their values; an empty/descending
            // enum (first > last) then trips the empty-range check ("Invalid
            // range." on the enum name, via range_dotdot_tok).
            assert(s.children.size() == 3 && "kForEnumStmt needs var+enum+body");
            parse::Node& enum_ref = *s.children[1];
            int enum_id = isQualified(enum_ref)
                ? resolveQualifiedRef(tree, enum_ref, diag)
                : resolveName(tree, enum_ref.name);
            if (enum_id < 0) {
                if (!isQualified(enum_ref)) {
                    diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                        "Unknown enum '" + enum_ref.name + "'.", {}});
                }
                return Completion::Normal;
            }
            // An enum is a kNamespace carrying an underlying type (transparent).
            if (tree.entries[enum_id].kind != parse::EntryKind::kNamespace
                || tree.entries[enum_id].slids_type.empty()) {
                parse::Entry const& bad = tree.entries[enum_id];
                diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                    "'" + enum_ref.name + "' is not an enum.",
                    {{bad.file_id, bad.tok, "declared here"}}});
                return Completion::Normal;
            }
            int member_frame = tree.entries[enum_id].ns_frame_id;
            int first = -1, last = -1;
            for (std::size_t i = 0; i < tree.entries.size(); i++) {
                parse::Entry const& m = tree.entries[i];
                if (m.kind == parse::EntryKind::kConst
                    && m.owner_ns_frame == member_frame) {
                    if (first < 0) first = (int)i;
                    last = (int)i;
                }
            }
            if (first < 0) {
                diagnostic::report(diag, {enum_ref.file_id, enum_ref.tok,
                    "Enum '" + enum_ref.name + "' has no members to iterate.", {}});
                return Completion::Normal;
            }
            // Capture enum-ref shape before we rebuild s.children.
            std::vector<std::string> en_qual = enum_ref.qualifier;
            std::vector<int> en_qtoks = enum_ref.qualifier_toks;
            std::string en_name = enum_ref.name;
            bool en_global = enum_ref.global_qualified;
            int en_file = enum_ref.file_id;
            int en_tok = enum_ref.tok;
            int en_nametok = enum_ref.name_tok >= 0 ? enum_ref.name_tok : enum_ref.tok;

            auto ident = [&](std::string nm, int tok) {
                auto n = std::make_unique<parse::Node>();
                n->kind = parse::Kind::kIdentExpr;
                n->name = std::move(nm);
                n->file_id = en_file; n->tok = tok; n->name_tok = tok;
                return n;
            };
            // A qualified ref `<enum-path>:member` to a first/last member.
            auto memberRef = [&](int mid) {
                auto r = ident(tree.entries[mid].name, en_tok);
                r->qualifier = en_qual;
                r->qualifier.push_back(en_name);
                r->qualifier_toks = en_qtoks;
                r->qualifier_toks.push_back(en_nametok);
                r->global_qualified = en_global;
                return r;
            };

            // Reuse the loop-var decl; init it to the first member.
            std::unique_ptr<parse::Node> var_decl = std::move(s.children[0]);
            std::string var_name = var_decl->name;
            std::string var_type = var_decl->return_type;
            int var_tok = var_decl->name_tok;
            var_decl->children.push_back(memberRef(first));
            // _$end = last member.
            auto end_decl = std::make_unique<parse::Node>();
            end_decl->kind = parse::Kind::kVarDeclStmt;
            end_decl->name = "_$end";
            end_decl->return_type = var_type;
            end_decl->file_id = en_file;
            end_decl->tok = en_tok;
            end_decl->name_tok = en_tok;
            end_decl->children.push_back(memberRef(last));
            // cond: var <= _$end
            auto cond = std::make_unique<parse::Node>();
            cond->kind = parse::Kind::kBinaryExpr;
            cond->text = "<=";
            cond->file_id = en_file; cond->tok = en_tok;
            cond->children.push_back(ident(var_name, var_tok));
            cond->children.push_back(ident("_$end", en_tok));
            // update: { var = var + 1 }
            auto one = std::make_unique<parse::Node>();
            one->kind = parse::Kind::kIntLiteral; one->text = "1";
            one->file_id = en_file; one->tok = en_tok;
            auto add = std::make_unique<parse::Node>();
            add->kind = parse::Kind::kBinaryExpr; add->text = "+";
            add->file_id = en_file; add->tok = en_tok;
            add->children.push_back(ident(var_name, var_tok));
            add->children.push_back(std::move(one));
            auto assign = std::make_unique<parse::Node>();
            assign->kind = parse::Kind::kAssignStmt;
            assign->name = var_name; assign->name_tok = var_tok;
            assign->file_id = en_file; assign->tok = var_tok;
            assign->children.push_back(std::move(add));
            auto update = std::make_unique<parse::Node>();
            update->kind = parse::Kind::kBlockStmt;
            update->file_id = en_file; update->tok = en_tok;
            update->children.push_back(std::move(assign));

            std::unique_ptr<parse::Node> body = std::move(s.children[2]);

            // Rebuild s as a kForLongStmt and resolve it through that path.
            s.kind = parse::Kind::kForLongStmt;
            s.range_dotdot_tok = en_tok;   // empty-range caret on the enum name
            s.children.clear();
            s.children.push_back(std::move(cond));       // [0]
            s.children.push_back(std::move(update));     // [1]
            s.children.push_back(std::move(body));       // [2]
            s.children.push_back(std::move(var_decl));   // [3]
            s.children.push_back(std::move(end_decl));   // [4]
            return resolveStmt(tree, s, diag);
        }
        case parse::Kind::kBreakStmt:
        case parse::Kind::kContinueStmt: {
            // A loop exit / restart: legal only inside a loop body, and it
            // transfers control (Abrupt — a following sibling is unreachable).
            // Fold the current init-set into the enclosing loop's accumulator so
            // a do-while can intersect it (a pre-condition while ignores it).
            char const* what =
                s.kind == parse::Kind::kBreakStmt ? "break" : "continue";
            if (tree.in_for_update
                && (int)tree.loop_stack.size() == tree.for_update_floor) {
                // Directly in a long-for update clause (no nested loop absorbs
                // it) — the update may not break/continue the for.
                diagnostic::report(diag, {s.file_id, s.tok,
                    std::string("A '") + what
                        + "' statement is not allowed in a for-loop update "
                          "clause.", {}});
            } else if (tree.loop_stack.empty()) {
                diagnostic::report(diag, {s.file_id, s.tok,
                    std::string("A '") + what
                        + "' statement must be inside a loop.", {}});
            } else {
                parse::Tree::LoopFrame& lf = tree.loop_stack.back();
                if (s.kind == parse::Kind::kBreakStmt) {
                    foldLoopExit(lf.break_init, lf.break_seen,
                                 tree.initialized_locals);
                } else {
                    foldLoopExit(lf.continue_init, lf.continue_seen,
                                 tree.initialized_locals);
                }
            }
            return Completion::Abrupt;
        }
        case parse::Kind::kProgram:
        case parse::Kind::kFunctionDef:
        case parse::Kind::kFunctionDecl:
        case parse::Kind::kStringLiteral:
        case parse::Kind::kIntLiteral:
        case parse::Kind::kUintLiteral:
        case parse::Kind::kCharLiteral:
        case parse::Kind::kBoolLiteral:
        case parse::Kind::kFloatLiteral:
        case parse::Kind::kIdentExpr:
        case parse::Kind::kUnaryExpr:
        case parse::Kind::kBinaryExpr:
        case parse::Kind::kPreIncExpr:
        case parse::Kind::kPostIncExpr:
        case parse::Kind::kStringifyType:
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "resolveStmt: not a statement kind");
            return Completion::Normal;
    }
    assert(false && "resolveStmt: unhandled parse::Kind");
    return Completion::Normal;
}

Completion resolveStmtList(parse::Tree& tree,
                           std::vector<std::unique_ptr<parse::Node>>& stmts,
                           diagnostic::Sink& diag) {
    for (std::size_t i = 0; i < stmts.size(); i++) {
        if (!stmts[i]) continue;
        Completion c = resolveStmt(tree, *stmts[i], diag);
        if (c == Completion::Abrupt) {
            // 2A: every statement after an abrupt one is unreachable. Flag the
            // next real statement (if any) once, then stop — dead code declares
            // no locals (so it can't trip the unused sweep) and resolving it
            // would only spawn follow-on diagnostics.
            for (std::size_t j = i + 1; j < stmts.size(); j++) {
                if (!stmts[j]) continue;
                diagnostic::report(diag, {stmts[j]->file_id, stmts[j]->tok,
                    "Unreachable statement.", {}});
                break;
            }
            return Completion::Abrupt;
        }
    }
    return Completion::Normal;
}

void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag) {
    parse::pushFrame(tree);
    // Bare `alias Ns;` imports inside this body extend the open-namespace chain;
    // restore it on exit so imports don't leak to sibling functions.
    std::size_t open_ns_at_entry = tree.open_ns_frames.size();
    // Definite-assignment + unused-local state is per-body; clear on entry and
    // restore on exit so one function's locals don't leak into the next.
    std::set<int> saved_initialized = std::move(tree.initialized_locals);
    std::set<int> saved_read = std::move(tree.read_locals);
    std::vector<int> saved_body_locals = std::move(tree.body_locals);
    tree.initialized_locals.clear();
    tree.read_locals.clear();
    tree.body_locals.clear();
    // Params become LocalVar entries in the body frame. Type spellings were
    // already validated in pass 1. A param arrives initialized (the caller
    // supplied its value), so it is seeded into initialized_locals.
    for (auto& p : fn.params) {
        if (!p) continue;
        int existing = parse::findInFrame(tree, parse::currentFrameId(tree), p->name);
        if (existing >= 0) {
            parse::Entry const& prev = tree.entries[existing];
            diagnostic::report(diag, {p->file_id, p->tok,
                "Duplicate parameter name '" + p->name + "'.",
                {{prev.file_id, prev.tok, "first declared here"}}});
            continue;
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kLocalVar;
        e.name = p->name;
        e.slids_type = p->return_type;
        e.alias_label = p->alias_label;   // alias/enum spelling captured in pass 1
        e.file_id = p->file_id;
        e.tok = p->name_tok;
        p->resolved_entry_id = parse::addEntry(tree, std::move(e));
        tree.initialized_locals.insert(p->resolved_entry_id);
    }
    // Forward-decl pre-pass for kConst: pre-create entries so const init
    // expressions can reference later-declared consts in the same body.
    // Dup detection is deferred to the main pass below (which emits a single
    // "Duplicate declaration" diagnostic with a multi-source note).
    for (auto& ch : fn.children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        if (isQualified(*ch)) continue;  // qualified const is a namespace member
        if (parse::findInFrame(tree, parse::currentFrameId(tree), ch->name) >= 0) {
            continue;  // pre-pass yields to main-pass dup detection
        }
        // A typeless const has no spelling to validate; constfold infers its
        // type from the rhs and stamps slids_type later.
        if (!ch->return_type.empty()) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        }
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    resolveStmtList(tree, fn.children, diag);
    sweepUnusedLocals(tree, diag);   // function-body declarations
    tree.body_locals = std::move(saved_body_locals);
    tree.read_locals = std::move(saved_read);
    tree.initialized_locals = std::move(saved_initialized);
    tree.open_ns_frames.resize(open_ns_at_entry);
    parse::popFrame(tree);
}

}  // namespace

void run(parse::Tree& tree, diagnostic::Sink& diag) {
    parse::Node* program = nullptr;
    for (auto& n : tree.nodes) {
        if (n && n->kind == parse::Kind::kProgram) {
            program = n.get();
            break;
        }
    }
    if (!program) return;

    parse::pushFrame(tree);   // program frame

    // Pass 1a-alias — register all file-scope value aliases first, so any decl
    // below (in any order) can resolve through them, then validate each target.
    // Bare `alias Ns;` (no target) is a namespace import, handled at use scope.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && !ch->return_type.empty()) {
            registerAlias(tree, *ch, diag);
        }
    }
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl
            && !ch->return_type.empty()) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        }
    }

    // Pass 1a-enum — register every file-scope enum (alias + namespace + member
    // consts, or bare consts for anonymous) before namespaces / bare aliases so
    // a later `alias EnumName;` import and qualified `Enum:member` both resolve.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kEnumDecl) {
            registerEnum(tree, *ch, kGlobalFrame, diag);
        }
    }

    // Pass 1a-ns — register every file-scope namespace and its members'
    // signatures (recursively), across all reopens, before any body or inline
    // qualified member decl. Members live in the global frame (file lifetime).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kNamespaceDecl) {
            registerNamespaceTree(tree, *ch, kGlobalFrame, diag);
        }
    }

    // Pass 1a — collect entries at program scope WITHOUT walking init
    // expressions. This lets globals reference each other regardless of
    // declaration order (forward-decl semantics).
    for (auto& ch : program->children) {
        if (!ch) continue;
        if (ch->kind == parse::Kind::kFunctionDef
         || ch->kind == parse::Kind::kFunctionDecl) {
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            std::vector<std::string> param_types;
            for (auto& p : ch->params) {
                if (!p) continue;
                std::string declared = p->return_type;   // pre-erasure spelling
                resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
                if (declared != p->return_type) p->alias_label = declared;
                param_types.push_back(p->return_type);
            }
            bool is_def = (ch->kind == parse::Kind::kFunctionDef);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry& prev = tree.entries[existing];
                if (prev.slids_type != ch->return_type) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Return type '" + ch->return_type
                        + "' does not match earlier declaration's '"
                        + prev.slids_type + "'.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                    continue;
                }
                if (prev.param_types != param_types) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Parameter types do not match earlier declaration.",
                        {{prev.file_id, prev.tok, "first declared here"}}});
                    continue;
                }
                if (is_def && prev.defined) {
                    diagnostic::report(diag, {ch->file_id, ch->tok,
                        "Duplicate definition of '" + ch->name + "'.",
                        {{prev.file_id, prev.tok, "first defined here"}}});
                    continue;
                }
                if (is_def) prev.defined = true;
                ch->resolved_entry_id = existing;
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kFunction;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.param_types = std::move(param_types);
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            e.defined = is_def;
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kVarDeclStmt && ch->is_const) {
            // A qualified file-scope const defines a namespace member; handled
            // in the inline-member pass below (the namespace must exist first).
            if (isQualified(*ch)) continue;
            resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
            int existing = parse::findInFrame(tree, parse::currentFrameId(tree), ch->name);
            if (existing >= 0) {
                parse::Entry const& prev = tree.entries[existing];
                diagnostic::report(diag, {ch->file_id, ch->name_tok,
                    "Duplicate declaration of '" + ch->name + "'.",
                    {{prev.file_id, prev.tok, "first declared here"}}});
                continue;
            }
            parse::Entry e;
            e.kind = parse::EntryKind::kConst;
            e.name = ch->name;
            e.slids_type = ch->return_type;
            e.file_id = ch->file_id;
            e.tok = ch->name_tok;
            ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
            continue;
        }
        if (ch->kind == parse::Kind::kAliasDecl) continue;     // handled above
        if (ch->kind == parse::Kind::kNamespaceDecl) continue; // handled above
        if (ch->kind == parse::Kind::kEnumDecl) continue;      // handled above
        // Mutable globals + other top-level shapes not supported today.
        // Grammar rejects them; if one slips through, it's a grammar bug.
        diagnostic::report(diag, {ch->file_id, ch->tok,
            "Only function definitions, function declarations, and "
            "constants are allowed at file scope.", {}});
    }

    // Pass 1a-inline — file-scope qualified const members (`const int Space:kSix
    // = 6;`). The namespace exists now; register the member and resolve its init.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kVarDeclStmt && isQualified(*ch)) {
            resolveInlineQualifiedDecl(tree, *ch, diag);
        }
    }

    // Pass 1a-import — file-scope bare `alias Ns;`. Opens the namespace for the
    // whole file: pushed onto open_ns_frames and never popped within run() (file
    // scope = the whole file), so all const inits and function bodies below see
    // its members unqualified.
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kAliasDecl && ch->return_type.empty()) {
            resolveBareAlias(tree, *ch, diag);
        }
    }

    // Pass 1b-enum — resolve file-scope enum member init expressions now that
    // every program-scope entry is registered, so a member init can reference a
    // file-scope const (registration happened early in Pass 1a-enum).
    for (auto& ch : program->children) {
        if (ch && ch->kind == parse::Kind::kEnumDecl) {
            resolveEnumMemberInits(tree, *ch, diag);
        }
    }

    // Pass 1b — walk each top-level const's init expression to resolve
    // ident references. By now every program-scope entry is in the table,
    // so forward refs between globals resolve cleanly.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kVarDeclStmt || !ch->is_const) continue;
        if (isQualified(*ch)) continue;   // inline member init resolved above
        for (auto& init : ch->children) {
            if (init) resolveExpr(tree, *init, diag);
        }
    }

    // Pass 2 — walk each function body.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kFunctionDef) continue;
        resolveFunctionBody(tree, *ch, diag);
    }

    // Pass 2-ns — resolve namespace member bodies (const inits + function
    // bodies), which see their enclosing namespace's members unqualified.
    for (auto& ch : program->children) {
        if (!ch || ch->kind != parse::Kind::kNamespaceDecl) continue;
        resolveNamespaceBodies(tree, *ch, kGlobalFrame, diag);
    }

    // Pass 3 — orphan declarations. A function declared but never defined
    // (anywhere, used or not) is a compile error: a call to it would emit a
    // `declare` with no `define` and llc would reject the IR. Caret at the
    // declaration's name. (Cross-TU `.slh` headers legitimately declare-only;
    // that distinction defers with the rest of the .slh work.)
    for (parse::Entry const& e : tree.entries) {
        if (e.kind == parse::EntryKind::kFunction && !e.defined) {
            diagnostic::report(diag, {e.file_id, e.tok,
                "Function '" + e.name + "' is declared but never defined.", {}});
        }
    }

    parse::popFrame(tree);
}

}  // namespace resolve
