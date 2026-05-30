#include "resolve.h"

#include <cassert>
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

// Substitute a type-alias spelling to its underlying type, following chains
// (`alias A = B; alias B = int` → `int`) and detecting cycles. The `[]` suffix
// rides along. A spelling that isn't an alias entry is returned unchanged.
std::string resolveTypeSpelling(parse::Tree& tree, std::string const& spelling,
                                std::set<std::string>& visiting, bool& cyclic,
                                int file_id, int tok, diagnostic::Sink& diag) {
    std::string base = spelling;
    std::string suffix;
    while (base.size() >= 2 && base.compare(base.size() - 2, 2, "[]") == 0) {
        suffix += "[]";
        base.resize(base.size() - 2);
    }
    int id = parse::findInLiveScopes(tree, base);
    if (id < 0 || tree.entries[id].kind != parse::EntryKind::kAlias) {
        return spelling;
    }
    if (visiting.count(base)) {
        cyclic = true;
        diagnostic::report(diag, {file_id, tok,
            "Type alias '" + base + "' is part of a cycle.", {}});
        return base;
    }
    visiting.insert(base);
    std::string resolved = resolveTypeSpelling(
        tree, tree.entries[id].slids_type, visiting, cyclic, file_id, tok, diag);
    visiting.erase(base);
    return resolved + suffix;
}

// Rewrite a declared type spelling in place to its underlying type, then
// require the result to be a known type. A cycle has already been reported, so
// skip the redundant "Unknown type" that the broken chain would otherwise emit.
void resolveDeclType(parse::Tree& tree, std::string& spelling,
                     int file_id, int tok, diagnostic::Sink& diag) {
    std::set<std::string> visiting;
    bool cyclic = false;
    spelling = resolveTypeSpelling(tree, spelling, visiting, cyclic,
                                   file_id, tok, diag);
    if (!cyclic) requireKnownType(spelling, file_id, tok, diag);
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
int resolveName(parse::Tree const& tree, std::string const& name) {
    for (auto it = tree.open_ns_frames.rbegin();
         it != tree.open_ns_frames.rend(); ++it) {
        int id = findMemberLive(tree, *it, name);
        if (id >= 0) return id;
    }
    for (auto it = tree.live_entry_ids.rbegin();
         it != tree.live_entry_ids.rend(); ++it) {
        parse::Entry const& e = tree.entries[*it];
        if (e.name == name && e.owner_ns_frame < 0) return *it;
    }
    return -1;
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

// ---- Namespace registration -------------------------------------------------
void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag);
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag);
void resolveNamespaceBodies(parse::Tree& tree, parse::Node& node,
                            int parent_ns, diagnostic::Sink& diag);

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
                  int file_id, int parent_ns, diagnostic::Sink& diag) {
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
        "Only constants, functions, and namespaces may appear in a namespace.",
        {}});
}

// Register a namespace and all its members' signatures (no bodies). Recurses
// into nested namespaces. Members inherit the current lexical lifetime.
void registerNamespaceTree(parse::Tree& tree, parse::Node& node,
                           int parent_ns, diagnostic::Sink& diag) {
    int ns = openNamespace(tree, node.name, node.name_tok, node.file_id,
                           parent_ns, diag);
    if (ns < 0) return;
    node.resolved_entry_id = ns;   // stash the ns frame for the body pass
    for (auto& m : node.children) {
        if (m) registerMemberSignature(tree, *m, ns, diag);
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

void resolveExpr(parse::Tree& tree, parse::Node& e, diagnostic::Sink& diag) {
    switch (e.kind) {
        case parse::Kind::kIdentExpr: {
            int id;
            if (isQualified(e)) {
                id = resolveQualifiedRef(tree, e, diag);
                if (id < 0) return;
            } else {
                id = resolveName(tree, e.name);
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
        case parse::Kind::kReturnStmt:
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

void resolveStmt(parse::Tree& tree, parse::Node& s, diagnostic::Sink& diag) {
    switch (s.kind) {
        case parse::Kind::kVarDeclStmt: {
            // A qualified name defines a namespace member, not a local.
            if (isQualified(s)) {
                resolveInlineQualifiedDecl(tree, s, diag);
                return;
            }
            // Consts in function bodies are pre-created in the forward-decl
            // pre-pass (resolveFunctionBody). If resolved_entry_id is set,
            // entry already exists; skip creation and dup-check.
            if (s.resolved_entry_id < 0) {
                resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
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
                    e.file_id = s.file_id;
                    e.tok = s.name_tok;   // caret at the ident, not at 'const'/type
                    s.resolved_entry_id = parse::addEntry(tree, std::move(e));
                }
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kAssignStmt: {
            resolveAssignTarget(tree, s, diag);
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
        }
        case parse::Kind::kAugAssignStmt: {
            if (resolveAssignTarget(tree, s, diag)) {
                // Cache lvalue type on the stmt so desugar's synthesized
                // IdentExpr inherits it without re-walking entries.
                s.return_type = parse::entryType(tree, s.resolved_entry_id);
            }
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
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
            return;
        }
        case parse::Kind::kAliasDecl: {
            // Bare `alias Ns;` imports a namespace's members into this scope.
            if (s.return_type.empty()) {
                resolveBareAlias(tree, s, diag);
                return;
            }
            // Function-scope value alias: register in the body frame, then
            // validate the target (forward refs within a body aren't pre-scanned).
            registerAlias(tree, s, diag);
            resolveDeclType(tree, s.return_type, s.file_id, s.tok, diag);
            return;
        }
        case parse::Kind::kNamespaceDecl: {
            // A namespace opened in a function body: register its members
            // (lifetime = this body frame) then resolve their bodies. Parent is
            // the global namespace — a function body is not itself a namespace.
            registerNamespaceTree(tree, s, kGlobalFrame, diag);
            resolveNamespaceBodies(tree, s, kGlobalFrame, diag);
            return;
        }
        case parse::Kind::kExprStmt:
        case parse::Kind::kReturnStmt: {
            for (auto& ch : s.children) {
                if (ch) resolveExpr(tree, *ch, diag);
            }
            return;
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
        case parse::Kind::kCallExpr:
        case parse::Kind::kParam:
            assert(false && "resolveStmt: not a statement kind");
            return;
    }
}

void resolveFunctionBody(parse::Tree& tree, parse::Node& fn,
                         diagnostic::Sink& diag) {
    parse::pushFrame(tree);
    // Bare `alias Ns;` imports inside this body extend the open-namespace chain;
    // restore it on exit so imports don't leak to sibling functions.
    std::size_t open_ns_at_entry = tree.open_ns_frames.size();
    // Params become LocalVar entries in the body frame. Type spellings were
    // already validated in pass 1.
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
        e.file_id = p->file_id;
        e.tok = p->name_tok;
        p->resolved_entry_id = parse::addEntry(tree, std::move(e));
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
        resolveDeclType(tree, ch->return_type, ch->file_id, ch->tok, diag);
        parse::Entry e;
        e.kind = parse::EntryKind::kConst;
        e.name = ch->name;
        e.slids_type = ch->return_type;
        e.file_id = ch->file_id;
        e.tok = ch->name_tok;
        ch->resolved_entry_id = parse::addEntry(tree, std::move(e));
    }
    for (auto& ch : fn.children) {
        if (ch) resolveStmt(tree, *ch, diag);
    }
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
                resolveDeclType(tree, p->return_type, p->file_id, p->tok, diag);
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
